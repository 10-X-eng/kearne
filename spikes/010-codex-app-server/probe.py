#!/usr/bin/env python3
"""Probe a pinned Codex app-server without starting a model turn."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import selectors
import shutil
import subprocess
import sys
import tempfile
import time
from typing import Any


class ProbeError(RuntimeError):
    pass


def require(condition: bool, message: str) -> None:
    if not condition:
        raise ProbeError(message)


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def canonical_json_sha256(path: Path) -> str:
    value = json.loads(path.read_text(encoding="utf-8"))
    encoded = json.dumps(value, sort_keys=True, separators=(",", ":")).encode()
    return hashlib.sha256(encoded).hexdigest()


def schema_manifest(root: Path) -> dict[str, str]:
    return {
        path.relative_to(root).as_posix(): canonical_json_sha256(path)
        for path in sorted(root.rglob("*.json"))
    }


def codex_version(binary: str) -> str:
    completed = subprocess.run(
        [binary, "--version"], capture_output=True, text=True, check=False
    )
    require(completed.returncode == 0, completed.stderr.strip() or "codex --version failed")
    fields = completed.stdout.strip().split()
    require(len(fields) == 2 and fields[0] == "codex-cli", "unexpected version output")
    return fields[1]


def generate_schema(binary: str, target: Path, experimental: bool) -> dict[str, Any]:
    command = [binary, "app-server", "generate-json-schema"]
    if experimental:
        command.append("--experimental")
    command.extend(["--out", os.fspath(target)])
    started = time.perf_counter()
    completed = subprocess.run(command, capture_output=True, text=True, check=False)
    elapsed_ms = (time.perf_counter() - started) * 1000
    require(completed.returncode == 0, completed.stderr.strip() or "schema generation failed")
    require(not completed.stdout.strip(), "schema generator wrote unexpected stdout")

    files = sorted(path for path in target.rglob("*") if path.is_file())
    bundle = target / "codex_app_server_protocol.v2.schemas.json"
    require(bundle in files, "v2 schema bundle is missing")
    return {
        "elapsed_ms": round(elapsed_ms, 3),
        "file_count": len(files),
        "total_bytes": sum(path.stat().st_size for path in files),
        "v2_bundle_raw_sha256": sha256(bundle),
        "v2_bundle_canonical_sha256": canonical_json_sha256(bundle),
    }


def read_json(path: Path) -> dict[str, Any]:
    value = json.loads(path.read_text(encoding="utf-8"))
    require(isinstance(value, dict), f"{path.name} is not a JSON object")
    return value


def method_names(client_request: dict[str, Any]) -> set[str]:
    methods: set[str] = set()
    for variant in client_request.get("oneOf", []):
        values = variant.get("properties", {}).get("method", {}).get("enum", [])
        methods.update(value for value in values if isinstance(value, str))
    return methods


def verify_schema(stable: Path, experimental: Path) -> dict[str, Any]:
    initialize = read_json(stable / "v1" / "InitializeParams.json")
    notification = read_json(stable / "ClientNotification.json")
    requests = read_json(stable / "ClientRequest.json")
    stable_thread = read_json(stable / "v2" / "ThreadStartParams.json")
    experimental_thread = read_json(experimental / "v2" / "ThreadStartParams.json")
    turn = read_json(stable / "v2" / "TurnStartParams.json")

    require("clientInfo" in initialize.get("required", []), "initialize.clientInfo is not required")
    notification_text = json.dumps(notification, sort_keys=True)
    require('"initialized"' in notification_text, "initialized notification is missing")

    methods = method_names(requests)
    required_methods = {"initialize", "thread/start", "turn/start", "mcpServerStatus/list"}
    require(required_methods <= methods, f"missing methods: {sorted(required_methods - methods)}")

    stable_properties = stable_thread.get("properties", {})
    experimental_properties = experimental_thread.get("properties", {})
    require("dynamicTools" not in stable_properties, "dynamic tools leaked into stable schema")
    require("dynamicTools" in experimental_properties, "experimental dynamic tools are missing")

    require({"input", "threadId"} <= set(turn.get("required", [])), "turn inputs changed")
    user_input = turn.get("definitions", {}).get("UserInput", {})
    local_images = [
        variant
        for variant in user_input.get("oneOf", [])
        if variant.get("properties", {}).get("type", {}).get("enum") == ["localImage"]
    ]
    require(len(local_images) == 1, "localImage input is missing or ambiguous")
    require(
        {"path", "type"} <= set(local_images[0].get("required", [])),
        "localImage.path is not required",
    )
    return {
        "stable_method_count": len(methods),
        "local_image_input": True,
        "stable_mcp_status": True,
        "dynamic_tools_stable": False,
        "dynamic_tools_experimental": True,
    }


def send(stream: Any, message: dict[str, Any]) -> None:
    stream.write(json.dumps(message, separators=(",", ":")) + "\n")
    stream.flush()


def read_response(
    process: subprocess.Popen[str],
    selector: selectors.BaseSelector,
    request_id: str,
    timeout: float,
    notifications: list[str],
) -> dict[str, Any]:
    deadline = time.monotonic() + timeout
    while True:
        remaining = deadline - time.monotonic()
        require(remaining > 0, f"timeout waiting for {request_id}")
        require(bool(selector.select(remaining)), f"timeout waiting for {request_id}")
        line = process.stdout.readline() if process.stdout else ""
        require(bool(line), f"app-server closed stdout before {request_id}")
        try:
            message = json.loads(line)
        except json.JSONDecodeError as error:
            raise ProbeError("app-server stdout is not clean JSONL") from error
        require(isinstance(message, dict), "app-server emitted a non-object message")
        if message.get("id") == request_id:
            return message
        method = message.get("method")
        notifications.append(method if isinstance(method, str) else "<uncorrelated-response>")


def protocol_probe(binary: str, timeout: float) -> dict[str, Any]:
    with tempfile.TemporaryFile() as stderr:
        started = time.perf_counter()
        process = subprocess.Popen(
            [binary, "app-server", "--stdio", "--strict-config"],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=stderr,
            text=True,
            bufsize=1,
        )
        require(process.stdin is not None and process.stdout is not None, "pipe creation failed")
        selector = selectors.DefaultSelector()
        selector.register(process.stdout, selectors.EVENT_READ)
        notifications: list[str] = []

        send(process.stdin, {"method": "thread/loaded/list", "id": "preinit", "params": {}})
        preinit = read_response(process, selector, "preinit", timeout, notifications)

        initialize = {
            "method": "initialize",
            "id": "initialize",
            "params": {
                "clientInfo": {
                    "name": "kearne_spike",
                    "title": "Kearne SPIKE-010",
                    "version": "0.0.0",
                },
                "capabilities": {"experimentalApi": False},
            },
        }
        send(process.stdin, initialize)
        initialized = read_response(process, selector, "initialize", timeout, notifications)
        initialize_ms = (time.perf_counter() - started) * 1000
        send(process.stdin, {"method": "initialized", "params": {}})
        repeat = dict(initialize)
        repeat["id"] = "repeat"
        send(process.stdin, repeat)
        repeated = read_response(process, selector, "repeat", timeout, notifications)

        process.stdin.close()
        try:
            exit_code = process.wait(timeout=timeout)
        except subprocess.TimeoutExpired as error:
            process.terminate()
            process.wait(timeout=timeout)
            raise ProbeError("app-server did not stop after stdin EOF") from error
        stderr.seek(0)
        stderr_bytes = len(stderr.read())

    preinit_error = preinit.get("error", {})
    repeated_error = repeated.get("error", {})
    result = initialized.get("result", {})
    require(preinit_error.get("message") == "Not initialized", "pre-init request was not rejected")
    require(repeated_error.get("message") == "Already initialized", "repeated initialize was not rejected")
    require(exit_code == 0, f"app-server exited with {exit_code}")
    require(stderr_bytes == 0, "app-server wrote unexpected stderr")
    require(result.get("platformFamily") and result.get("platformOs"), "platform identity is missing")
    return {
        "initialize_ms": round(initialize_ms, 3),
        "platform_family": result["platformFamily"],
        "platform_os": result["platformOs"],
        "preinitialize_error_code": preinit_error.get("code"),
        "reinitialize_error_code": repeated_error.get("code"),
        "notification_methods": sorted(set(notifications)),
        "clean_jsonl_stdout": True,
        "clean_stderr": True,
        "graceful_eof_shutdown": True,
    }


def git_identity(repository: Path) -> dict[str, Any]:
    revision = subprocess.run(
        ["git", "rev-parse", "HEAD"], cwd=repository, capture_output=True, text=True, check=False
    )
    status = subprocess.run(
        ["git", "status", "--porcelain"], cwd=repository, capture_output=True, text=True, check=False
    )
    return {
        "revision": revision.stdout.strip() if revision.returncode == 0 else None,
        "dirty": bool(status.stdout.strip()) if status.returncode == 0 else None,
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--codex", default=shutil.which("codex"))
    parser.add_argument("--expected-version")
    parser.add_argument("--timeout", type=float, default=10.0)
    args = parser.parse_args()
    require(bool(args.codex), "codex executable was not found")

    binary = Path(args.codex).resolve()
    require(binary.is_file(), f"codex executable does not exist: {binary}")
    version = codex_version(os.fspath(binary))
    if args.expected_version:
        require(version == args.expected_version, f"expected Codex {args.expected_version}, found {version}")

    with tempfile.TemporaryDirectory(prefix="kearne-appserver-spike-") as temporary:
        root = Path(temporary)
        stable = root / "stable-a"
        stable_repeat = root / "stable-b"
        experimental = root / "experimental"
        stable.mkdir()
        stable_repeat.mkdir()
        experimental.mkdir()
        stable_metrics = generate_schema(os.fspath(binary), stable, False)
        stable_repeat_metrics = generate_schema(os.fspath(binary), stable_repeat, False)
        experimental_metrics = generate_schema(os.fspath(binary), experimental, True)
        stable_manifest = schema_manifest(stable)
        stable_repeat_manifest = schema_manifest(stable_repeat)
        require(stable_manifest == stable_repeat_manifest, "stable schemas changed semantically")
        schema = verify_schema(stable, experimental)
        determinism = {
            "semantic_equal": True,
            "raw_v2_bundle_equal": stable_metrics["v2_bundle_raw_sha256"]
            == stable_repeat_metrics["v2_bundle_raw_sha256"],
        }

    repository = Path(__file__).resolve().parents[2]
    evidence = {
        "evidence_schema": 1,
        "source": git_identity(repository),
        "environment": {
            "system": platform.system(),
            "machine": platform.machine(),
            "python": platform.python_version(),
        },
        "codex": {
            "version": version,
            "binary_sha256": sha256(binary),
        },
        "schemas": {
            "stable": stable_metrics,
            "stable_repeat": stable_repeat_metrics,
            "experimental": experimental_metrics,
            "determinism": determinism,
            "contract": schema,
        },
        "protocol": protocol_probe(os.fspath(binary), args.timeout),
    }
    print(json.dumps(evidence, indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ProbeError, subprocess.SubprocessError) as error:
        print(f"probe failed: {error}", file=sys.stderr)
        raise SystemExit(1)
