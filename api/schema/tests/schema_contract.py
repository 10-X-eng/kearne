#!/usr/bin/env python3
"""Verify generated Python bindings and metadata from the retained descriptor."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import subprocess
import sys
import tempfile

import google.protobuf
from google.protobuf import descriptor_pb2


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--descriptor", type=Path, required=True)
    parser.add_argument("--metadata", type=Path, required=True)
    parser.add_argument("--generator", type=Path, required=True)
    parser.add_argument("--python-root", type=Path, required=True)
    parser.add_argument("--protoc", type=Path, required=True)
    parser.add_argument("--lock", type=Path, required=True)
    arguments = parser.parse_args()

    expected = json.loads(arguments.lock.read_text(encoding="utf-8"))["dependencies"]
    protobuf = expected["protobuf"]
    assert google.protobuf.__version__ == protobuf["python_version"]
    version = subprocess.run(
        [arguments.protoc, "--version"], check=True, capture_output=True, text=True
    ).stdout.strip()
    assert version == f"libprotoc {protobuf['version']}"

    sys.path.insert(0, str(arguments.python_root))
    from kearne.api.v1 import engineering_pb2  # type: ignore[import-not-found]
    from kearne.api.v1 import options_pb2  # type: ignore[import-not-found]
    from kearne.api.v1 import sketch_pb2  # type: ignore[import-not-found]

    command = engineering_pb2.CommandEnvelope()
    command.ParseFromString(b"\xa2\x06\x03new")
    assert command.SerializeToString().endswith(b"\xa2\x06\x03new")
    assert sketch_pb2.SketchDefinition.DESCRIPTOR.full_name == (
        "kearne.api.v1.SketchDefinition"
    )

    descriptor_set = descriptor_pb2.FileDescriptorSet.FromString(
        arguments.descriptor.read_bytes()
    )
    commands = 0
    stable_names: set[str] = set()
    for file in descriptor_set.file:
        for message in file.message_type:
            if not message.options.HasExtension(options_pb2.message_rules):
                continue
            rules = message.options.Extensions[options_pb2.message_rules]
            assert rules.stable_name and rules.schema_version > 0
            assert rules.stable_name not in stable_names
            stable_names.add(rules.stable_name)
            commands += rules.surface == options_pb2.SURFACE_KIND_COMMAND

    metadata = json.loads(arguments.metadata.read_text(encoding="utf-8"))
    assert len(metadata["commands"]) == commands
    for command_metadata in metadata["commands"]:
        schema = command_metadata["inputSchema"]
        assert schema["additionalProperties"] is False
        assert command_metadata["permission"]
    commands_by_name = {command["name"]: command for command in metadata["commands"]}
    source_schema = commands_by_name["source.module.create"]["inputSchema"]
    assert set(source_schema["properties"]) == {"projectId", "path", "content"}
    content_schema = source_schema["properties"]["content"]
    assert content_schema["additionalProperties"] is False
    assert content_schema["properties"]["byteSize"]["maximum"] == 16_777_216

    with tempfile.TemporaryDirectory(prefix="kearne-schema-") as directory:
        outputs = [Path(directory) / name for name in ("first.json", "second.json")]
        for output in outputs:
            subprocess.run(
                [
                    sys.executable,
                    arguments.generator,
                    "--descriptor",
                    arguments.descriptor,
                    "--python-root",
                    arguments.python_root,
                    "--output",
                    output,
                ],
                check=True,
            )
        assert outputs[0].read_bytes() == outputs[1].read_bytes()
        assert outputs[0].read_bytes() == arguments.metadata.read_bytes()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
