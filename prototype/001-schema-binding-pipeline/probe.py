#!/usr/bin/env python3
"""Exercise one semantic scenario through every generated boundary."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import time
from typing import Any, BinaryIO


MAX_FRAME_BYTES = 64 * 1024


def read_exact(stream: BinaryIO, size: int) -> bytes:
    value = stream.read(size)
    if len(value) != size:
        raise RuntimeError(f"short frame: expected {size} bytes, received {len(value)}")
    return value


def exchange_binary(process: subprocess.Popen[bytes], request: Any, response_type: Any) -> Any:
    payload = request.SerializeToString()
    assert process.stdin is not None
    assert process.stdout is not None
    process.stdin.write(struct.pack(">I", len(payload)) + payload)
    process.stdin.flush()
    size = struct.unpack(">I", read_exact(process.stdout, 4))[0]
    response = response_type()
    response.ParseFromString(read_exact(process.stdout, size))
    return response


def exchange_json(process: subprocess.Popen[str], request: dict[str, Any]) -> dict[str, Any]:
    assert process.stdin is not None
    assert process.stdout is not None
    process.stdin.write(json.dumps(request, separators=(",", ":")) + "\n")
    process.stdin.flush()
    response = process.stdout.readline()
    if not response:
        raise RuntimeError("JSON server closed without a response")
    return json.loads(response)


def close_process(process: subprocess.Popen[Any]) -> tuple[int, str]:
    if process.stdin:
        process.stdin.close()
    return_code = process.wait(timeout=10)
    stderr = process.stderr.read() if process.stderr else ""
    return return_code, stderr


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(128 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def assert_result(result: dict[str, Any], adapter: str) -> None:
    expected = {"display_name": "Mounting Plate", "revision_id": "revision-0001"}
    actual = {key: result[key] for key in expected}
    if actual != expected:
        raise RuntimeError(f"{adapter} semantic result differs: {actual!r}")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--build-dir", type=Path, required=True)
    parser.add_argument("--output", type=Path)
    arguments = parser.parse_args()
    build_dir = arguments.build_dir.resolve()
    generated_dir = build_dir / "generated"
    executable = build_dir / "kearne-schema-probe"
    compiler = build_dir / "kearne-schema-compiler"
    qt_executable = build_dir / "kearne-schema-qt-adapter"
    sys.path.insert(0, str(generated_dir))

    import api_pb2  # type: ignore[import-not-found]
    import google.protobuf  # type: ignore[import-not-found]
    from google.protobuf import json_format  # type: ignore[import-not-found]

    started = time.perf_counter()
    cpp_result = json.loads(
        subprocess.run(
            [executable, "--self-test"], check=True, capture_output=True, text=True
        ).stdout
    )
    assert_result(cpp_result, "cpp-in-process")
    qt_wire_result = json.loads(
        subprocess.run(
            [qt_executable], check=True, capture_output=True, text=True
        ).stdout
    )
    qt_result = {
        "display_name": qt_wire_result["displayName"],
        "revision_id": qt_wire_result["revisionId"],
    }
    assert_result(qt_result, "qt-qvariant-adapter")

    def rename_request(arguments_dict: dict[str, str]) -> Any:
        command = api_pb2.RenameProjectRequest()
        json_format.ParseDict(arguments_dict, command)
        request = api_pb2.RpcRequest()
        request.command.request_id = "request-0001"
        request.command.base_revision_id = "revision-0000"
        request.command.rename_project.CopyFrom(command)
        return request

    def metadata_query(revision_id: str) -> Any:
        request = api_pb2.RpcRequest()
        request.query.revision_id = revision_id
        request.query.limit = 1
        request.query.get_project_metadata.project_id = "project-01"
        return request

    binary = subprocess.Popen(
        [executable, "--binary-server"],
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
    binary_command = exchange_binary(
        binary,
        rename_request({"projectId": "project-01", "displayName": "Mounting Plate"}),
        api_pb2.RpcResponse,
    )
    binary_query = exchange_binary(
        binary, metadata_query(binary_command.command.revision_id), api_pb2.RpcResponse
    )
    binary_code, binary_stderr = close_process(binary)
    if binary_code != 0 or binary_stderr:
        raise RuntimeError(f"binary server failed ({binary_code}): {binary_stderr}")
    binary_result = {
        "display_name": binary_query.query.display_name,
        "revision_id": binary_query.query.observed_revision_id,
    }
    assert_result(binary_result, "generated-python-binary-ipc")

    cli = subprocess.Popen(
        [executable, "--json-server"],
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
    )
    cli_command = exchange_json(
        cli,
        {
            "command": {
                "requestId": "request-0001",
                "baseRevisionId": "revision-0000",
                "renameProject": {
                    "projectId": "project-01",
                    "displayName": "Mounting Plate",
                },
            }
        },
    )
    cli_query = exchange_json(
        cli,
        {
            "query": {
                "revisionId": cli_command["command"]["revision_id"],
                "limit": 1,
                "getProjectMetadata": {"projectId": "project-01"},
            }
        },
    )
    invalid_json = exchange_json(
        cli,
        {
            "command": {
                "requestId": "request-0002",
                "baseRevisionId": "revision-0001",
                "renameProject": {
                    "projectId": "project-01",
                    "displayName": "Rejected",
                    "unknownProperty": True,
                },
            }
        },
    )
    cli_code, cli_stderr = close_process(cli)
    if cli_code != 0 or cli_stderr:
        raise RuntimeError(f"JSON server failed ({cli_code}): {cli_stderr}")
    cli_result = {
        "display_name": cli_query["query"]["display_name"],
        "revision_id": cli_query["query"]["observed_revision_id"],
    }
    assert_result(cli_result, "cli-json")
    if invalid_json.get("transport_diagnostic", {}).get("code") != "wire.invalid_json":
        raise RuntimeError("unknown JSON property was not rejected")

    tool_path = generated_dir / "metadata" / "project.rename.tool.json"
    tool = json.loads(tool_path.read_text(encoding="utf-8"))
    schema = tool["inputSchema"]
    if (
        tool["name"] != "project.rename"
        or schema["additionalProperties"] is not False
        or set(schema["required"]) != {"projectId", "displayName"}
        or schema["properties"]["displayName"]["maxLength"] != 80
    ):
        raise RuntimeError("generated AI tool schema lost descriptor constraints")
    ai_arguments = {"projectId": "project-01", "displayName": "Mounting Plate"}
    ai = subprocess.Popen(
        [executable, "--binary-server"],
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
    ai_command = exchange_binary(ai, rename_request(ai_arguments), api_pb2.RpcResponse)
    ai_query = exchange_binary(
        ai, metadata_query(ai_command.command.revision_id), api_pb2.RpcResponse
    )
    ai_code, ai_stderr = close_process(ai)
    if ai_code != 0 or ai_stderr:
        raise RuntimeError(f"AI adapter server failed ({ai_code}): {ai_stderr}")
    ai_result = {
        "display_name": ai_query.query.display_name,
        "revision_id": ai_query.query.observed_revision_id,
    }
    assert_result(ai_result, "generated-ai-tool")

    oversized = subprocess.run(
        [executable, "--binary-server"],
        input=struct.pack(">I", MAX_FRAME_BYTES + 1),
        capture_output=True,
    )
    if oversized.returncode != 4 or b"exceeds negotiated inline limit" not in oversized.stderr:
        raise RuntimeError("oversized frame was not rejected before payload allocation")

    oversized_json_process = subprocess.Popen(
        [executable, "--json-server"],
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
    )
    assert oversized_json_process.stdin is not None
    assert oversized_json_process.stdout is not None
    oversized_json_process.stdin.write("x" * (MAX_FRAME_BYTES + 1) + "\n")
    oversized_json_process.stdin.flush()
    oversized_json = json.loads(oversized_json_process.stdout.readline())
    oversized_json_code, oversized_json_stderr = close_process(oversized_json_process)
    if (
        oversized_json_code != 0
        or oversized_json_stderr
        or oversized_json.get("transport_diagnostic", {}).get("code")
        != "wire.frame_too_large"
    ):
        raise RuntimeError("oversized JSON line was not rejected at the transport boundary")

    with tempfile.TemporaryDirectory(prefix="kearne-schema-determinism-") as temporary:
        first = Path(temporary) / "first"
        second = Path(temporary) / "second"
        for output in (first, second):
            subprocess.run(
                [compiler, "--output", output], check=True, capture_output=True, text=True
            )
        first_manifest = {
            path.relative_to(first): path.read_bytes() for path in sorted(first.iterdir())
        }
        second_manifest = {
            path.relative_to(second): path.read_bytes() for path in sorted(second.iterdir())
        }
        if first_manifest != second_manifest:
            raise RuntimeError("metadata generation is not deterministic")

    measured_paths = [
        generated_dir / "api.pb.cc",
        generated_dir / "api.pb.h",
        generated_dir / "api_pb2.py",
        generated_dir / "kearne-schema.pb",
        generated_dir / "metadata" / "command-registry.json",
        tool_path,
        executable,
        compiler,
        qt_executable,
    ]
    result = {
        "scenario": "project.rename_then_get_metadata",
        "semantic_result": ai_result,
        "adapters": [
            "cpp-in-process",
            "qt-qvariant-adapter",
            "generated-python-binary-ipc",
            "cli-json",
            "generated-ai-tool",
        ],
        "one_engine_validator": True,
        "unknown_json_property_rejected": True,
        "metadata_generation_deterministic": True,
        "oversized_frame_rejected_before_payload": True,
        "oversized_json_line_rejected_at_boundary": True,
        "max_frame_bytes": MAX_FRAME_BYTES,
        "protobuf": {
            "compiler_numeric_version": subprocess.run(
                [executable, "--version"], check=True, capture_output=True, text=True
            ).stdout.strip(),
            "python_runtime_version": google.protobuf.__version__,
        },
        "artifacts": {
            str(path.relative_to(build_dir)): {
                "bytes": path.stat().st_size,
                "sha256": sha256(path),
            }
            for path in measured_paths
        },
        "elapsed_ms": round((time.perf_counter() - started) * 1000, 3),
    }
    encoded = json.dumps(result, indent=2, sort_keys=True) + "\n"
    if arguments.output:
        arguments.output.parent.mkdir(parents=True, exist_ok=True)
        arguments.output.write_text(encoded, encoding="utf-8")
    sys.stdout.write(encoded)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
