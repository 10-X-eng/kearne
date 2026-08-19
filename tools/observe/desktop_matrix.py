#!/usr/bin/env python3
"""Generate and verify Kearne desktop observation scenarios."""

from __future__ import annotations

import argparse
from collections import Counter
import hashlib
import json
import math
import os
from pathlib import Path
import shutil
import stat
import struct
import subprocess
import sys
import tempfile
import zlib


SURFACES = ("editor", "projects", "settings", "recovery", "operations")
WORKSPACES = (
    "model", "sketch", "assemble", "sheet-metal", "simulate", "cam", "drawing", "bom",
)
STATES = (
    "empty", "loading", "current", "preview", "pending", "stale", "failed",
    "unavailable", "read-only", "permission-denied",
)
SETTINGS_CATEGORIES = ("appearance", "units", "input", "files", "compute", "agent")
ACTIONABLE_ROLES = {"button", "tab", "searchbox", "textbox", "combobox", "switch"}
ROUTE_ACTIONS = {
    "editor": ("projects", "navigation.editor"),
    "projects": ("editor", "navigation.projects"),
    "settings": ("editor", "navigation.settings"),
    "recovery": ("projects", "projects.recovery"),
    "operations": ("editor", "navigation.operations"),
}


def scenarios(mode: str) -> list[dict[str, object]]:
    result = [
        {"name": f"route-{surface}", "initial_surface": initial, "surface": surface,
         "actions": [action], "width": 1440, "height": 900}
        for surface, (initial, action) in ROUTE_ACTIONS.items()
    ]
    result.extend(
        {"name": f"workspace-{workspace}", "surface": "editor", "workspace": workspace,
         "initial_workspace": "model", "actions": [f"workspace.{workspace}"],
         "width": 1280, "height": 800}
        for workspace in WORKSPACES
    )
    result.extend(
        {"name": f"state-{state}", "surface": "editor", "state": state,
         "width": 1024, "height": 700}
        for state in STATES
    )
    result.extend(
        {"name": f"narrow-{surface}", "surface": surface, "width": 800, "height": 600}
        for surface in SURFACES
    )
    result.extend(
        {"name": f"settings-{category}", "surface": "settings",
         "settings_category": category, "initial_settings_category": "appearance",
         "actions": [f"settings.category.{category}"], "width": 1280, "height": 800}
        for category in SETTINGS_CATEGORIES
    )
    result.append(
        {"name": "inspector-source", "surface": "editor",
         "actions": ["inspector.tab.source"], "required": ["inspector.source.editor"],
         "width": 1440, "height": 900}
    )
    result.extend((
        {"name": "command-plane", "surface": "editor",
         "actions": ["command.model.plane.create"],
         "required": ["field.model.plane.create.method",
                      "field.model.plane.create.reference",
                      "field.model.plane.create.offset"],
         "width": 1440, "height": 900},
        {"name": "command-new-sketch", "surface": "editor",
         "actions": ["command.model.sketch.create"],
         "required": ["field.model.sketch.create.attachment",
                      "field.model.sketch.create.orientation"],
         "width": 1440, "height": 900},
    ))
    if mode == "full":
        result.extend(
            {"name": f"full-{surface}-{width}", "surface": surface, "width": width,
             "height": round(width * 0.625)}
            for surface in SURFACES for width in (960, 1200, 1920)
        )
        result.extend(
            {"name": f"full-{workspace}-{state}", "surface": "editor",
             "workspace": workspace, "state": state, "width": 1440, "height": 900}
            for workspace in WORKSPACES for state in STATES
        )
    names = [str(item["name"]) for item in result]
    if len(names) != len(set(names)):
        raise RuntimeError("scenario generator produced duplicate names")
    return result


class VirtualDisplay:
    def __init__(self) -> None:
        self.process: subprocess.Popen[str] | None = None
        self.display: str | None = None

    def __enter__(self) -> str | None:
        xvfb = shutil.which("Xvfb") if sys.platform.startswith("linux") else None
        if not xvfb:
            return None
        self.process = subprocess.Popen(
            [xvfb, "-displayfd", "1", "-screen", "0", "2200x1400x24", "-nolisten", "tcp"],
            stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True,
        )
        assert self.process.stdout is not None
        display_number = self.process.stdout.readline().strip()
        if not display_number or self.process.poll() is not None:
            assert self.process.stderr is not None
            raise RuntimeError(f"Xvfb failed to start: {self.process.stderr.read()}")
        self.display = f":{display_number}"
        return self.display

    def __exit__(self, *_: object) -> None:
        if not self.process:
            return
        self.process.terminate()
        try:
            self.process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            self.process.kill()
            self.process.wait(timeout=5)


def launch(executable: Path, scenario: dict[str, object], output: Path,
           display: str | None) -> None:
    width = int(scenario["width"])
    height = int(scenario["height"])
    command = [
        str(executable), "--capture-dir", str(output), "--surface",
        str(scenario.get("initial_surface", scenario["surface"])), "--workspace",
        str(scenario.get("initial_workspace", scenario.get("workspace", "model"))), "--ui-state",
        str(scenario.get("state", "unavailable")), "--inspector-page",
        "properties", "--settings-category",
        str(scenario.get("initial_settings_category",
                         scenario.get("settings_category", "appearance"))), "--width", str(width),
        "--height", str(height),
    ]
    for action in scenario.get("actions", []):
        command.extend(("--ui-action", str(action)))
    environment = os.environ.copy()
    environment.setdefault("QT_QUICK_BACKEND", "software")
    if display:
        environment["DISPLAY"] = display
    elif sys.platform.startswith("linux") and "DISPLAY" not in environment:
        environment["QT_QPA_PLATFORM"] = "offscreen"
    completed = subprocess.run(command, capture_output=True, text=True, env=environment, timeout=20)
    if completed.returncode:
        raise RuntimeError(
            f"application exited {completed.returncode}\nstdout:\n{completed.stdout}\nstderr:\n{completed.stderr}"
        )
    if completed.stderr.strip():
        raise RuntimeError(f"application emitted stderr:\n{completed.stderr}")


def png_pixels(path: Path) -> tuple[int, int, int]:
    data = path.read_bytes()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError("invalid PNG signature")
    position = 8
    compressed = bytearray()
    width = height = bit_depth = color_type = 0
    while position < len(data):
        length = struct.unpack(">I", data[position:position + 4])[0]
        kind = data[position + 4:position + 8]
        payload = data[position + 8:position + 8 + length]
        if kind == b"IHDR":
            width, height, bit_depth, color_type = struct.unpack(">IIBB", payload[:10])
        elif kind == b"IDAT":
            compressed.extend(payload)
        elif kind == b"IEND":
            break
        position += length + 12
    channels = {0: 1, 2: 3, 4: 2, 6: 4}.get(color_type)
    if bit_depth != 8 or channels is None:
        raise ValueError(f"unsupported PNG format depth={bit_depth} color={color_type}")
    raw = zlib.decompress(compressed)
    stride = width * channels
    expected = (stride + 1) * height
    if len(raw) != expected:
        raise ValueError(f"PNG scanline size {len(raw)} does not match {expected}")
    sample_step = max(1, len(raw) // 16_384)
    return width, height, len(set(raw[::sample_step]))


def validate_bounds(node: dict[str, object], image_width: int, image_height: int) -> list[str]:
    failures: list[str] = []
    for key in ("logical_bounds", "physical_bounds", "screen_bounds"):
        bounds = node.get(key)
        if not isinstance(bounds, list) or len(bounds) != 4 or not all(
            isinstance(value, (int, float)) and math.isfinite(value) for value in bounds
        ):
            failures.append(f"{node['id']}: invalid {key}")
    if failures or not node.get("visible"):
        return failures
    x, y, width, height = node["physical_bounds"]
    if width <= 0 or height <= 0:
        failures.append(f"{node['id']}: visible node has non-positive bounds")
    tolerance = 2.0
    if x + width <= -tolerance or y + height <= -tolerance or x >= image_width + tolerance or y >= image_height + tolerance:
        failures.append(f"{node['id']}: visible bounds do not intersect captured image")
    return failures


def validate(output: Path, scenario: dict[str, object]) -> list[str]:
    failures: list[str] = []
    image_path = output / "application-session.png"
    metadata_path = output / "capture.json"
    semantic_path = output / "semantic-ui.json"
    for path in (image_path, metadata_path, semantic_path):
        if not path.is_file():
            return [f"missing {path.name}"]
    if os.name == "posix":
        if stat.S_IMODE(output.stat().st_mode) & 0o077:
            failures.append("capture directory is accessible outside its owner")
        for path in (image_path, metadata_path, semantic_path):
            if stat.S_IMODE(path.stat().st_mode) & 0o077:
                failures.append(f"{path.name} is accessible outside its owner")
    try:
        image_width, image_height, color_count = png_pixels(image_path)
    except (OSError, ValueError, zlib.error) as error:
        return [str(error)]
    if (image_width, image_height) != (int(scenario["width"]), int(scenario["height"])):
        failures.append(f"image size {(image_width, image_height)} does not match scenario")
    if color_count < 8:
        failures.append(f"capture has only {color_count} sampled colors")
    metadata = json.loads(metadata_path.read_text(encoding="utf-8"))
    semantic = json.loads(semantic_path.read_text(encoding="utf-8"))
    if metadata.get("schema") != "kearne.application-session-capture/v1":
        failures.append("capture schema mismatch")
    if semantic.get("schema") != "kearne.semantic-ui/v1":
        failures.append("semantic schema mismatch")
    if metadata.get("target") != "ApplicationSession" or metadata.get("surface_count", 0) < 1:
        failures.append("capture target is not a non-empty application session")
    image = metadata.get("image", {})
    if (image.get("pixel_width"), image.get("pixel_height")) != (image_width, image_height):
        failures.append("capture metadata dimensions mismatch PNG")
    if image.get("sha256") != hashlib.sha256(image_path.read_bytes()).hexdigest():
        failures.append("capture digest mismatch")
    statistics = image.get("statistics", {})
    if statistics.get("sampled_unique_colors", 0) < 8:
        failures.append("capture statistics report fewer than eight colors")
    if statistics.get("sampled_alpha_max", 0) <= 0:
        failures.append("capture statistics report no visible pixels")
    if statistics.get("sampled_luminance_max", 0) - statistics.get("sampled_luminance_min", 0) < 16:
        failures.append("capture statistics report insufficient luminance range")
    if metadata.get("ui_generation") != semantic.get("ui_generation"):
        failures.append("semantic and capture generations differ")
    expected_actions = list(scenario.get("actions", []))
    action_receipts = metadata.get("actions", [])
    semantic_actions = semantic.get("actions", [])
    if action_receipts != semantic_actions:
        failures.append("semantic and capture action receipts differ")
    if [receipt.get("semantic_id") for receipt in action_receipts] != expected_actions:
        failures.append("executed semantic actions differ from scenario")
    for receipt in action_receipts:
        if receipt.get("action") != "invoke":
            failures.append("semantic action receipt has unexpected action")
        if receipt.get("generation_after", -1) < receipt.get("generation_before", 0):
            failures.append("semantic action receipt moved generation backward")
    if semantic.get("surface_id") != scenario["surface"]:
        failures.append("captured surface differs from scenario")
    if semantic.get("workspace_id") != scenario.get("workspace", "model"):
        failures.append("captured workspace differs from scenario")
    nodes = semantic.get("nodes")
    if not isinstance(nodes, list) or not nodes:
        return [*failures, "semantic snapshot has no nodes"]
    ids = [node.get("id") for node in nodes]
    duplicates = sorted(node_id for node_id, count in Counter(ids).items() if count > 1)
    if duplicates:
        failures.append(f"duplicate semantic IDs: {duplicates}")
    id_set = set(ids)
    required = {"window.main", f"surface.{scenario['surface']}"}
    required.update(scenario.get("required", []))
    if scenario.get("surface") == "editor":
        required.update({"project.length_unit", "viewport.grid",
                         "viewport.grid.toggle", "viewport.grid_snap.toggle"})
    if scenario.get("settings_category"):
        required.add(f"settings.{scenario['settings_category']}")
    missing = sorted(required - id_set)
    if missing:
        failures.append(f"missing required semantic nodes: {missing}")
    for node in nodes:
        node_id = node.get("id", "<missing>")
        if not isinstance(node_id, str) or not node_id:
            failures.append("semantic node has no ID")
            continue
        if not isinstance(node.get("name"), str) or not node["name"].strip():
            failures.append(f"{node_id}: empty accessible name")
        if not isinstance(node.get("role"), str) or not node["role"].strip():
            failures.append(f"{node_id}: empty role")
        if not isinstance(node.get("actions"), list):
            failures.append(f"{node_id}: actions are not a list")
        elif node.get("visible") and node.get("enabled") and node.get("role") in ACTIONABLE_ROLES and not node["actions"]:
            failures.append(f"{node_id}: actionable control declares no action")
        parent = node.get("parent_id")
        if parent and parent not in id_set:
            failures.append(f"{node_id}: unknown semantic parent {parent}")
        failures.extend(validate_bounds(node, image_width, image_height))
    return failures


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--executable", required=True, type=Path)
    parser.add_argument("--mode", choices=("smoke", "full"), default="smoke")
    parser.add_argument("--output", type=Path)
    arguments = parser.parse_args()
    executable = arguments.executable.resolve()
    if not executable.is_file():
        parser.error(f"executable does not exist: {executable}")
    temporary = None
    if arguments.output:
        root = arguments.output.resolve()
        root.mkdir(parents=True, exist_ok=True)
    else:
        temporary = tempfile.TemporaryDirectory(prefix="kearne-observation-")
        root = Path(temporary.name)
    all_failures: list[str] = []
    matrix = scenarios(arguments.mode)
    with VirtualDisplay() as display:
        for index, scenario in enumerate(matrix, start=1):
            name = str(scenario["name"])
            output = root / name
            output.mkdir()
            try:
                launch(executable, scenario, output, display)
                failures = validate(output, scenario)
            except (OSError, subprocess.SubprocessError, RuntimeError) as error:
                failures = [str(error)]
            if failures:
                all_failures.extend(f"{name}: {failure}" for failure in failures)
                print(f"[{index:03}/{len(matrix):03}] FAIL {name}")
            else:
                print(f"[{index:03}/{len(matrix):03}] PASS {name}")
    if all_failures:
        print("\n".join(all_failures), file=sys.stderr)
        if temporary:
            print(f"artifacts retained only for this process: {root}", file=sys.stderr)
        return 1
    print(f"verified {len(matrix)} generated observation scenarios")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
