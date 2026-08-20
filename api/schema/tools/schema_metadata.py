#!/usr/bin/env python3
"""Compile Kearne protobuf descriptors into bounded adapter metadata."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import sys
from typing import Any, Iterator

from google.protobuf import descriptor_pb2


MAX_FIELDS = 64
MAX_DEPTH = 16
MAX_OUTPUT_BYTES = 1024 * 1024


def messages(
    file: descriptor_pb2.FileDescriptorProto,
) -> Iterator[tuple[str, descriptor_pb2.DescriptorProto]]:
    def walk(
        prefix: str, values: list[descriptor_pb2.DescriptorProto]
    ) -> Iterator[tuple[str, descriptor_pb2.DescriptorProto]]:
        for value in values:
            name = f"{prefix}.{value.name}"
            yield name, value
            yield from walk(name, value.nested_type)

    yield from walk(file.package, file.message_type)


def enums(
    file: descriptor_pb2.FileDescriptorProto,
) -> Iterator[tuple[str, descriptor_pb2.EnumDescriptorProto]]:
    for value in file.enum_type:
        yield f"{file.package}.{value.name}", value

    def walk(
        prefix: str, values: list[descriptor_pb2.DescriptorProto]
    ) -> Iterator[tuple[str, descriptor_pb2.EnumDescriptorProto]]:
        for message in values:
            name = f"{prefix}.{message.name}"
            for value in message.enum_type:
                yield f"{name}.{value.name}", value
            yield from walk(name, message.nested_type)

    yield from walk(file.package, file.message_type)


def scalar_schema(field: descriptor_pb2.FieldDescriptorProto) -> dict[str, Any]:
    integers = {
        field.TYPE_INT32,
        field.TYPE_INT64,
        field.TYPE_UINT32,
        field.TYPE_UINT64,
        field.TYPE_SINT32,
        field.TYPE_SINT64,
        field.TYPE_FIXED32,
        field.TYPE_FIXED64,
        field.TYPE_SFIXED32,
        field.TYPE_SFIXED64,
    }
    if field.type in integers:
        return {"type": "integer"}
    if field.type in {field.TYPE_FLOAT, field.TYPE_DOUBLE}:
        return {"type": "number"}
    if field.type == field.TYPE_BOOL:
        return {"type": "boolean"}
    if field.type == field.TYPE_BYTES:
        return {"type": "string", "contentEncoding": "base64"}
    return {"type": "string"}


def compile_schema(
    message: descriptor_pb2.DescriptorProto,
    types: dict[str, descriptor_pb2.DescriptorProto],
    enum_types: dict[str, descriptor_pb2.EnumDescriptorProto],
    options: Any,
    depth: int = 0,
) -> dict[str, Any]:
    if depth > MAX_DEPTH or len(message.field) > MAX_FIELDS:
        raise ValueError("schema exceeds compiler limits")
    properties: dict[str, Any] = {}
    required: list[str] = []
    for field in message.field:
        name = field.json_name or field.name
        rules = (
            field.options.Extensions[options.field_rules]
            if field.options.HasExtension(options.field_rules)
            else None
        )
        semantic = rules.semantic_type if rules else ""
        if semantic.endswith("Id") and semantic != "RevisionId":
            schema: dict[str, Any] = {
                "type": "string",
                "format": "uuid",
                "pattern": "^[0-9a-f]{8}-[0-9a-f]{4}-7[0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}$",
            }
        elif semantic == "RevisionId":
            schema = {
                "type": "string",
                "pattern": "^[a-z0-9-]{1,32}:[0-9a-f]{64}$",
            }
        elif field.type == field.TYPE_MESSAGE:
            schema = compile_schema(
                types[field.type_name.removeprefix(".")],
                types,
                enum_types,
                options,
                depth + 1,
            )
        elif field.type == field.TYPE_ENUM:
            descriptor = enum_types[field.type_name.removeprefix(".")]
            values = [value.name for value in descriptor.value]
            if rules and rules.disallow_default:
                values = values[1:]
            schema = {"type": "string", "enum": values}
        else:
            schema = scalar_schema(field)
        if rules:
            if rules.min_length:
                schema["minLength"] = (
                    4 * ((rules.min_length + 2) // 3)
                    if field.type == field.TYPE_BYTES
                    else rules.min_length
                )
            if rules.max_length:
                schema["maxLength"] = (
                    4 * ((rules.max_length + 2) // 3)
                    if field.type == field.TYPE_BYTES
                    else rules.max_length
                )
            if rules.has_numeric_range:
                schema["minimum"] = rules.minimum
                schema["maximum"] = rules.maximum
            if rules.required:
                required.append(name)
        if field.label == field.LABEL_REPEATED:
            item_schema = schema
            schema = {"type": "array", "items": item_schema}
            if rules and rules.max_items:
                schema["maxItems"] = rules.max_items
        properties[name] = schema
    result: dict[str, Any] = {
        "type": "object",
        "additionalProperties": False,
        "properties": properties,
    }
    if required:
        result["required"] = required
    return result


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--descriptor", type=Path, required=True)
    parser.add_argument("--python-root", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    arguments = parser.parse_args()
    sys.path.insert(0, str(arguments.python_root))
    from kearne.api.v1 import options_pb2  # type: ignore[import-not-found]

    descriptor_set = descriptor_pb2.FileDescriptorSet.FromString(
        arguments.descriptor.read_bytes()
    )
    types = {
        name: message
        for file in descriptor_set.file
        for name, message in messages(file)
    }
    enum_types = {
        name: value
        for file in descriptor_set.file
        for name, value in enums(file)
    }
    commands = []
    for name, message in sorted(types.items()):
        if not message.options.HasExtension(options_pb2.message_rules):
            continue
        rules = message.options.Extensions[options_pb2.message_rules]
        if rules.surface != options_pb2.SURFACE_KIND_COMMAND:
            continue
        commands.append(
            {
                "name": rules.stable_name,
                "schemaVersion": rules.schema_version,
                "permission": rules.permission,
                "wireType": name,
                "inputSchema": compile_schema(
                    message, types, enum_types, options_pb2
                ),
            }
        )
    result = {
        "schemaVersion": 1,
        "commands": sorted(commands, key=lambda item: item["name"]),
    }
    encoded = (json.dumps(result, indent=2, sort_keys=True) + "\n").encode()
    if len(encoded) > MAX_OUTPUT_BYTES:
        raise ValueError("metadata exceeds compiler limit")
    arguments.output.parent.mkdir(parents=True, exist_ok=True)
    arguments.output.write_bytes(encoded)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
