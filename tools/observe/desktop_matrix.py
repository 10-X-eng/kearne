#!/usr/bin/env python3
"""Generate and verify Kearne desktop observation scenarios."""

from __future__ import annotations

import argparse
from collections import Counter
from concurrent.futures import ThreadPoolExecutor, as_completed
from fnmatch import fnmatchcase
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
ACCESSIBLE_ROLES = {
    "button": "Button",
    "canvas": "Canvas",
    "combobox": "ComboBox",
    "listitem": "ListItem",
    "row": "ListItem",
    "searchbox": "EditableText",
    "splitter": "Splitter",
    "status": "Button",
    "switch": "Switch",
    "tab": "PageTab",
    "textbox": "EditableText",
}
ROUTE_ACTIONS = {
    "editor": ("projects", "navigation.editor"),
    "projects": ("editor", "navigation.projects"),
    "settings": ("editor", "navigation.settings"),
    "recovery": ("projects", "projects.recovery"),
    "operations": ("editor", "navigation.operations"),
}
PROJECT_ROUTES = {
    "projects.new": "model",
    "project.motor-bracket": "model",
    "template.part.create": "model",
    "template.assembly.create": "assemble",
    "template.drawing.create": "drawing",
    "template.sheet-metal.create": "sheet-metal",
    "template.cam.create": "cam",
}


def protocol_schemas() -> tuple[str, str]:
    path = Path(__file__).with_name("observation.schema.json")
    document = json.loads(path.read_text(encoding="utf-8"))
    definitions = document.get("$defs", {})
    try:
        semantic = definitions["semanticUi"]["properties"]["schema"]["const"]
        capture = definitions["applicationSessionCapture"]["properties"]["schema"]["const"]
    except (KeyError, TypeError) as error:
        raise RuntimeError(f"invalid observation protocol schema: {path}") from error
    if not isinstance(semantic, str) or not isinstance(capture, str):
        raise RuntimeError(f"invalid observation protocol schema identifiers: {path}")
    return semantic, capture


def operations_for(scenario: dict[str, object]) -> list[dict[str, object]]:
    operations = [
        {"action": "invoke", "semantic_id": semantic_id}
        for semantic_id in scenario.get("actions", [])
    ]
    operations.extend(scenario.get("operations", []))
    return operations


def profile_for(output: Path, scenario: dict[str, object]) -> Path:
    return output.parent / "profiles" / str(scenario.get("profile", scenario["name"]))


def scenarios(mode: str) -> list[dict[str, object]]:
    if mode == "proof":
        create = [
            {"action": "invoke", "semantic_id": "command.model.sketch.create"},
            {"action": "choose", "semantic_id": "viewport.datum_planes",
             "value": "reference.plane.xy"},
        ]
        proofs = [
            {"name": "proof-sketch-entry", "surface": "editor",
             "workspace": "sketch", "initial_workspace": "sketch",
             "design_engine": True,
             "required": ["command.model.sketch.create"],
             "expected_absent": ["command.sketch.rectangle",
                                 "sketch.solve.state"],
             "expected_values": {"viewport.state": "current"},
             "width": 1440, "height": 900},
            {"name": "proof-sketch-plane-preselection", "surface": "editor",
             "workspace": "sketch", "initial_workspace": "model",
             "design_engine": True,
             "operations": [
                 {"action": "select",
                  "semantic_id": "entity.reference.plane.xz"},
                 {"action": "invoke",
                  "semantic_id": "command.model.sketch.create"},
             ],
             "required": ["sketch.solve.state"],
             "expected_values": {"viewport.grid": "XZ:10",
                                 "sketch.solve.state": "solved:0",
                                 "viewport.state": "current"},
             "width": 1440, "height": 900},
            {"name": "proof-sketch-empty", "surface": "editor",
             "workspace": "sketch", "initial_workspace": "model",
             "design_engine": True,
             "operations": create,
             "required": ["sketch.solve.state"],
             "expected_values": {"sketch.solve.state": "solved:0",
                                 "viewport.state": "current"},
             "width": 1440, "height": 900},
            {"name": "proof-sketch-no-cursor-substitute", "surface": "editor",
             "workspace": "sketch", "initial_workspace": "model",
             "design_engine": True,
             "operations": [
                 *create,
                 {"action": "invoke",
                  "semantic_id": "command.sketch.rectangle"},
                 {"action": "pointerHover", "semantic_id": "viewport.primary",
                  "value": {"from": [0.32, 0.32], "to": [0.40, 0.40],
                            "capture_preview": True}},
             ],
             "required": ["sketch.solve.state"],
             "expected_values": {"sketch.solve.state": "solved:0",
                                 "viewport.state": "current"},
             "expected_preview_measurements": 0,
             "action_evidence": [
                 {"semantic_id": "viewport.primary", "action": "pointerHover",
                  "forbid_pointer_move_preview": True,
                  "forbid_preview_frame": True,
                  "require_preview_image": True,
                  "require_native_scene": True},
             ],
             "width": 1440, "height": 900},
            {"name": "proof-sketch-rectangle", "surface": "editor",
             "workspace": "sketch", "initial_workspace": "model",
             "design_engine": True,
             "operations": [
                 *create,
                 {"action": "invoke",
                  "semantic_id": "command.sketch.rectangle"},
                 {"action": "pointerDrag", "semantic_id": "viewport.primary",
                  "value": {"button": "left", "from": [0.42, 0.43],
                            "to": [0.58, 0.57],
                            "capture_preview": True}},
             ],
             "required": ["sketch.solve.state"],
             "expected_values": {
                 "sketch.solve.state": "underconstrained:4",
                 "viewport.state": "current",
             },
             "action_evidence": [
                 {"semantic_id": "viewport.datum_planes", "action": "choose",
                  "state_after_dispatch": "pending",
                  "settled_state": "none", "max_first_presented_ms": 100,
                  "max_settled_ms": 500, "require_native_scene": True},
                 {"semantic_id": "viewport.primary", "action": "pointerDrag",
                  "state_after_input": "pending", "settled_state": "none",
                  "max_first_presented_ms": 100,
                  "max_current_scene_after_input_ms": 250,
                  "max_pointer_move_p95_ms": 16.7,
                  "require_pointer_move_preview": True,
                  "require_preview_frame": True,
                  "require_preview_image": True,
                  "require_native_scene": True},
             ],
             "expected_preview_measurements": 2,
             "width": 1440, "height": 900},
            {"name": "proof-sketch-point-selection", "surface": "editor",
             "workspace": "sketch", "initial_workspace": "model",
             "design_engine": True,
             "operations": [
                 *create,
                 {"action": "invoke",
                  "semantic_id": "command.sketch.rectangle"},
                 {"action": "pointerDrag", "semantic_id": "viewport.primary",
                  "value": {"button": "left", "from": [0.42, 0.43],
                            "to": [0.58, 0.57]}},
                 {"action": "pointerClick", "semantic_id": "viewport.primary",
                  "value": {"button": "left", "position": [0.428, 0.418]}},
             ],
             "required": ["sketch.solve.state", "input.selection.type",
                          "input.selection.x", "input.selection.y"],
             "expected_values": {
                 "sketch.solve.state": "underconstrained:4",
                 "input.selection.type": "Point",
                 "viewport.state": "current",
             },
             "expected_sketch_selection": "point",
             "revision_evidence": [{
                 "semantic_id": "viewport.primary", "action": "pointerClick",
                 "occurrence": 1,
                 "reference_semantic_id": "viewport.primary",
                 "reference_action": "pointerDrag", "reference_occurrence": 1,
                 "relation": "same",
             }],
             "action_evidence": [{
                 "semantic_id": "viewport.primary", "action": "pointerClick",
                 "max_current_scene_ms": 250,
                 "require_native_scene": True,
             }],
             "width": 1440, "height": 900},
            {"name": "proof-sketch-rectangle-warm", "surface": "editor",
             "workspace": "sketch", "initial_workspace": "model",
             "design_engine": True,
             "operations": [
                 *create,
                 {"action": "invoke",
                  "semantic_id": "command.sketch.rectangle"},
                 {"action": "pointerDrag", "semantic_id": "viewport.primary",
                  "value": {"button": "left", "from": [0.34, 0.43],
                            "to": [0.46, 0.57]}},
                 {"action": "invoke",
                  "semantic_id": "command.sketch.rectangle"},
                 {"action": "pointerDrag", "semantic_id": "viewport.primary",
                  "value": {"button": "left", "from": [0.56, 0.43],
                            "to": [0.68, 0.57]}},
             ],
             "required": ["sketch.solve.state",
                          "entity.sketch.rectangle.2"],
             "expected_values": {
                 "sketch.solve.state": "underconstrained:8",
                 "viewport.state": "current",
             },
             "action_evidence": [
                 {"semantic_id": "viewport.primary", "action": "pointerDrag",
                  "occurrence": 1, "state_after_input": "pending",
                  "settled_state": "none",
                  "max_current_scene_after_input_ms": 250,
                  "max_pointer_move_p95_ms": 16.7,
                  "require_pointer_move_preview": True,
                  "require_preview_frame": True,
                  "require_native_scene": True},
                 {"semantic_id": "viewport.primary", "action": "pointerDrag",
                  "occurrence": 2, "state_after_input": "pending",
                  "settled_state": "none",
                  "max_current_scene_after_input_ms": 100,
                  "max_pointer_move_p95_ms": 16.7,
                  "require_pointer_move_preview": True,
                  "require_preview_frame": True,
                  "require_native_scene": True},
             ],
             "width": 1440, "height": 900},
            {"name": "proof-sketch-edge-resize", "surface": "editor",
             "workspace": "sketch", "initial_workspace": "model",
             "design_engine": True,
             "operations": [
                 *create,
                 {"action": "invoke",
                  "semantic_id": "command.sketch.rectangle"},
                 {"action": "pointerDrag", "semantic_id": "viewport.primary",
                  "value": {"button": "left", "from": [0.42, 0.43],
                            "to": [0.58, 0.57]}},
                 {"action": "pointerHover", "semantic_id": "viewport.primary",
                  "value": {"from": [0.30, 0.30], "to": [0.50, 0.42],
                            "capture_preview": True}},
                 {"action": "pointerDrag", "semantic_id": "viewport.primary",
                  "value": {"button": "left", "from": [0.50, 0.42],
                            "to": [0.50, 0.32],
                            "capture_preview": True}},
             ],
             "required": ["sketch.solve.state",
                          "entity.function.sketch",
                          "entity.sketch.rectangle.1"],
             "expected_values": {
                 "sketch.solve.state": "underconstrained:4",
                 "viewport.state": "current",
             },
             "action_evidence": [
                 {"semantic_id": "viewport.primary", "action": "pointerHover",
                  "max_pointer_move_p95_ms": 50,
                  "require_pointer_move_hover": True,
                  "require_preview_image": True,
                  "require_native_scene": True},
                 {"semantic_id": "viewport.primary", "action": "pointerDrag",
                  "occurrence": 2, "max_pointer_move_p95_ms": 50,
                  "require_pointer_move_hover": True,
                  "require_preview_image": True,
                  "require_native_scene": True},
             ],
             "width": 1440, "height": 900},
            {"name": "proof-sketch-adaptive-grid", "surface": "editor",
             "workspace": "sketch", "initial_workspace": "model",
             "design_engine": True,
             "operations": [
                 *create,
                 {"action": "zoom", "semantic_id": "viewport.primary",
                  "value": 12},
             ],
             "required": ["viewport.grid", "sketch.solve.state"],
             "expected_values": {"viewport.grid": "XY:2",
                                 "sketch.solve.state": "solved:0"},
             "width": 1440, "height": 900},
            {"name": "proof-sketch-construction-toggle", "surface": "editor",
             "workspace": "sketch", "initial_workspace": "model",
             "design_engine": True,
             "operations": [
                 *create,
                 {"action": "invoke",
                  "semantic_id": "command.sketch.rectangle"},
                 {"action": "pointerDrag", "semantic_id": "viewport.primary",
                  "value": {"button": "left", "from": [0.42, 0.43],
                            "to": [0.58, 0.57]}},
                 {"action": "pointerClick", "semantic_id": "viewport.primary",
                  "value": {"button": "left", "position": [0.50, 0.42]}},
                 {"action": "toggleConstruction",
                  "semantic_id": "viewport.primary"},
             ],
             "required": ["sketch.solve.state", "entity.function.sketch"],
             "expected_absent": ["entity.sketch.rectangle.1"],
             "expected_values": {
                 "sketch.solve.state": "underconstrained:4",
                 "viewport.state": "current",
             },
             "width": 1440, "height": 900},
            {"name": "proof-sketch-cancel", "surface": "editor",
             "workspace": "sketch", "initial_workspace": "model",
             "design_engine": True,
             "operations": [
                 {"action": "invoke",
                  "semantic_id": "command.model.sketch.create"},
                 {"action": "invoke", "semantic_id": "inspector.cancel"},
             ],
             "revision_evidence": [
                 {"semantic_id": "inspector.cancel", "action": "invoke",
                  "reference_semantic_id": "command.model.sketch.create",
                  "reference_action": "invoke", "relation": "same"},
             ],
             "expected_absent": ["command.sketch.rectangle",
                                 "sketch.solve.state"],
             "expected_values": {"viewport.state": "current"},
             "width": 1440, "height": 900},
            {"name": "proof-sketch-invalid-retry", "surface": "editor",
             "workspace": "sketch", "initial_workspace": "model",
             "design_engine": True,
             "operations": [
                 *create,
                 {"action": "invoke",
                  "semantic_id": "command.sketch.rectangle"},
                 {"action": "pointerClick", "semantic_id": "viewport.primary",
                  "value": {"button": "left", "position": [0.50, 0.50]}},
                 {"action": "pointerClick", "semantic_id": "viewport.primary",
                  "value": {"button": "left", "position": [0.50, 0.50]}},
                 {"action": "pointerDrag", "semantic_id": "viewport.primary",
                  "value": {"button": "left", "from": [0.42, 0.43],
                            "to": [0.58, 0.57]}},
             ],
             "action_evidence": [
                 {"semantic_id": "viewport.primary", "action": "pointerClick",
                  "occurrence": 2, "settled_state": "editing",
                  "require_native_scene": True},
                 {"semantic_id": "viewport.primary", "action": "pointerDrag",
                  "state_after_input": "pending", "settled_state": "none",
                  "max_current_scene_after_input_ms": 250,
                  "max_pointer_move_p95_ms": 16.7,
                  "require_pointer_move_preview": True,
                  "require_preview_frame": True,
                  "require_native_scene": True},
             ],
             "revision_evidence": [
                 {"semantic_id": "viewport.primary", "action": "pointerClick",
                  "occurrence": 2,
                  "reference_semantic_id": "command.sketch.rectangle",
                  "reference_action": "invoke", "relation": "same"},
                 {"semantic_id": "viewport.primary", "action": "pointerDrag",
                  "reference_semantic_id": "command.sketch.rectangle",
                  "reference_action": "invoke", "relation": "different"},
             ],
             "required": ["sketch.solve.state",
                          "entity.sketch.rectangle.1"],
             "expected_values": {
                 "sketch.solve.state": "underconstrained:4",
                 "viewport.state": "current",
             },
             "width": 1440, "height": 900},
            {"name": "proof-sketch-source-history", "surface": "editor",
             "workspace": "sketch", "initial_workspace": "model",
             "design_engine": True,
             "operations": [
                 *create,
                 {"action": "invoke",
                  "semantic_id": "command.sketch.rectangle"},
                 {"action": "pointerDrag", "semantic_id": "viewport.primary",
                  "value": {"button": "left", "from": [0.42, 0.43],
                            "to": [0.58, 0.57]}},
                 {"action": "invoke",
                  "semantic_id": "inspector.tab.source"},
                 {"action": "prependText",
                  "semantic_id": "inspector.source.editor",
                  "value": "# edited through Kearne UI\n"},
                 {"action": "invoke", "semantic_id": "source.diff"},
                 {"action": "invoke", "semantic_id": "source_diff.apply"},
                 {"action": "invoke",
                  "semantic_id": "structure.tab.history"},
                 {"action": "invoke",
                  "semantic_id": "history.command.version.undo"},
                 {"action": "invoke",
                  "semantic_id": "history.command.version.redo"},
             ],
             "revision_evidence": [
                 {"semantic_id": "source_diff.apply", "action": "invoke",
                  "reference_semantic_id": "viewport.primary",
                  "reference_action": "pointerDrag", "relation": "different"},
                 {"semantic_id": "history.command.version.undo",
                  "action": "invoke",
                  "reference_semantic_id": "viewport.primary",
                  "reference_action": "pointerDrag", "relation": "same"},
                 {"semantic_id": "history.command.version.redo",
                  "action": "invoke",
                  "reference_semantic_id": "source_diff.apply",
                  "reference_action": "invoke", "relation": "same"},
             ],
             "required": ["sketch.solve.state", "source.draft.state",
                          "history.command.version.undo",
                          "history.command.version.redo"],
             "expected_values": {
                 "sketch.solve.state": "underconstrained:4",
                 "source.draft.state": "current",
                 "viewport.state": "current",
             },
             "expected_source_contains": "# edited through Kearne UI",
             "width": 1440, "height": 900},
        ]
        def invoke(semantic_id: str) -> dict[str, object]:
            return {"action": "invoke", "semantic_id": semantic_id}

        def drag(start: list[float], finish: list[float],
                 capture: bool = False) -> dict[str, object]:
            return {
                "action": "pointerDrag", "semantic_id": "viewport.primary",
                "value": {"button": "left", "from": start, "to": finish,
                          "capture_preview": capture},
            }

        def hover(start: list[float], finish: list[float],
                  capture: bool = False) -> dict[str, object]:
            return {
                "action": "pointerHover", "semantic_id": "viewport.primary",
                "value": {"from": start, "to": finish,
                          "capture_preview": capture},
            }

        def click(position: list[float]) -> dict[str, object]:
            return {
                "action": "pointerClick", "semantic_id": "viewport.primary",
                "value": {"button": "left", "position": position},
            }

        def set_value(semantic_id: str, value: str) -> dict[str, object]:
            return {
                "action": "setValue", "semantic_id": semantic_id,
                "value": value,
            }

        line = [
            invoke("command.sketch.line"),
            drag([0.40, 0.55], [0.60, 0.43]),
        ]
        dimension = [
            click([0.50, 0.49]),
            invoke("command.sketch.dimension"),
            {"action": "choose",
             "semantic_id": "input.sketch.dimension.kind",
             "value": "distance"},
            set_value("input.sketch.dimension.expression", "40 mm"),
            invoke("inspector.apply"),
        ]

        proofs.extend([
            {
                "name": "proof-sketch-constraint-lifecycle",
                "surface": "editor", "workspace": "sketch",
                "initial_workspace": "model", "design_engine": True,
                "operations": [
                    *create, *line,
                    click([0.50, 0.49]),
                    invoke("command.sketch.horizontal"),
                    {"action": "select",
                     "semantic_id": "entity.sketch.constraints"},
                    {"action": "toggle",
                     "semantic_id":
                         "input.sketch.constraint-display.constraints",
                     "value": False},
                    {"action": "toggle",
                     "semantic_id":
                         "input.sketch.constraint-display.constraints",
                     "value": True},
                    {"action": "select",
                     "semantic_id": "entity.sketch.constraint.1"},
                    set_value("input.constraint.label", "Base alignment"),
                    {"action": "toggle",
                     "semantic_id": "input.constraint.active",
                     "value": False},
                    {"action": "toggle",
                     "semantic_id": "input.constraint.active",
                     "value": True},
                    invoke("constraint.delete"),
                    invoke("structure.tab.history"),
                    invoke("history.command.version.undo"),
                    invoke("history.command.version.redo"),
                    invoke("history.command.version.undo"),
                    invoke("structure.tab.entities"),
                    {"action": "select",
                     "semantic_id": "entity.sketch.constraint.1"},
                ],
                "required": [
                    "entity.sketch.constraint.1",
                    "input.constraint.label",
                    "input.constraint.active",
                    "constraint.delete",
                    "history.command.version.redo",
                    "sketch.solve.state",
                ],
                "expected_values": {
                    "input.constraint.label": "Base alignment",
                    "input.constraint.active": True,
                    "input.selection.state": "Driving",
                    "viewport.state": "current",
                },
                "width": 1440, "height": 900,
            },
            {
                "name": "proof-sketch-dimension-lifecycle",
                "surface": "editor", "workspace": "sketch",
                "initial_workspace": "model", "design_engine": True,
                "operations": [
                    *create, *line, *dimension,
                    {"action": "select",
                     "semantic_id": "entity.sketch.constraint.1"},
                    set_value("input.constraint.value", "50 mm"),
                    {"action": "choose",
                     "semantic_id": "input.constraint.mode",
                     "value": "reference"},
                ],
                "required": [
                    "entity.sketch.constraint.1",
                    "input.constraint.mode",
                    "input.constraint.value",
                    "sketch.solve.state",
                ],
                "expected_values": {
                    "input.constraint.mode": "reference",
                    "input.constraint.value": "50 mm",
                    "input.selection.state": "Reference",
                    "viewport.state": "current",
                },
                "width": 1440, "height": 900,
            },
            {
                "name": "proof-sketch-dimension-canvas-selection",
                "surface": "editor", "workspace": "sketch",
                "initial_workspace": "model", "design_engine": True,
                "operations": [
                    *create, *line, *dimension,
                    click([0.48, 0.46]),
                ],
                "required": [
                    "entity.sketch.constraint.1",
                    "input.selection.type",
                    "input.selection.geometry",
                    "input.constraint.value",
                ],
                "expected_values": {
                    "input.selection.type": "Distance",
                    "input.selection.state": "Driving",
                    "input.selection.geometry": "Line 1",
                    "input.constraint.value": "40 mm",
                    "viewport.state": "current",
                },
                "action_evidence": [{
                    "semantic_id": "viewport.primary",
                    "action": "pointerClick",
                    "occurrence": 2,
                    "state_after_dispatch": "none",
                    "settled_state": "none",
                    "max_first_presented_ms": 100,
                    "max_current_scene_ms": 250,
                    "max_settled_ms": 350,
                    "require_native_scene": True,
                }],
                "width": 1440, "height": 900,
            },
            {
                "name": "proof-sketch-constraint-conflict-recovery",
                "surface": "editor", "workspace": "sketch",
                "initial_workspace": "model", "design_engine": True,
                "operations": [
                    *create, *line, *dimension,
                    click([0.50, 0.49]),
                    invoke("command.sketch.dimension"),
                    {"action": "choose",
                     "semantic_id": "input.sketch.dimension.kind",
                     "value": "distance"},
                    set_value("input.sketch.dimension.expression", "50 mm"),
                    invoke("inspector.apply"),
                    {"action": "select",
                     "semantic_id": "entity.sketch.constraint.2"},
                    {"action": "toggle",
                     "semantic_id": "input.constraint.active",
                     "value": False},
                    {"action": "toggle",
                     "semantic_id": "input.constraint.active",
                     "value": True},
                    invoke("constraint.delete"),
                    invoke("structure.tab.history"),
                    invoke("history.command.version.undo"),
                    invoke("structure.tab.entities"),
                    {"action": "select",
                     "semantic_id": "entity.sketch.constraint.2"},
                ],
                "required": [
                    "entity.sketch.constraint.2",
                    "input.selection.conflicts",
                    "input.constraint.active",
                    "constraint.delete",
                    "sketch.solve.state",
                ],
                "expected_values": {
                    "input.selection.state": "Conflict",
                    "input.constraint.active": True,
                    "viewport.state": "current",
                },
                "width": 1440, "height": 900,
            },
            {
                "name": "proof-sketch-constraint-dense",
                "surface": "editor", "workspace": "sketch",
                "initial_workspace": "model", "design_engine": True,
                "operations": [
                    *create,
                    invoke("command.sketch.rectangle"),
                    drag([0.34, 0.38], [0.46, 0.48]),
                    invoke("command.sketch.rectangle"),
                    drag([0.54, 0.38], [0.66, 0.48]),
                    invoke("command.sketch.rectangle"),
                    drag([0.34, 0.55], [0.46, 0.65]),
                    invoke("command.sketch.rectangle"),
                    drag([0.54, 0.55], [0.66, 0.65]),
                ],
                "required": [
                    "entity.sketch.rectangle.4", "sketch.solve.state",
                ],
                "expected_values": {
                    "sketch.solve.state": "underconstrained:16",
                    "viewport.state": "current",
                },
                "width": 1440, "height": 900,
            },
            {
                "name": "proof-sketch-refraction",
                "surface": "editor", "workspace": "sketch",
                "initial_workspace": "model", "design_engine": True,
                "operations": [
                    *create,
                    invoke("command.sketch.line"),
                    drag([0.36, 0.60], [0.48, 0.50]),
                    invoke("command.sketch.line"),
                    drag([0.52, 0.50], [0.64, 0.40]),
                    invoke("command.sketch.line"),
                    drag([0.34, 0.50], [0.66, 0.50]),
                    invoke("command-menu.sketch.relate"),
                    invoke("command.sketch.snell"),
                    click([0.36, 0.60]),
                    click([0.64, 0.40]),
                    click([0.50, 0.50]),
                    {"action": "select",
                     "semantic_id": "entity.sketch.constraint.1"},
                ],
                "required": [
                    "entity.sketch.constraint.1",
                    "input.constraint.value",
                    "sketch.solve.state",
                ],
                "expected_values": {
                    "input.selection.type": "Refraction",
                    "input.constraint.value": "1.5",
                    "viewport.state": "current",
                },
                "width": 1440, "height": 900,
            },
        ])

        bspline = [
            invoke("command.sketch.bspline.control-points"),
            drag([0.37, 0.58], [0.45, 0.40]),
            click([0.55, 0.40]),
            click([0.63, 0.58]),
            invoke("inspector.apply"),
        ]

        def edit_bspline(command: str,
                         fields: list[dict[str, object]] | None = None
                         ) -> list[dict[str, object]]:
            return [
                {"action": "select",
                 "semantic_id": "entity.sketch.bspline.1"},
                invoke("command-menu.sketch.spline-edit"),
                invoke(f"command.{command}"),
                *(fields or []),
                hover([0.45, 0.50], [0.50, 0.44], True),
                click([0.50, 0.44]),
                invoke("inspector.apply"),
            ]

        bspline_edits = [
            ("sketch.bspline.increase-degree", []),
            ("sketch.bspline.decrease-degree", [
                set_value(
                    "input.sketch.bspline.decrease-degree.maximum-deviation",
                    "0.001 mm"),
            ]),
            ("sketch.bspline.insert-knot", [
                set_value("input.sketch.bspline.insert-knot.parameter", "0.5"),
            ]),
            ("sketch.bspline.increase-knot-multiplicity", [
                set_value(
                    "input.sketch.bspline.increase-knot-multiplicity.knot",
                    "2"),
            ]),
            ("sketch.bspline.decrease-knot-multiplicity", [
                set_value(
                    "input.sketch.bspline.decrease-knot-multiplicity.knot",
                    "2"),
                set_value(
                    "input.sketch.bspline.decrease-knot-multiplicity."
                    "maximum-deviation",
                    "0.001 mm"),
            ]),
            ("sketch.bspline.pole-weight", [
                set_value("input.sketch.bspline.pole-weight.pole", "2"),
                set_value("input.sketch.bspline.pole-weight.weight", "1.5"),
            ]),
        ]

        def preview_scenario(name: str, operations: list[dict[str, object]],
                             measurement_count: int,
                             action: str = "pointerHover",
                             occurrence: int = 1) -> dict[str, object]:
            evidence = {
                "semantic_id": "viewport.primary", "action": action,
                "require_preview_frame": True,
                "require_preview_image": True,
                "require_native_scene": True,
            }
            if occurrence > 1:
                evidence["occurrence"] = occurrence
            if action == "pointerDrag":
                evidence.update({
                    "max_pointer_move_p95_ms": 30.0,
                    "require_pointer_move_preview": True,
                })
            else:
                evidence.update({
                    "max_pointer_move_p95_ms": 30.0,
                    "require_pointer_move_preview": True,
                    "settled_state": "editing",
                })
            return {
                "name": f"proof-sketch-preview-{name}",
                "surface": "editor", "workspace": "sketch",
                "initial_workspace": "model", "design_engine": True,
                "operations": [*create, *operations],
                "required": ["sketch.solve.state"],
                "expected_values": {"viewport.state": "current"},
                "expected_preview_measurements": measurement_count,
                "action_evidence": [evidence],
                "width": 1440, "height": 900,
            }

        proofs.extend([
            preview_scenario(
                "line",
                [invoke("command.sketch.line"),
                 drag([0.38, 0.56], [0.62, 0.42], True)],
                1,
                "pointerDrag"),
            preview_scenario(
                "polygon",
                [invoke("command-menu.sketch.shapes"),
                 invoke("command.sketch.polygon"),
                 drag([0.48, 0.52], [0.61, 0.43], True)],
                2,
                "pointerDrag"),
            preview_scenario(
                "circle",
                [invoke("command.sketch.circle"),
                 drag([0.48, 0.52], [0.62, 0.42], True)],
                1,
                "pointerDrag"),
            preview_scenario(
                "arc",
                [invoke("command-menu.sketch.circles"),
                 invoke("command.sketch.arc"),
                 drag([0.38, 0.55], [0.62, 0.55]),
                 hover([0.62, 0.55], [0.50, 0.38], True)],
                2),
            preview_scenario(
                "ellipse",
                [invoke("command.sketch.ellipse"),
                 drag([0.45, 0.52], [0.62, 0.52]),
                 hover([0.62, 0.52], [0.45, 0.38], True)],
                2),
            preview_scenario(
                "elliptical-arc",
                [invoke("command-menu.sketch.conics"),
                 invoke("command.sketch.elliptical-arc"),
                 drag([0.45, 0.52], [0.62, 0.52]),
                 hover([0.62, 0.52], [0.45, 0.40]),
                 click([0.45, 0.40]),
                 hover([0.45, 0.40], [0.58, 0.45]),
                 click([0.58, 0.45]),
                 hover([0.58, 0.45], [0.45, 0.64], True)],
                4,
                occurrence=3),
            preview_scenario(
                "hyperbolic-arc",
                [invoke("command-menu.sketch.conics"),
                 invoke("command.sketch.hyperbolic-arc"),
                 drag([0.40, 0.52], [0.48, 0.52]),
                 hover([0.48, 0.52], [0.58, 0.42]),
                 click([0.58, 0.42]),
                 hover([0.58, 0.42], [0.60, 0.62], True)],
                2,
                occurrence=2),
            preview_scenario(
                "parabolic-arc",
                [invoke("command-menu.sketch.conics"),
                 invoke("command.sketch.parabolic-arc"),
                 drag([0.48, 0.52], [0.40, 0.52]),
                 hover([0.40, 0.52], [0.50, 0.42]),
                 click([0.50, 0.42]),
                 hover([0.50, 0.42], [0.53, 0.62], True)],
                1,
                occurrence=2),
            preview_scenario(
                "slot",
                [invoke("command.sketch.slot"),
                 drag([0.38, 0.52], [0.58, 0.52]),
                 hover([0.58, 0.52], [0.48, 0.42], True)],
                2),
            preview_scenario(
                "arc-slot",
                [invoke("command-menu.sketch.slots"),
                 invoke("command.sketch.arc-slot"),
                 drag([0.45, 0.55], [0.58, 0.55]),
                 hover([0.58, 0.55], [0.45, 0.42]),
                 click([0.45, 0.42]),
                 hover([0.45, 0.42], [0.62, 0.55], True)],
                3,
                occurrence=2),
            preview_scenario(
                "bspline",
                [invoke("command.sketch.bspline.control-points"),
                 drag([0.37, 0.58], [0.48, 0.40]),
                 hover([0.48, 0.40], [0.61, 0.56], True)],
                1),
            {
                "name": "proof-sketch-bspline-create",
                "surface": "editor", "workspace": "sketch",
                "initial_workspace": "model", "design_engine": True,
                "operations": [
                    *create, *bspline,
                    invoke("inspector.tab.source"),
                ],
                "required": ["sketch.solve.state",
                             "entity.sketch.bspline.1",
                             "inspector.source.editor"],
                "expected_values": {"viewport.state": "current"},
                "expected_source_contains": "bspline(",
                "action_evidence": [{
                    "semantic_id": "inspector.apply", "action": "invoke",
                    "state_after_dispatch": "pending",
                    "settled_state": "none",
                    "max_first_presented_ms": 100,
                    "max_current_scene_ms": 350,
                    "max_settled_ms": 450,
                    "require_native_scene": True,
                }],
                "revision_evidence": [{
                    "semantic_id": "inspector.apply", "action": "invoke",
                    "reference_semantic_id":
                        "command.sketch.bspline.control-points",
                    "reference_action": "invoke", "relation": "different",
                }],
                "width": 1440, "height": 900,
            },
            {
                "name": "proof-sketch-bspline-control-polygon",
                "surface": "editor", "workspace": "sketch",
                "initial_workspace": "model", "design_engine": True,
                "operations": [
                    *create, *bspline,
                    {"action": "select",
                     "semantic_id": "entity.sketch.bspline.1"},
                    invoke("command-menu.sketch.spline-edit"),
                    invoke("command.sketch.bspline.control-polygon"),
                ],
                "required": [
                    "sketch.solve.state",
                    "entity.sketch.bspline.1",
                    "command.sketch.bspline.control-polygon",
                    "input.selection.degree",
                ],
                "expected_values": {
                    "viewport.state": "current",
                    "command.sketch.bspline.control-polygon": "selected",
                    "input.selection.degree": "3",
                },
                "action_evidence": [{
                    "semantic_id":
                        "command.sketch.bspline.control-polygon",
                    "action": "invoke",
                    "state_after_dispatch": "none",
                    "settled_state": "none",
                    "max_first_presented_ms": 100,
                    "max_current_scene_ms": 250,
                    "max_settled_ms": 350,
                    "require_native_scene": True,
                }],
                "revision_evidence": [{
                    "semantic_id":
                        "command.sketch.bspline.control-polygon",
                    "action": "invoke",
                    "reference_semantic_id": "inspector.apply",
                    "reference_action": "invoke",
                    "relation": "same",
                }],
                "width": 1440, "height": 900,
            },
            {
                "name": "proof-sketch-bspline-curvature-comb",
                "surface": "editor", "workspace": "sketch",
                "initial_workspace": "model", "design_engine": True,
                "operations": [
                    *create, *bspline,
                    {"action": "select",
                     "semantic_id": "entity.sketch.bspline.1"},
                    invoke("command-menu.sketch.spline-edit"),
                    invoke("command.sketch.bspline.curvature-comb"),
                ],
                "required": [
                    "sketch.solve.state",
                    "entity.sketch.bspline.1",
                    "command.sketch.bspline.curvature-comb",
                    "input.selection.degree",
                ],
                "expected_values": {
                    "viewport.state": "current",
                    "command.sketch.bspline.curvature-comb": "selected",
                    "input.selection.degree": "3",
                },
                "action_evidence": [{
                    "semantic_id":
                        "command.sketch.bspline.curvature-comb",
                    "action": "invoke",
                    "state_after_dispatch": "none",
                    "settled_state": "none",
                    "max_first_presented_ms": 100,
                    "max_current_scene_ms": 250,
                    "max_settled_ms": 350,
                    "require_native_scene": True,
                }],
                "revision_evidence": [{
                    "semantic_id":
                        "command.sketch.bspline.curvature-comb",
                    "action": "invoke",
                    "reference_semantic_id": "inspector.apply",
                    "reference_action": "invoke",
                    "relation": "same",
                }],
                "width": 1440, "height": 900,
            },
            {
                "name": "proof-sketch-bspline-labels",
                "surface": "editor", "workspace": "sketch",
                "initial_workspace": "model", "design_engine": True,
                "operations": [
                    *create, *bspline,
                    {"action": "select",
                     "semantic_id": "entity.sketch.bspline.1"},
                    invoke("command-menu.sketch.spline-edit"),
                    invoke("command.sketch.bspline.control-polygon"),
                    invoke("command-menu.sketch.spline-edit"),
                    invoke("command.sketch.bspline.degree-labels"),
                    invoke("command-menu.sketch.spline-edit"),
                    invoke("command.sketch.bspline.knot-labels"),
                    invoke("command-menu.sketch.spline-edit"),
                    invoke("command.sketch.bspline.weight-labels"),
                ],
                "required": [
                    "entity.sketch.bspline.1",
                    "command.sketch.bspline.control-polygon",
                    "command.sketch.bspline.degree-labels",
                    "command.sketch.bspline.knot-labels",
                    "command.sketch.bspline.weight-labels",
                ],
                "expected_values": {
                    "viewport.state": "current",
                    "command.sketch.bspline.control-polygon": "selected",
                    "command.sketch.bspline.degree-labels": "selected",
                    "command.sketch.bspline.knot-labels": "selected",
                    "command.sketch.bspline.weight-labels": "selected",
                },
                "action_evidence": [
                    {
                        "semantic_id": command,
                        "action": "invoke",
                        "state_after_dispatch": "none",
                        "settled_state": "none",
                        "max_first_presented_ms": 100,
                        "max_current_scene_ms": 250,
                        "max_settled_ms": 350,
                        "require_native_scene": True,
                    }
                    for command in (
                        "command.sketch.bspline.degree-labels",
                        "command.sketch.bspline.knot-labels",
                        "command.sketch.bspline.weight-labels",
                    )
                ],
                "revision_evidence": [
                    {
                        "semantic_id": command,
                        "action": "invoke",
                        "reference_semantic_id": "inspector.apply",
                        "reference_action": "invoke",
                        "relation": "same",
                    }
                    for command in (
                        "command.sketch.bspline.degree-labels",
                        "command.sketch.bspline.knot-labels",
                        "command.sketch.bspline.weight-labels",
                    )
                ],
                "width": 1440, "height": 900,
            },
            {
                "name": "proof-sketch-bspline-edit",
                "surface": "editor", "workspace": "sketch",
                "initial_workspace": "model", "design_engine": True,
                "operations": [
                    *create, *bspline,
                    *(
                        operation
                        for command, fields in bspline_edits
                        for operation in edit_bspline(command, fields)
                    ),
                    {"action": "select",
                     "semantic_id": "entity.sketch.bspline.1"},
                ],
                "required": ["sketch.solve.state",
                             "entity.sketch.bspline.1",
                             "input.selection.degree",
                             "input.selection.control-points",
                             "input.selection.weights"],
                "expected_values": {
                    "viewport.state": "current",
                    "input.selection.degree": "3",
                    "input.selection.control-points": "5",
                    "input.selection.weights": "Rational",
                },
                "action_evidence": [
                    {
                        "semantic_id": "inspector.apply",
                        "action": "invoke",
                        "occurrence": occurrence,
                        "state_after_dispatch": "pending",
                        "settled_state": "none",
                        "max_first_presented_ms": 100,
                        "max_current_scene_ms": 350,
                        "max_settled_ms": 450,
                        "require_native_scene": True,
                    }
                    for occurrence in range(2, 8)
                ],
                "revision_evidence": [
                    {
                        "semantic_id": "inspector.apply",
                        "action": "invoke",
                        "occurrence": occurrence,
                        "reference_semantic_id": "inspector.apply",
                        "reference_action": "invoke",
                        "reference_occurrence": occurrence - 1,
                        "relation": "different",
                    }
                    for occurrence in range(2, 8)
                ],
                "width": 1440, "height": 900,
            },
        ])

        rectangle = [
            invoke("command.sketch.rectangle"),
            drag([0.42, 0.43], [0.58, 0.57]),
            {"action": "select",
             "semantic_id": "entity.sketch.rectangle.1"},
            invoke("command-menu.sketch.modify"),
        ]

        def rectangle_modification(
            name: str, command: str, picks: list[list[float]],
            result_id: str, fields: list[dict[str, object]],
        ) -> dict[str, object]:
            return {
                "name": f"proof-sketch-modify-{name}",
                "surface": "editor", "workspace": "sketch",
                "initial_workspace": "model", "design_engine": True,
                "operations": [
                    *create, *rectangle, invoke(command), *fields,
                    *(click(position) for position in picks),
                    invoke("inspector.apply"),
                ],
                "required": [
                    "sketch.solve.state",
                    result_id,
                    *(
                        ["entity.sketch.curve-group.1"]
                        if name in {"fillet", "chamfer"}
                        else []
                    ),
                ],
                "expected_values": {"viewport.state": "current"},
                "revision_evidence": [{
                    "semantic_id": "inspector.apply", "action": "invoke",
                    "reference_semantic_id": "viewport.primary",
                    "reference_action": "pointerDrag",
                    "relation": "different",
                }],
                "width": 1440, "height": 900,
            }

        def detach(command: str) -> dict[str, object]:
            return {
                "action": "choose",
                "semantic_id": f"input.{command}.external-constraints",
                "value": "detach",
            }

        def size(command: str) -> dict[str, object]:
            return {
                "action": "setValue",
                "semantic_id": f"input.{command}.size",
                "value": "5 mm",
            }
        proofs.extend([
            rectangle_modification(
                "fillet", "command.sketch.fillet",
                [[0.52, 0.58], [0.57, 0.52]],
                "entity.sketch.fillet.1",
                [size("sketch.fillet"), detach("sketch.fillet")]),
            rectangle_modification(
                "chamfer", "command.sketch.chamfer",
                [[0.52, 0.58], [0.57, 0.52]],
                "entity.sketch.chamfer.1",
                [size("sketch.chamfer"), detach("sketch.chamfer")]),
            rectangle_modification(
                "offset", "command.sketch.offset", [[0.57, 0.50]],
                "entity.sketch.offset.1",
                [{"action": "setValue",
                  "semantic_id": "input.sketch.offset.distance",
                  "value": "5 mm"}]),
            {
                "name": "proof-sketch-modify-extend",
                "surface": "editor", "workspace": "sketch",
                "initial_workspace": "model", "design_engine": True,
                "operations": [
                    *create, invoke("command.sketch.line"),
                    drag([0.42, 0.56], [0.56, 0.44]),
                    {"action": "select",
                     "semantic_id": "entity.sketch.line.1"},
                    invoke("command-menu.sketch.modify"),
                    invoke("command.sketch.extend"),
                    click([0.54, 0.46]), click([0.68, 0.34]),
                    invoke("inspector.apply"),
                ],
                "required": ["sketch.solve.state", "entity.sketch.line.1"],
                "expected_values": {"viewport.state": "current"},
                "revision_evidence": [{
                    "semantic_id": "inspector.apply", "action": "invoke",
                    "reference_semantic_id": "viewport.primary",
                    "reference_action": "pointerDrag",
                    "relation": "different",
                }],
                "width": 1440, "height": 900,
            },
            {
                "name": "proof-sketch-modify-trim",
                "surface": "editor", "workspace": "sketch",
                "initial_workspace": "model", "design_engine": True,
                "operations": [
                    *create,
                    invoke("command.sketch.line"),
                    drag([0.36, 0.50], [0.64, 0.50]),
                    invoke("command.sketch.line"),
                    drag([0.45, 0.38], [0.45, 0.62]),
                    invoke("command.sketch.line"),
                    drag([0.55, 0.38], [0.55, 0.62]),
                    {"action": "select",
                     "semantic_id": "entity.sketch.line.1"},
                    invoke("command-menu.sketch.modify"),
                    invoke("command.sketch.trim"),
                    hover([0.40, 0.50], [0.50, 0.50], True),
                    click([0.50, 0.50]),
                ],
                "required": ["sketch.solve.state",
                             "entity.sketch.curve-group.1"],
                "expected_values": {"viewport.state": "current",
                                    "sketch.canvas": "sketch.trim:0",
                                    "command.draft.state": "editing"},
                "action_evidence": [
                    {"semantic_id": "viewport.primary",
                     "action": "pointerHover",
                     "settled_state": "editing",
                     "max_pointer_move_p95_ms": 50.0,
                     "require_pointer_move_hover": True,
                     "require_preview_frame": True,
                     "require_preview_image": True,
                     "require_native_scene": True},
                    {"semantic_id": "viewport.primary",
                     "action": "pointerClick",
                     "state_after_dispatch": "pending",
                     "settled_state": "editing",
                     "max_first_presented_ms": 100,
                     "max_settled_ms": 300,
                     "require_native_scene": True},
                ],
                "revision_evidence": [{
                    "semantic_id": "viewport.primary",
                    "action": "pointerClick",
                    "reference_semantic_id": "viewport.primary",
                    "reference_action": "pointerHover",
                    "relation": "different",
                }],
                "width": 1440, "height": 900,
            },
            {
                "name": "proof-sketch-modify-split",
                "surface": "editor", "workspace": "sketch",
                "initial_workspace": "model", "design_engine": True,
                "operations": [
                    *create,
                    invoke("command.sketch.line"),
                    drag([0.36, 0.50], [0.64, 0.50]),
                    {"action": "select",
                     "semantic_id": "entity.sketch.line.1"},
                    invoke("command-menu.sketch.modify"),
                    invoke("command.sketch.split"),
                    hover([0.44, 0.50], [0.52, 0.50], True),
                    click([0.52, 0.50]),
                ],
                "required": ["sketch.solve.state",
                             "entity.sketch.curve-group.1"],
                "expected_values": {"viewport.state": "current",
                                    "sketch.canvas": "sketch.split:0",
                                    "command.draft.state": "editing"},
                "action_evidence": [
                    {"semantic_id": "viewport.primary",
                     "action": "pointerHover",
                     "settled_state": "editing",
                     "max_pointer_move_p95_ms": 50.0,
                     "require_pointer_move_hover": True,
                     "require_preview_frame": True,
                     "require_preview_image": True,
                     "require_native_scene": True},
                    {"semantic_id": "viewport.primary",
                     "action": "pointerClick",
                     "state_after_dispatch": "pending",
                     "settled_state": "editing",
                     "max_first_presented_ms": 100,
                     "max_settled_ms": 300,
                     "require_native_scene": True},
                ],
                "revision_evidence": [{
                    "semantic_id": "viewport.primary",
                    "action": "pointerClick",
                    "reference_semantic_id": "viewport.primary",
                    "reference_action": "pointerHover",
                    "relation": "different",
                }],
                "width": 1440, "height": 900,
            },
            {
                "name": "proof-sketch-modify-join",
                "surface": "editor", "workspace": "sketch",
                "initial_workspace": "model", "design_engine": True,
                "operations": [
                    *create,
                    invoke("command.sketch.line"),
                    drag([0.42, 0.53], [0.52, 0.47]),
                    invoke("command.sketch.line"),
                    drag([0.52, 0.47], [0.62, 0.41]),
                    {"action": "select",
                     "semantic_id": "entity.sketch.line.1"},
                    invoke("command-menu.sketch.modify"),
                    invoke("command.sketch.join"),
                    hover([0.49, 0.49], [0.52, 0.47], True),
                    click([0.52, 0.47]),
                ],
                "required": ["sketch.solve.state",
                             "entity.sketch.joined-curve.1"],
                "expected_values": {"viewport.state": "current",
                                    "sketch.canvas": "sketch.join:0",
                                    "command.draft.state": "editing"},
                "action_evidence": [
                    {"semantic_id": "viewport.primary",
                     "action": "pointerHover",
                     "settled_state": "editing",
                     "max_pointer_move_p95_ms": 50.0,
                     "require_pointer_move_hover": True,
                     "require_preview_image": True,
                     "require_native_scene": True},
                    {"semantic_id": "viewport.primary",
                     "action": "pointerClick",
                     "state_after_dispatch": "pending",
                     "settled_state": "editing",
                     "max_first_presented_ms": 100,
                     "max_settled_ms": 300,
                     "require_native_scene": True},
                ],
                "revision_evidence": [{
                    "semantic_id": "viewport.primary",
                    "action": "pointerClick",
                    "reference_semantic_id": "viewport.primary",
                    "reference_action": "pointerHover",
                    "relation": "different",
                }],
                "width": 1440, "height": 900,
            },
            {
                "name": "proof-sketch-convert-to-nurbs",
                "surface": "editor", "workspace": "sketch",
                "initial_workspace": "model", "design_engine": True,
                "operations": [
                    *create,
                    invoke("command.sketch.circle"),
                    drag([0.50, 0.50], [0.62, 0.50]),
                    {"action": "select",
                     "semantic_id": "entity.sketch.circle.1"},
                    invoke("command-menu.sketch.spline-edit"),
                    invoke("command.sketch.bspline.convert-to-nurbs"),
                    hover([0.42, 0.42], [0.50, 0.365], True),
                    click([0.50, 0.365]),
                    invoke("inspector.tab.source"),
                ],
                "required": ["sketch.solve.state",
                             "entity.sketch.bspline.1",
                             "inspector.source.editor"],
                "expected_values": {
                    "viewport.state": "current",
                    "sketch.canvas": "sketch.bspline.convert-to-nurbs:0",
                },
                "expected_source_contains": "bspline_object(",
                "action_evidence": [
                    {"semantic_id": "viewport.primary",
                     "action": "pointerHover",
                     "settled_state": "editing",
                     "max_pointer_move_p95_ms": 60.0,
                     "require_pointer_move_hover": True,
                     "require_preview_image": True,
                     "require_native_scene": True},
                    {"semantic_id": "viewport.primary",
                     "action": "pointerClick",
                     "state_after_dispatch": "pending",
                     "settled_state": "editing",
                     "max_first_presented_ms": 100,
                     "max_current_scene_ms": 350,
                     "max_settled_ms": 450,
                     "require_native_scene": True},
                ],
                "revision_evidence": [{
                    "semantic_id": "viewport.primary",
                    "action": "pointerClick",
                    "reference_semantic_id": "viewport.primary",
                    "reference_action": "pointerHover",
                    "relation": "different",
                }],
                "width": 1440, "height": 900,
            },
        ])
        return proofs
    result = [
        {"name": f"route-{surface}", "initial_surface": initial, "surface": surface,
         "actions": [action], "width": 1440, "height": 900}
        for surface, (initial, action) in ROUTE_ACTIONS.items()
    ]
    result.append(
        {"name": "route-editor-projects-round-trip", "surface": "editor",
         "design_engine": True,
         "operations": [
             {"action": "invoke", "semantic_id": "navigation.projects"},
             {"action": "invoke", "semantic_id": "navigation.editor"},
         ],
         "required": ["viewport.primary", "navigation.projects",
                      "navigation.editor"],
         "expected_values": {"viewport.state": "current"},
         "width": 1440, "height": 900}
    )
    result.extend(
        {"name": f"workspace-{workspace}", "surface": "editor", "workspace": workspace,
         "initial_workspace": "model", "actions": [f"workspace.{workspace}"],
         "viewport_visual": workspace == "model",
         "expected_values": {
             "viewport.state": "current" if workspace in ("model", "sketch")
             else "unavailable"},
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
    result.append(
        {"name": "inspector-function-ports", "surface": "editor",
         "actions": ["inspector.tab.source", "function.contract.toggle"],
         "required": ["function.port.input.width", "function.port.input.depth",
                      "function.port.input.thickness", "function.port.output.body"],
         "width": 1440, "height": 900}
    )
    result.append(
        {"name": "sketch-source-function", "surface": "editor",
         "initial_workspace": "model", "workspace": "sketch",
         "operations": [
             {"action": "select",
              "semantic_id": "entity.function.mounting_profile"},
             {"action": "invoke", "semantic_id": "workspace.sketch"},
             {"action": "invoke", "semantic_id": "inspector.tab.source"},
             {"action": "invoke", "semantic_id": "function.contract.toggle"},
         ],
         "required": ["function.port.input.width",
                      "function.port.input.depth",
                      "function.port.input.hole-spacing",
                      "function.port.input.hole-diameter",
                      "function.port.output.profile"],
         "expected_absent": ["function.port.input.thickness",
                             "function.port.output.body"],
         "width": 1440, "height": 900}
    )
    result.extend((
        {"name": "source-revision-review", "surface": "editor",
         "operations": [
             {"action": "invoke", "semantic_id": "inspector.tab.source"},
             {"action": "setValue", "semantic_id": "inspector.source.editor",
              "value": "from build123d import Box\n\ndef base_plate():\n    return Box(120, 60, 8)\n"},
             {"action": "invoke", "semantic_id": "source.diff"},
         ],
         "required": ["dialog.source_diff", "source_diff.current",
                      "source_diff.proposed", "source_diff.apply",
                      "source.draft.state"],
         "expected_visible": ["dialog.source_diff"],
         "expected_values": {"inspector.source.editor": "modified",
                             "source.draft.state": "modified"},
         "width": 1440, "height": 900},
        {"name": "source-revision-apply", "surface": "editor",
         "operations": [
             {"action": "invoke", "semantic_id": "inspector.tab.source"},
             {"action": "setValue", "semantic_id": "inspector.source.editor",
              "value": "from build123d import Box\n\ndef base_plate():\n    return Box(120, 60, 8)\n"},
             {"action": "invoke", "semantic_id": "source.diff"},
             {"action": "invoke", "semantic_id": "source_diff.apply"},
         ],
         "required": ["dialog.source_diff", "source.draft.state"],
         "expected_hidden": ["dialog.source_diff"],
         "expected_values": {"inspector.source.editor": "current",
                             "source.draft.state": "current"},
         "width": 1440, "height": 900},
        {"name": "transient-command-palette", "surface": "editor",
         "actions": ["command.palette.open"],
         "required": ["dialog.command_palette", "command_palette.query",
                      "palette.command.model.sketch.create"],
         "expected_visible": ["dialog.command_palette", "command_palette.query"],
         "width": 1280, "height": 800},
        {"name": "transient-command-palette-dismiss", "surface": "editor",
         "operations": [
             {"action": "invoke", "semantic_id": "command.palette.open"},
             {"action": "dismiss", "semantic_id": "dialog.command_palette"},
         ],
         "expected_hidden": ["dialog.command_palette"],
         "width": 1280, "height": 800},
        {"name": "command-palette-context-route", "surface": "editor",
         "workspace": "model", "initial_workspace": "assemble",
         "operations": [
             {"action": "invoke", "semantic_id": "command.palette.open"},
             {"action": "invoke",
              "semantic_id": "palette.command.model.extrude"},
         ],
         "required": ["field.model.extrude.profile",
                      "command.draft.state"],
         "expected_values": {"command.draft.state": "editing",
                             "viewport.state": "current"},
         "expected_hidden": ["dialog.command_palette"],
         "width": 1440, "height": 900},
        {"name": "transient-theme-import", "surface": "settings",
         "settings_category": "appearance",
         "actions": ["settings.theme.import"],
         "required": ["dialog.theme_import", "theme_import.path",
                      "theme_import.cancel", "theme_import.apply"],
         "expected_visible": ["dialog.theme_import", "theme_import.path"],
         "width": 1280, "height": 800},
        {"name": "transient-theme-import-dismiss", "surface": "settings",
         "settings_category": "appearance",
         "operations": [
             {"action": "invoke", "semantic_id": "settings.theme.import"},
             {"action": "dismiss", "semantic_id": "dialog.theme_import"},
         ],
         "expected_hidden": ["dialog.theme_import"],
         "width": 1280, "height": 800},
        {"name": "transient-structure-drawer", "surface": "editor",
         "actions": ["viewport.structure.toggle"],
         "required": ["drawer.structure", "panel.structure"],
         "expected_visible": ["drawer.structure", "panel.structure"],
         "width": 800, "height": 600},
        {"name": "transient-structure-drawer-dismiss", "surface": "editor",
         "operations": [
             {"action": "invoke", "semantic_id": "viewport.structure.toggle"},
             {"action": "dismiss", "semantic_id": "drawer.structure"},
         ],
         "expected_hidden": ["drawer.structure"],
         "expected_absent": ["panel.structure"],
         "width": 800, "height": 600},
        {"name": "transient-inspector-drawer", "surface": "editor",
         "actions": ["viewport.inspector.toggle"],
         "required": ["drawer.inspector", "panel.inspector"],
         "expected_visible": ["drawer.inspector", "panel.inspector"],
         "width": 800, "height": 600},
        {"name": "transient-inspector-drawer-dismiss", "surface": "editor",
         "operations": [
             {"action": "invoke", "semantic_id": "viewport.inspector.toggle"},
             {"action": "dismiss", "semantic_id": "drawer.inspector"},
         ],
         "expected_hidden": ["drawer.inspector"],
         "expected_absent": ["panel.inspector"],
         "width": 800, "height": 600},
    ))
    result.extend((
        {"name": "command-plane", "surface": "editor",
         "actions": ["command.model.plane.create"],
         "required": ["field.model.plane.create.method",
                      "field.model.plane.create.reference",
                      "field.model.plane.create.offset"],
         "width": 1440, "height": 900},
        {"name": "command-new-sketch", "surface": "editor",
         "workspace": "sketch", "initial_workspace": "model",
         "actions": ["command.model.sketch.create"],
         "required": ["field.model.sketch.create.attachment",
                      "field.model.sketch.create.orientation"],
         "width": 1440, "height": 900},
        {"name": "command-extrude", "surface": "editor",
         "actions": ["command.model.extrude"],
         "required": ["field.model.extrude.profile",
                      "field.model.extrude.distance",
                      "field.model.extrude.extent",
                      "field.model.extrude.operation", "command.draft.state",
                      "inspector.cancel", "inspector.preview", "inspector.apply"],
         "expected_values": {"command.draft.state": "editing"},
         "width": 1440, "height": 900},
        {"name": "command-extrude-preview", "surface": "editor",
         "operations": [
             {"action": "invoke", "semantic_id": "command.model.extrude"},
             {"action": "setValue",
              "semantic_id": "input.model.extrude.distance", "value": "25 mm"},
             {"action": "invoke", "semantic_id": "inspector.preview"},
         ],
         "required": ["command.draft.state", "viewport.state"],
         "expected_values": {"command.draft.state": "preview",
                             "viewport.state": "preview"},
         "width": 1440, "height": 900},
        {"name": "command-extrude-apply", "surface": "editor",
         "operations": [
             {"action": "invoke", "semantic_id": "command.model.extrude"},
             {"action": "invoke", "semantic_id": "inspector.preview"},
             {"action": "invoke", "semantic_id": "inspector.apply"},
         ],
         "required": ["command.draft.state", "viewport.state"],
         "expected_values": {"command.draft.state": "unavailable",
                             "viewport.state": "current"},
         "width": 1440, "height": 900},
        {"name": "command-extrude-cancel", "surface": "editor",
         "operations": [
             {"action": "invoke", "semantic_id": "command.model.extrude"},
             {"action": "invoke", "semantic_id": "inspector.cancel"},
         ],
         "expected_absent": ["field.model.extrude.profile",
                             "field.model.extrude.distance",
                             "command.draft.state"],
         "expected_hidden": ["inspector.cancel", "inspector.preview",
                             "inspector.apply"],
         "width": 1440, "height": 900},
        {"name": "command-sketch-rectangle", "surface": "editor",
         "workspace": "sketch", "initial_workspace": "model",
         "operations": [
             {"action": "select",
              "semantic_id": "entity.function.mounting_profile"},
             {"action": "invoke", "semantic_id": "workspace.sketch"},
             {"action": "invoke",
              "semantic_id": "command.sketch.rectangle"},
             {"action": "toggle",
              "semantic_id": "input.sketch.rectangle.construction"},
         ],
         "required": ["field.sketch.rectangle.construction",
                      "input.sketch.rectangle.construction",
                      "command.draft.state", "inspector.cancel"],
         "expected_values": {"command.draft.state": "editing",
                             "viewport.state": "current"},
         "expected_hidden": ["inspector.preview", "inspector.apply"],
         "width": 1440, "height": 900},
        {"name": "sketch-pointer-rectangle-ready", "surface": "editor",
         "workspace": "sketch", "initial_workspace": "model",
         "operations": [
             {"action": "select",
              "semantic_id": "entity.function.mounting_profile"},
             {"action": "invoke", "semantic_id": "workspace.sketch"},
             {"action": "invoke",
              "semantic_id": "command.sketch.rectangle"},
             {"action": "pointerDrag", "semantic_id": "viewport.primary",
              "value": {"button": "left", "from": [0.42, 0.43],
                        "to": [0.58, 0.57]}},
         ],
         "required": ["sketch.canvas", "command.draft.state",
                      "inspector.preview", "inspector.apply"],
         "expected_values": {"sketch.canvas": "sketch.rectangle:2",
                             "command.draft.state": "editing"},
         "expected_visible": ["inspector.preview", "inspector.apply"],
         "width": 1440, "height": 900},
        {"name": "command-unavailable", "surface": "editor",
         "actions": ["command.palette.open"],
         "required": ["palette.command.model.revolve"],
         "expected_absent": ["command.model.revolve"],
         "expected_disabled": ["palette.command.model.revolve"],
         "width": 1440, "height": 900},
        {"name": "parameter-manager", "surface": "editor",
         "actions": ["parameters.manage"],
         "required": ["dialog.parameters", "parameter_manager.list",
                      "parameter_manager.editor",
                      "parameter_manager.expression",
                      "parameter_manager.apply"],
         "expected_visible": ["dialog.parameters"],
         "expected_values": {"dialog.parameters": "width"},
         "width": 1440, "height": 900},
        {"name": "parameter-edit-apply", "surface": "editor",
         "operations": [
             {"action": "invoke", "semantic_id": "parameters.manage"},
             {"action": "setValue",
              "semantic_id": "parameter_manager.expression",
              "value": "120 mm"},
             {"action": "invoke", "semantic_id": "parameter_manager.apply"},
         ],
         "required": ["dialog.parameters", "parameter.width"],
         "expected_hidden": ["dialog.parameters"],
         "expected_values": {"parameter.width": "120 mm"},
         "width": 1440, "height": 900},
        {"name": "operation-inspect", "surface": "operations",
         "operations": [
             {"action": "inspect",
              "semantic_id": "operation.operation.frontend"},
         ],
         "required": ["operation.detail"],
         "expected_values": {"operation.detail": "operation.frontend"},
         "width": 1280, "height": 800},
    ))
    result.extend(
        {"name": f"project-route-{workspace}-{index}", "initial_surface": "projects",
         "surface": "editor", "workspace": workspace, "actions": [semantic_id],
         "expected_values": {
             "viewport.state": "current" if workspace == "model" else "unavailable"},
         "width": 1440, "height": 900}
        for index, (semantic_id, workspace) in enumerate(PROJECT_ROUTES.items())
    )
    result.extend((
        {"name": "viewport-orbit", "surface": "editor",
         "operations": [
             {"action": "orbit", "semantic_id": "viewport.primary"},
         ],
         "required": ["viewport.view_cube", "viewport.camera.fit"],
         "expected_contains": {"viewport.primary": "solidworks:custom"},
         "width": 1440, "height": 900},
        {"name": "viewport-pointer-orbit", "surface": "editor",
         "operations": [
             {"action": "pointerDrag", "semantic_id": "viewport.primary",
              "value": {"button": "middle", "modifiers": [],
                        "from": [0.42, 0.44], "to": [0.58, 0.54]}},
         ],
         "required": ["viewport.view_cube", "viewport.camera.fit"],
         "expected_contains": {"viewport.primary": "solidworks:custom"},
         "width": 1440, "height": 900},
        {"name": "viewport-adaptive-grid-zoom", "surface": "editor",
         "operations": [
             {"action": "zoom", "semantic_id": "viewport.primary",
              "value": 4},
         ],
         "required": ["region.status_bar"],
         "expected_contains": {"region.status_bar": ":5 mm"},
         "width": 1440, "height": 900},
        {"name": "viewport-view-cube-front", "surface": "editor",
         "operations": [
             {"action": "choose", "semantic_id": "viewport.view_cube",
              "value": "front"},
         ],
         "required": ["viewport.view_cube", "viewport.camera.fit"],
         "expected_values": {"viewport.view_cube": "front"},
         "expected_contains": {"viewport.primary": "solidworks:front"},
         "width": 1440, "height": 900},
        {"name": "viewport-pointer-cube-left", "surface": "editor",
         "operations": [
             {"action": "pointerClick", "semantic_id": "viewport.view_cube",
              "value": {"button": "left", "modifiers": [],
                        "position": [0.33, 0.48]}},
         ],
         "required": ["viewport.view_cube", "viewport.camera.fit"],
         "expected_values": {"viewport.view_cube": "left"},
         "expected_contains": {"viewport.primary": "solidworks:left"},
         "width": 1440, "height": 900},
        {"name": "viewport-display-mode-select", "surface": "editor",
         "profile": "display-mode-persistence",
         "operations": [
             {"action": "choose", "semantic_id": "viewport.display_mode",
              "value": "wireframe"},
         ],
         "expected_values": {"viewport.display_mode": "wireframe"},
         "expected_workspace": {"display_mode": "wireframe"},
         "width": 1440, "height": 900},
        {"name": "viewport-display-mode-restart", "surface": "editor",
         "profile": "display-mode-persistence",
         "expected_values": {"viewport.display_mode": "wireframe"},
         "expected_workspace": {"display_mode": "wireframe"},
         "width": 1440, "height": 900},
        {"name": "theme-dark-select", "surface": "settings",
         "settings_category": "appearance", "theme": "light",
         "profile": "theme-persistence", "expected_theme": "dark",
         "operations": [
             {"action": "choose", "semantic_id": "setting.theme.control",
              "value": "dark"},
             {"action": "invoke", "semantic_id": "navigation.editor"},
             {"action": "invoke", "semantic_id": "navigation.settings"},
         ],
         "expected_values": {"setting.theme.control": "dark"},
         "expected_preferences": {"theme": "dark"},
         "width": 1280, "height": 800},
        {"name": "theme-dark-restart", "surface": "settings",
         "settings_category": "appearance", "theme": None,
         "profile": "theme-persistence", "expected_theme": "dark",
         "expected_values": {"setting.theme.control": "dark"},
         "expected_preferences": {"theme": "dark"},
         "width": 1280, "height": 800},
        {"name": "density-comfortable-select", "surface": "settings",
         "settings_category": "appearance",
         "profile": "density-persistence",
         "operations": [
             {"action": "choose",
              "semantic_id": "setting.interface-density.control",
              "value": "comfortable"},
         ],
         "expected_values": {
             "setting.interface-density.control": "comfortable"},
         "expected_bounds": {"region.project_bar": [None, None, None, 51]},
         "expected_preferences": {"interface-density": "comfortable"},
         "width": 1280, "height": 800},
        {"name": "density-comfortable-restart", "surface": "settings",
         "settings_category": "appearance",
         "profile": "density-persistence",
         "expected_values": {
             "setting.interface-density.control": "comfortable"},
         "expected_bounds": {"region.project_bar": [None, None, None, 51]},
         "expected_preferences": {"interface-density": "comfortable"},
         "width": 1280, "height": 800},
        {"name": "default-unit-select", "surface": "settings",
         "settings_category": "units", "profile": "unit-persistence",
         "operations": [
             {"action": "choose",
              "semantic_id": "setting.default-length-unit.control",
              "value": "in"},
             {"action": "invoke", "semantic_id": "navigation.editor"},
             {"action": "invoke", "semantic_id": "navigation.settings"},
         ],
         "expected_values": {"setting.default-length-unit.control": "in"},
         "expected_preferences": {"default-length-unit": "in"},
         "width": 1280, "height": 800},
        {"name": "default-unit-new-project", "initial_surface": "projects",
         "surface": "editor", "workspace": "model",
         "profile": "unit-persistence", "actions": ["projects.new"],
         "expected_values": {"project.length_unit": "in",
                             "viewport.grid": "XY:0.5 in"},
         "expected_preferences": {"default-length-unit": "in"},
         "width": 1440, "height": 900},
        {"name": "navigation-profile-select", "surface": "settings",
         "settings_category": "input", "profile": "navigation-persistence",
         "operations": [
             {"action": "choose",
              "semantic_id": "setting.navigation-profile.control",
              "value": "fusion"},
         ],
         "required": ["settings.input.space_mouse"],
         "expected_values": {
             "setting.navigation-profile.control": "fusion"},
         "expected_preferences": {"navigation-profile": "fusion"},
         "width": 1280, "height": 800},
        {"name": "navigation-profile-restart", "surface": "settings",
         "settings_category": "input", "profile": "navigation-persistence",
         "required": ["settings.input.space_mouse"],
         "expected_values": {
             "setting.navigation-profile.control": "fusion"},
         "expected_preferences": {"navigation-profile": "fusion"},
         "width": 1280, "height": 800},
        {"name": "workspace-layout-customize", "surface": "editor",
         "profile": "workspace-layout-persistence",
         "operations": [
             {"action": "resize", "semantic_id": "layout.structure.resize",
              "value": 360},
             {"action": "resize", "semantic_id": "layout.inspector.resize",
              "value": 420},
             {"action": "invoke", "semantic_id": "viewport.grid.toggle"},
             {"action": "invoke", "semantic_id": "viewport.grid_snap.toggle"},
             {"action": "invoke", "semantic_id": "structure.collapse"},
             {"action": "invoke", "semantic_id": "inspector.collapse"},
         ],
         "required": ["viewport.structure.toggle", "viewport.inspector.toggle"],
         "expected_absent": ["panel.structure", "panel.inspector",
                             "layout.structure.resize", "layout.inspector.resize"],
         "expected_values": {"viewport.grid.toggle": "hidden",
                             "viewport.grid_snap.toggle": "disabled"},
         "expected_workspace": {
             "structure_width": 360, "inspector_width": 420,
             "structure_visible": False, "inspector_visible": False,
             "grid_visible": False, "grid_snap_enabled": False,
         },
         "width": 1440, "height": 900},
        {"name": "workspace-layout-restart", "surface": "editor",
         "profile": "workspace-layout-persistence",
         "required": ["viewport.structure.toggle", "viewport.inspector.toggle"],
         "expected_absent": ["panel.structure", "panel.inspector",
                             "layout.structure.resize", "layout.inspector.resize"],
         "expected_values": {"viewport.grid.toggle": "hidden",
                             "viewport.grid_snap.toggle": "disabled"},
         "expected_workspace": {
             "structure_width": 360, "inspector_width": 420,
             "structure_visible": False, "inspector_visible": False,
             "grid_visible": False, "grid_snap_enabled": False,
         },
         "width": 1440, "height": 900},
        {"name": "workspace-layout-restore", "surface": "editor",
         "profile": "workspace-layout-persistence",
         "operations": [
             {"action": "invoke", "semantic_id": "viewport.structure.toggle"},
             {"action": "invoke", "semantic_id": "viewport.inspector.toggle"},
             {"action": "invoke", "semantic_id": "viewport.grid.toggle"},
             {"action": "invoke", "semantic_id": "viewport.grid_snap.toggle"},
         ],
         "required": ["panel.structure", "panel.inspector",
                      "layout.structure.resize", "layout.inspector.resize"],
         "expected_values": {"viewport.grid.toggle": "visible",
                             "viewport.grid_snap.toggle": "enabled"},
         "expected_bounds": {"panel.structure": [None, None, 360, None],
                             "panel.inspector": [None, None, 420, None]},
         "expected_workspace": {
             "structure_width": 360, "inspector_width": 420,
             "structure_visible": True, "inspector_visible": True,
             "grid_visible": True, "grid_snap_enabled": True,
         },
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
           display: str | None, graphics: str) -> None:
    width = int(scenario["width"])
    height = int(scenario["height"])
    command = [
        str(executable), "--capture-dir", str(output), "--surface",
        str(scenario.get("initial_surface", scenario["surface"])), "--workspace",
        str(scenario.get("initial_workspace", scenario.get("workspace", "model"))),
        "--inspector-page",
        "properties", "--settings-category",
        str(scenario.get("initial_settings_category",
                         scenario.get("settings_category", "appearance"))), "--width", str(width),
        "--height", str(height),
    ]
    if "state" in scenario:
        command.extend(("--ui-state", str(scenario["state"])))
    if scenario.get("design_engine"):
        command.append("--design-engine")
    if scenario.get("theme", "light") is not None:
        command.extend(("--theme", str(scenario.get("theme", "light"))))
    for operation in operations_for(scenario):
        command.extend(("--ui-operation", json.dumps(operation, separators=(",", ":"))))
    environment = os.environ.copy()
    profile = profile_for(output, scenario)
    profile.mkdir(parents=True, exist_ok=True, mode=0o700)
    if os.name == "posix":
        profile.chmod(0o700)
    environment["XDG_CONFIG_HOME"] = str(profile / "config")
    environment["XDG_DATA_HOME"] = str(profile / "data")
    environment["XDG_CACHE_HOME"] = str(profile / "cache")
    environment.setdefault("QT_QUICK_BACKEND", "rhi")
    if sys.platform.startswith("linux"):
        if graphics == "software-opengl":
            environment["QSG_RHI_BACKEND"] = "opengl"
            environment["LIBGL_ALWAYS_SOFTWARE"] = "1"
        elif graphics == "vulkan":
            environment["QSG_RHI_BACKEND"] = "vulkan"
            environment.pop("LIBGL_ALWAYS_SOFTWARE", None)
            environment.setdefault("QT_VK_PHYSICAL_DEVICE_INDEX", "0")
    elif graphics != "platform":
        raise RuntimeError(
            f"the {graphics} observation backend is only available on Linux"
        )
    if display:
        environment["DISPLAY"] = display
    elif sys.platform.startswith("linux") and "DISPLAY" not in environment:
        environment["QT_QPA_PLATFORM"] = "offscreen"
    completed = subprocess.run(
        command, capture_output=True, text=True, env=environment,
        timeout=45 if scenario.get("design_engine") else 20,
    )
    if completed.returncode:
        raise RuntimeError(
            f"application exited {completed.returncode}\nstdout:\n{completed.stdout}\nstderr:\n{completed.stderr}"
        )
    if completed.stderr.strip():
        raise RuntimeError(f"application emitted stderr:\n{completed.stderr}")


def png_pixels(path: Path) -> tuple[int, int, int, int, list[bytes]]:
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
    rows: list[bytes] = []
    previous = bytearray(stride)
    for y in range(height):
        offset = y * (stride + 1)
        filter_kind = raw[offset]
        encoded = raw[offset + 1:offset + stride + 1]
        row = bytearray(stride)
        for index, value in enumerate(encoded):
            left = row[index - channels] if index >= channels else 0
            above = previous[index]
            upper_left = previous[index - channels] if index >= channels else 0
            if filter_kind == 0:
                predictor = 0
            elif filter_kind == 1:
                predictor = left
            elif filter_kind == 2:
                predictor = above
            elif filter_kind == 3:
                predictor = (left + above) // 2
            elif filter_kind == 4:
                estimate = left + above - upper_left
                distances = (abs(estimate - left), abs(estimate - above),
                             abs(estimate - upper_left))
                predictor = (left, above, upper_left)[distances.index(min(distances))]
            else:
                raise ValueError(f"unsupported PNG filter {filter_kind}")
            row[index] = (value + predictor) & 0xff
        rows.append(bytes(row))
        previous = row
    step = max(1, int(math.sqrt(width * height / 16_384)))
    colors = {
        rows[y][x * channels:(x + 1) * channels]
        for y in range(0, height, step)
        for x in range(0, width, step)
    }
    return width, height, len(colors), channels, rows


def region_color_count(rows: list[bytes], channels: int,
                       bounds: list[object]) -> int:
    image_height = len(rows)
    image_width = len(rows[0]) // channels
    x, y, width, height = (float(value) for value in bounds)
    x0 = max(0, int(x + width * 0.18))
    x1 = min(image_width, int(x + width * 0.82))
    y0 = max(0, int(y + height * 0.18))
    y1 = min(image_height, int(y + height * 0.74))
    area = max(1, (x1 - x0) * (y1 - y0))
    step = max(1, int(math.sqrt(area / 16_384)))
    return len({
        rows[row][column * channels:(column + 1) * channels]
        for row in range(y0, y1, step)
        for column in range(x0, x1, step)
    })


def png_difference(left: Path, right: Path) -> float:
    left_width, left_height, _, left_channels, left_rows = png_pixels(left)
    right_width, right_height, _, right_channels, right_rows = png_pixels(right)
    if ((left_width, left_height, left_channels)
            != (right_width, right_height, right_channels)):
        return 1.0
    step = max(1, int(math.sqrt(left_width * left_height / 65_536)))
    components = min(3, left_channels)
    difference = samples = 0
    for y in range(0, left_height, step):
        for x in range(0, left_width, step):
            offset = x * left_channels
            difference += sum(
                abs(left_rows[y][offset + component]
                    - right_rows[y][offset + component])
                for component in range(components)
            )
            samples += components
    return difference / (samples * 255)


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


def bounds_overlap(left: list[object], right: list[object]) -> bool:
    left_x, left_y, left_width, left_height = map(float, left)
    right_x, right_y, right_width, right_height = map(float, right)
    return (max(left_x, right_x) < min(left_x + left_width,
                                      right_x + right_width)
            and max(left_y, right_y) < min(left_y + left_height,
                                           right_y + right_height))


def validate(output: Path, scenario: dict[str, object],
             graphics: str) -> list[str]:
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
        image_width, image_height, color_count, channels, rows = png_pixels(
            image_path)
    except (OSError, ValueError, zlib.error) as error:
        return [str(error)]
    if (image_width, image_height) != (int(scenario["width"]), int(scenario["height"])):
        failures.append(f"image size {(image_width, image_height)} does not match scenario")
    if color_count < 8:
        failures.append(f"capture has only {color_count} sampled colors")
    metadata = json.loads(metadata_path.read_text(encoding="utf-8"))
    semantic = json.loads(semantic_path.read_text(encoding="utf-8"))
    semantic_schema, capture_schema = protocol_schemas()
    if metadata.get("schema") != capture_schema:
        failures.append("capture schema mismatch")
    expected_graphics = {
        "software-opengl": "opengl", "vulkan": "vulkan"
    }.get(graphics)
    if (expected_graphics is not None
            and metadata.get("graphics_api") != expected_graphics):
        failures.append(
            f"graphics API is {metadata.get('graphics_api')}, expected {expected_graphics}"
        )
    if semantic.get("schema") != semantic_schema:
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
    if (not isinstance(metadata.get("project_revision"), str)
            or not metadata["project_revision"]
            or metadata.get("project_revision") != semantic.get("project_revision")):
        failures.append("semantic and capture project revisions differ")
    expected_operations = operations_for(scenario)
    action_receipts = metadata.get("actions", [])
    semantic_actions = semantic.get("actions", [])
    if action_receipts != semantic_actions:
        failures.append("semantic and capture action receipts differ")
    if len(action_receipts) != len(expected_operations):
        failures.append("executed semantic operation count differs from scenario")
    for receipt, expected in zip(action_receipts, expected_operations, strict=False):
        for key in ("action", "semantic_id", "value"):
            if key in expected and receipt.get(key) != expected[key]:
                failures.append(f"semantic operation receipt differs for {key}")
        if receipt.get("generation_after", -1) < receipt.get("generation_before", 0):
            failures.append("semantic operation receipt moved generation backward")
        dispatch_ms = receipt.get("dispatch_ms")
        if (not isinstance(dispatch_ms, (int, float))
                or not math.isfinite(dispatch_ms) or dispatch_ms < 0):
            failures.append("semantic operation receipt has invalid dispatch timing")
        presented = receipt.get("presented")
        if not isinstance(presented, list) or not presented:
            failures.append(
                "semantic operation receipt has no presented-frame evidence"
            )
            continue
        frame_before = receipt.get("presented_frame_before")
        first = presented[0]
        if (not isinstance(frame_before, int)
                or first.get("frame") != frame_before + 1):
            failures.append("semantic operation was not observed on the next frame")
        prior_frame = frame_before
        prior_elapsed = -1.0
        for frame in presented:
            frame_number = frame.get("frame")
            elapsed_ms = frame.get("elapsed_ms")
            if (not isinstance(frame_number, int) or frame_number <= prior_frame
                    or not isinstance(elapsed_ms, (int, float))
                    or not math.isfinite(elapsed_ms) or elapsed_ms < prior_elapsed):
                failures.append("semantic operation frame trace is not monotonic")
                break
            prior_frame = frame_number
            prior_elapsed = elapsed_ms
        settled_frame = receipt.get("settled_frame")
        settled_ms = receipt.get("settled_ms")
        if (not isinstance(settled_frame, int) or settled_frame < prior_frame
                or not isinstance(settled_ms, (int, float))
                or not math.isfinite(settled_ms) or settled_ms < prior_elapsed):
            failures.append("semantic operation settlement evidence is invalid")
        if receipt.get("settled_generation", -1) < receipt.get("generation_after", 0):
            failures.append("semantic operation settled generation moved backward")
        if receipt.get("settled_native_scene_current") is not True:
            failures.append("semantic operation settled before its native scene")
        current_scene_frame = receipt.get("current_scene_frame")
        current_scene_ms = receipt.get("current_scene_ms")
        if (not isinstance(current_scene_frame, int)
                or current_scene_frame > settled_frame
                or not isinstance(current_scene_ms, (int, float))
                or not math.isfinite(current_scene_ms)
                or current_scene_ms > settled_ms):
            failures.append("semantic operation current-scene evidence is invalid")
        if "pointer_move_frames" in receipt:
            move_frames = receipt.get("pointer_move_frames")
            move_count = receipt.get("pointer_move_presentations")
            if (not isinstance(move_frames, list) or not move_frames
                    or move_count != len(move_frames)):
                failures.append("pointer motion has invalid presented-move evidence")
                continue
            prior_move = 0
            prior_move_frame = frame_before if isinstance(frame_before, int) else 0
            for move_frame in move_frames:
                move = move_frame.get("move")
                frame = move_frame.get("frame")
                latency_ms = move_frame.get("latency_ms")
                if (not isinstance(move, int) or move != prior_move + 1
                        or not isinstance(frame, int) or frame <= prior_move_frame
                        or not isinstance(latency_ms, (int, float))
                        or not math.isfinite(latency_ms) or latency_ms < 0
                        or not isinstance(move_frame.get("preview_visible"), bool)
                        or not isinstance(
                            move_frame.get("preview_generation"), int)
                        or move_frame.get("preview_generation", -1) < 0):
                    failures.append(
                        "pointer motion presented-move evidence is invalid")
                    break
                prior_move = move
                prior_move_frame = frame
    for evidence in scenario.get("action_evidence", []):
        matches = [
            receipt for receipt in action_receipts
            if receipt.get("semantic_id") == evidence["semantic_id"]
            and receipt.get("action") == evidence["action"]
        ]
        occurrence = evidence.get("occurrence", 1)
        if (not isinstance(occurrence, int) or occurrence < 1
                or len(matches) < occurrence
                or ("occurrence" not in evidence and len(matches) != 1)):
            failures.append(
                "action evidence target occurrence is unavailable: "
                f"{evidence['semantic_id']}:{evidence['action']}"
            )
            continue
        receipt = matches[occurrence - 1]
        presented = receipt.get("presented", [])
        first_presented_ms = (
            presented[0].get("elapsed_ms", math.inf)
            if isinstance(presented, list) and presented else math.inf
        )
        checks = (
            ("state_after_dispatch", "state_after_dispatch"),
            ("state_after_input", "state_after_input"),
            ("settled_state", "settled_state"),
        )
        for expected_key, receipt_key in checks:
            if (expected_key in evidence
                    and receipt.get(receipt_key) != evidence[expected_key]):
                failures.append(
                    f"{evidence['semantic_id']}: {receipt_key} differs from "
                    "evidence contract"
                )
        if first_presented_ms > evidence.get("max_first_presented_ms", math.inf):
            failures.append(
                f"{evidence['semantic_id']}: first presented frame exceeded budget"
            )
        if (receipt.get("settled_ms", math.inf)
                > evidence.get("max_settled_ms", math.inf)):
            failures.append(f"{evidence['semantic_id']}: settlement exceeded budget")
        if (receipt.get("current_scene_ms", math.inf)
                > evidence.get("max_current_scene_ms", math.inf)):
            failures.append(
                f"{evidence['semantic_id']}: current native scene exceeded budget"
            )
        if (receipt.get("settled_after_input_ms", math.inf)
                > evidence.get("max_settled_after_input_ms", math.inf)):
            failures.append(
                f"{evidence['semantic_id']}: post-input settlement exceeded budget"
            )
        if (receipt.get("current_scene_after_input_ms", math.inf)
                > evidence.get("max_current_scene_after_input_ms", math.inf)):
            failures.append(
                f"{evidence['semantic_id']}: accepted native scene exceeded budget"
            )
        move_frames = receipt.get("pointer_move_frames", [])
        if not isinstance(move_frames, list):
            move_frames = []
        move_latencies = sorted(
            move.get("latency_ms", math.inf) for move in move_frames)
        move_p95 = (move_latencies[
            max(0, math.ceil(0.95 * len(move_latencies)) - 1)]
            if move_latencies else math.inf)
        if move_p95 > evidence.get("max_pointer_move_p95_ms", math.inf):
            failures.append(
                f"{evidence['semantic_id']}: pointer preview p95 exceeded budget"
            )
        if evidence.get("require_pointer_move_preview"):
            visible_moves = [
                move.get("preview_visible") is True for move in move_frames
            ]
            first_visible = next(
                (index for index, visible in enumerate(visible_moves) if visible),
                None)
            if (first_visible is None
                    or not all(visible_moves[first_visible:])):
                failures.append(
                    f"{evidence['semantic_id']}: pointer preview did not remain visible"
                )
            visible_generations = [
                move.get("preview_generation")
                for move in move_frames
                if move.get("preview_visible") is True
            ]
            if len(set(visible_generations)) < 2:
                failures.append(
                    f"{evidence['semantic_id']}: pointer preview geometry did not update"
                )
        if (evidence.get("forbid_pointer_move_preview")
                and any(move.get("preview_visible") is True
                        for move in move_frames)):
            failures.append(
                f"{evidence['semantic_id']}: unwanted pointer preview became visible"
            )
        if evidence.get("require_pointer_move_hover"):
            hovered_moves = [
                move.get("hovered_entity", "") for move in move_frames
            ]
            first_hovered = next(
                (index for index, entity in enumerate(hovered_moves) if entity),
                None)
            if (first_hovered is None
                    or not all(hovered_moves[first_hovered:])):
                failures.append(
                    f"{evidence['semantic_id']}: hover highlight did not remain active"
                )
        if (evidence.get("require_preview_frame")
                and not any(frame.get("sketch_preview_visible") is True
                            for frame in presented)):
            failures.append(
                f"{evidence['semantic_id']}: drag preview reached no presented frame"
            )
        if (evidence.get("forbid_preview_frame")
                and any(frame.get("sketch_preview_visible") is True
                        for frame in presented)):
            failures.append(
                f"{evidence['semantic_id']}: unwanted preview reached a presented frame"
            )
        preview_image = receipt.get("preview_image")
        preview_semantic = receipt.get("preview_semantic")
        if evidence.get("require_preview_image"):
            if (not isinstance(preview_image, str)
                    or not (output / preview_image).is_file()):
                failures.append(
                    f"{evidence['semantic_id']}: full-screen pointer preview is missing"
                )
            if (not isinstance(preview_semantic, str)
                    or not (output / preview_semantic).is_file()):
                failures.append(
                    f"{evidence['semantic_id']}: pointer preview semantics are missing"
                )
        if (evidence.get("require_native_scene")
                and receipt.get("settled_native_scene_current") is not True):
            failures.append(
                f"{evidence['semantic_id']}: native scene was not current at settlement"
            )
    preview_receipts = [
        receipt for receipt in action_receipts
        if isinstance(receipt.get("preview_image"), str)
    ]
    expected_measurements = scenario.get("expected_preview_measurements")
    if expected_measurements is not None and len(preview_receipts) != 1:
        failures.append("scenario did not produce exactly one pointer preview")
    for receipt in preview_receipts:
        preview_path = output / receipt["preview_image"]
        semantic_name = receipt.get("preview_semantic")
        if not preview_path.is_file() or not isinstance(semantic_name, str):
            continue
        preview_semantic_path = output / semantic_name
        if not preview_semantic_path.is_file():
            continue
        if os.name == "posix":
            for path in (preview_path, preview_semantic_path):
                if stat.S_IMODE(path.stat().st_mode) & 0o077:
                    failures.append(
                        f"{path.name} is accessible outside its owner")
        try:
            preview_width, preview_height, preview_colors, _, _ = png_pixels(
                preview_path)
            preview_semantic = json.loads(
                preview_semantic_path.read_text(encoding="utf-8"))
        except (OSError, ValueError, zlib.error, json.JSONDecodeError) as error:
            failures.append(f"invalid pointer preview evidence: {error}")
            continue
        if (preview_width, preview_height) != (image_width, image_height):
            failures.append("pointer preview is not a full-window capture")
        if preview_colors < 8:
            failures.append("pointer preview has insufficient visual content")
        if (preview_semantic.get("schema") != "kearne.pointer-preview/v1"
                or preview_semantic.get("frame")
                != receipt.get("preview_frame")
                or preview_semantic.get("ui_generation")
                != receipt.get("preview_generation")):
            failures.append("pointer preview semantic correlation differs")
        preview_nodes = preview_semantic.get("nodes")
        if not isinstance(preview_nodes, list):
            failures.append("pointer preview semantic nodes are missing")
            continue
        measurements = [
            node for node in preview_nodes
            if str(node.get("id", "")).startswith(
                "sketch.preview.measurement.") and node.get("visible")
        ]
        if (expected_measurements is not None
                and len(measurements) != expected_measurements):
            failures.append(
                "pointer preview live measurement count differs from scenario")
        for node in measurements:
            failures.extend(validate_bounds(node, image_width, image_height))
        for index, left in enumerate(measurements):
            for right in measurements[index + 1:]:
                if bounds_overlap(left["physical_bounds"],
                                  right["physical_bounds"]):
                    failures.append(
                        "pointer preview live measurement labels overlap")
    for evidence in scenario.get("revision_evidence", []):
        matches = [
            receipt for receipt in action_receipts
            if receipt.get("semantic_id") == evidence["semantic_id"]
            and receipt.get("action") == evidence["action"]
        ]
        references = [
            receipt for receipt in action_receipts
            if receipt.get("semantic_id") == evidence["reference_semantic_id"]
            and receipt.get("action") == evidence["reference_action"]
        ]
        occurrence = evidence.get("occurrence", 1)
        reference_occurrence = evidence.get("reference_occurrence", 1)
        if (not isinstance(occurrence, int) or occurrence < 1
                or not isinstance(reference_occurrence, int)
                or reference_occurrence < 1
                or len(matches) < occurrence
                or len(references) < reference_occurrence):
            failures.append("revision evidence target is unavailable")
            continue
        match_revision = matches[occurrence - 1].get(
            "settled_project_revision"
        )
        reference_revision = references[reference_occurrence - 1].get(
            "settled_project_revision"
        )
        if (not isinstance(match_revision, str) or not match_revision
                or not isinstance(reference_revision, str)
                or not reference_revision):
            failures.append("revision evidence has no settled revision")
            continue
        matches_reference = match_revision == reference_revision
        relation = evidence.get("relation")
        if ((relation == "same" and not matches_reference)
                or (relation == "different" and matches_reference)
                or relation not in ("same", "different")):
            failures.append(
                f"{evidence['semantic_id']}: project revision relation failed"
            )
    expected_theme = scenario.get("expected_theme", scenario.get("theme", "light"))
    if metadata.get("theme_id") != expected_theme or semantic.get("theme_id") != expected_theme:
        failures.append("captured theme differs from scenario")
    if semantic.get("surface_id") != scenario["surface"]:
        failures.append("captured surface differs from scenario")
    if semantic.get("workspace_id") != scenario.get("workspace", "model"):
        failures.append("captured workspace differs from scenario")
    if expected_source := scenario.get("expected_source_contains"):
        source_snapshot = semantic.get("source_snapshot")
        if (not isinstance(source_snapshot, dict)
                or not isinstance(source_snapshot.get("source"), str)
                or expected_source not in source_snapshot["source"]
                or not source_snapshot.get("path")
                or not source_snapshot.get("revision")):
            failures.append("canonical source snapshot differs from scenario")
    if scenario.get("expected_sketch_selection") == "point":
        selection = semantic.get("sketch_selection")
        if (not isinstance(selection, list) or len(selection) != 1
                or not isinstance(selection[0], dict)
                or not selection[0].get("entity_id")
                or not selection[0].get("point_key")):
            failures.append("captured Sketch point selection is missing")
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
    required.update(scenario.get("expected_values", {}))
    required.update(scenario.get("expected_contains", {}))
    required.update(scenario.get("expected_visible", []))
    required.update(scenario.get("expected_hidden", []))
    required.update(scenario.get("expected_disabled", []))
    if scenario.get("surface") == "editor":
        required.update({"project.length_unit", "viewport.grid",
                         "viewport.grid.toggle", "viewport.grid_snap.toggle",
                         "project.save"})
    if scenario.get("surface") == "projects":
        required.update(PROJECT_ROUTES)
        required.update({"projects.open", "projects.recovery"})
    if scenario.get("settings_category"):
        required.add(f"settings.{scenario['settings_category']}")
    missing = sorted(required - id_set)
    if missing:
        failures.append(f"missing required semantic nodes: {missing}")
    unexpected = sorted(set(scenario.get("expected_absent", [])) & id_set)
    if unexpected:
        failures.append(f"unexpected semantic nodes are present: {unexpected}")
    nodes_by_id = {node.get("id"): node for node in nodes}
    if scenario.get("viewport_visual"):
        viewport = nodes_by_id.get("viewport.primary", {})
        bounds = viewport.get("physical_bounds")
        if (not isinstance(bounds, list) or len(bounds) != 4
                or region_color_count(rows, channels, bounds) < 12):
            failures.append("viewport has no rendered visual content")
    for semantic_id in scenario.get("expected_visible", []):
        node = nodes_by_id.get(semantic_id)
        if node and not node.get("visible"):
            failures.append(f"{semantic_id}: expected visible node is hidden")
    for semantic_id in scenario.get("expected_hidden", []):
        node = nodes_by_id.get(semantic_id)
        if node and node.get("visible"):
            failures.append(f"{semantic_id}: expected hidden node is visible")
    for semantic_id in scenario.get("expected_disabled", []):
        node = nodes_by_id.get(semantic_id)
        if node and node.get("enabled"):
            failures.append(f"{semantic_id}: expected disabled node is enabled")
    if (scenario.get("surface") == "projects"
            and nodes_by_id.get("projects.open", {}).get("enabled")):
        failures.append("projects.open is enabled without a file-opening adapter")
    if (scenario.get("surface") == "editor"
            and nodes_by_id.get("project.save", {}).get("enabled")):
        failures.append("project.save is enabled without a persistence adapter")
    for semantic_id, expected in scenario.get("expected_values", {}).items():
        node = nodes_by_id.get(semantic_id)
        if node and node.get("value") != expected:
            failures.append(f"{semantic_id}: value differs from scenario")
    for semantic_id, expected in scenario.get("expected_contains", {}).items():
        node = nodes_by_id.get(semantic_id)
        if node and expected not in str(node.get("value", "")):
            failures.append(f"{semantic_id}: value does not contain {expected}")
    for semantic_id, expected in scenario.get("expected_bounds", {}).items():
        node = nodes_by_id.get(semantic_id)
        if not node:
            continue
        actual = node.get("logical_bounds")
        if not isinstance(actual, list) or len(actual) != 4:
            failures.append(f"{semantic_id}: logical bounds are missing")
            continue
        for index, expected_value in enumerate(expected):
            if expected_value is not None and not math.isclose(
                    actual[index], expected_value, abs_tol=1.0):
                failures.append(f"{semantic_id}: logical bounds differ from scenario")
                break
    if scenario.get("expected_preferences"):
        preference_files = list(profile_for(output, scenario).rglob("preferences.json"))
        if len(preference_files) != 1:
            failures.append("expected one persisted user-preference document")
        else:
            preference_file = preference_files[0]
            persisted = json.loads(preference_file.read_text(encoding="utf-8"))
            if persisted.get("schema") != "kearne.user-preferences/v1":
                failures.append("user-preference schema mismatch")
            values = persisted.get("values", {})
            for preference_id, expected in scenario["expected_preferences"].items():
                if values.get(preference_id) != expected:
                    failures.append(f"{preference_id}: persisted value differs")
            if os.name == "posix" and stat.S_IMODE(preference_file.stat().st_mode) & 0o077:
                failures.append("user preferences are accessible outside their owner")
    if scenario.get("expected_workspace"):
        workspace_files = list(profile_for(output, scenario).rglob("workspace-state.json"))
        if len(workspace_files) != 1:
            failures.append("expected one persisted workspace-state document")
        else:
            workspace_file = workspace_files[0]
            persisted = json.loads(workspace_file.read_text(encoding="utf-8"))
            if persisted.get("schema") != "kearne.workspace-state/v2":
                failures.append("workspace-state schema mismatch")
            layout = persisted.get("workspaces", {}).get("model", {})
            for field, expected in scenario["expected_workspace"].items():
                if layout.get(field) != expected:
                    failures.append(f"workspace {field}: persisted value differs")
            if os.name == "posix" and stat.S_IMODE(workspace_file.stat().st_mode) & 0o077:
                failures.append("workspace state is accessible outside its owner")
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
        elif node["actions"] and node.get("action_handler") is not True:
            failures.append(f"{node_id}: declared actions have no handler")
        accessibility = node.get("accessibility")
        expected_role = ACCESSIBLE_ROLES.get(node.get("role"))
        needs_accessibility = bool(
            node.get("visible")
            and expected_role
            and (node.get("role") in ACTIONABLE_ROLES
                 or node.get("role") == "canvas"
                 or node.get("actions"))
        )
        if needs_accessibility:
            if not isinstance(accessibility, dict) or not accessibility.get("present"):
                failures.append(f"{node_id}: missing native accessibility interface")
            else:
                if accessibility.get("identifier") != node_id:
                    failures.append(f"{node_id}: native accessibility ID differs")
                if accessibility.get("name") != node.get("name"):
                    failures.append(f"{node_id}: native accessible name differs")
                if accessibility.get("role") != expected_role:
                    failures.append(
                        f"{node_id}: native role {accessibility.get('role')} is not {expected_role}"
                    )
                state = accessibility.get("state")
                if not isinstance(state, dict):
                    failures.append(f"{node_id}: native accessibility state is missing")
                elif node.get("enabled") and not state.get("focusable"):
                    failures.append(f"{node_id}: enabled control is not natively focusable")
                if node.get("enabled") and state.get("disabled"):
                    failures.append(f"{node_id}: enabled control is natively disabled")
                if node.get("visible") and state.get("invisible"):
                    failures.append(f"{node_id}: visible control is natively invisible")
        parent = node.get("parent_id")
        if parent and parent not in id_set:
            failures.append(f"{node_id}: unknown semantic parent {parent}")
        failures.extend(validate_bounds(node, image_width, image_height))
    return failures


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--executable", required=True, type=Path)
    parser.add_argument(
        "--mode", choices=("smoke", "full", "proof"), default="smoke")
    parser.add_argument("--output", type=Path)
    parser.add_argument(
        "--scenario", action="append", metavar="GLOB",
        help="Run only scenario names matching this repeatable glob.",
    )
    parser.add_argument(
        "--graphics", choices=("platform", "software-opengl", "vulkan"),
        default="software-opengl" if sys.platform.startswith("linux") else "platform",
        help="Select the deterministic Linux graphics backend.",
    )
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
    if arguments.scenario:
        matrix = [
            scenario for scenario in matrix
            if any(fnmatchcase(str(scenario["name"]), pattern)
                   for pattern in arguments.scenario)
        ]
        if not matrix:
            parser.error("--scenario patterns matched no scenarios")
    groups: dict[str, list[tuple[int, dict[str, object]]]] = {}
    for index, scenario in enumerate(matrix, 1):
        profile = str(scenario.get("profile", scenario["name"]))
        groups.setdefault(profile, []).append((index, scenario))

    def run_group(entries: list[tuple[int, dict[str, object]]],
                  display: str | None) -> list[tuple[int, str, list[str]]]:
        outcomes = []
        for index, scenario in entries:
            name = str(scenario["name"])
            output = root / name
            output.mkdir()
            try:
                launch(executable, scenario, output, display,
                       arguments.graphics)
                failures = validate(output, scenario, arguments.graphics)
            except (OSError, subprocess.SubprocessError, RuntimeError) as error:
                failures = [str(error)]
            outcomes.append((index, name, failures))
        return outcomes

    default_workers = "1" if arguments.mode == "proof" else "4"
    workers = max(1, int(os.environ.get("KEARNE_OBSERVE_WORKERS",
                                       default_workers)))
    outcomes: list[tuple[int, str, list[str]]] = []
    with VirtualDisplay() as display:
        with ThreadPoolExecutor(max_workers=min(workers, len(groups))) as executor:
            futures = [executor.submit(run_group, group, display)
                       for group in groups.values()]
            for future in as_completed(futures):
                outcomes.extend(future.result())
    for index, name, failures in sorted(outcomes):
        if failures:
            all_failures.extend(f"{name}: {failure}" for failure in failures)
            print(f"[{index:03}/{len(matrix):03}] FAIL {name}")
        else:
            print(f"[{index:03}/{len(matrix):03}] PASS {name}")
    light = root / "settings-appearance" / "application-session.png"
    selected_dark = root / "theme-dark-select" / "application-session.png"
    restarted_dark = root / "theme-dark-restart" / "application-session.png"
    if all(path.is_file() for path in (light, selected_dark, restarted_dark)):
        if png_difference(light, selected_dark) < 0.04:
            all_failures.append("theme metamorphic check: light and dark renders match")
        if png_difference(selected_dark, restarted_dark) > 0.01:
            all_failures.append("theme metamorphic check: persisted dark render changed after restart")
    empty_sketch = root / "proof-sketch-empty" / "application-session.png"
    rectangle_sketch = root / "proof-sketch-rectangle" / "application-session.png"
    if empty_sketch.is_file() and rectangle_sketch.is_file():
        if png_difference(empty_sketch, rectangle_sketch) < 0.0001:
            all_failures.append(
                "sketch proof metamorphic check: evaluated rectangle did not change the render")
    if all_failures:
        print("\n".join(all_failures), file=sys.stderr)
        if temporary:
            print(f"artifacts retained only for this process: {root}", file=sys.stderr)
        return 1
    print(f"verified {len(matrix)} generated observation scenarios")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
