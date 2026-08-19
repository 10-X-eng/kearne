#!/usr/bin/env python3
"""Minimal read-only MCP server for SPIKE-010."""

from __future__ import annotations

import json
import sys
from typing import Any


TOOL = {
    "name": "kearne.describe",
    "description": "Return the requested Kearne revision identity without mutation.",
    "inputSchema": {
        "type": "object",
        "properties": {"revision": {"type": "string", "minLength": 1}},
        "required": ["revision"],
        "additionalProperties": False,
    },
    "outputSchema": {
        "type": "object",
        "properties": {
            "protocolVersion": {"type": "string"},
            "revision": {"type": "string"},
            "source": {"const": "spike"},
        },
        "required": ["protocolVersion", "revision", "source"],
        "additionalProperties": False,
    },
}

PROTOCOL_VERSION: str | None = None
SUPPORTED_PROTOCOLS = ("2025-11-25", "2025-06-18")


def respond(request_id: Any, result: dict[str, Any]) -> None:
    print(
        json.dumps({"jsonrpc": "2.0", "id": request_id, "result": result}, separators=(",", ":")),
        flush=True,
    )


def error(request_id: Any, code: int, message: str) -> None:
    print(
        json.dumps(
            {"jsonrpc": "2.0", "id": request_id, "error": {"code": code, "message": message}},
            separators=(",", ":"),
        ),
        flush=True,
    )


def handle(message: dict[str, Any]) -> None:
    global PROTOCOL_VERSION
    method = message.get("method")
    request_id = message.get("id")
    params = message.get("params", {})

    if method == "initialize":
        version = params.get("protocolVersion")
        if not isinstance(version, str):
            error(request_id, -32602, "protocolVersion is required")
            return
        PROTOCOL_VERSION = version if version in SUPPORTED_PROTOCOLS else SUPPORTED_PROTOCOLS[0]
        respond(
            request_id,
            {
                "protocolVersion": PROTOCOL_VERSION,
                "capabilities": {"tools": {"listChanged": False}},
                "serverInfo": {"name": "kearne-spike-bridge", "version": "0.0.0"},
            },
        )
        return
    if method == "notifications/initialized" or method == "notifications/cancelled":
        return
    if method == "ping":
        respond(request_id, {})
        return
    if method == "tools/list":
        respond(request_id, {"tools": [TOOL]})
        return
    if method == "tools/call":
        arguments = params.get("arguments", {})
        revision = arguments.get("revision") if isinstance(arguments, dict) else None
        if (
            params.get("name") != TOOL["name"]
            or not isinstance(revision, str)
            or not revision
            or PROTOCOL_VERSION is None
        ):
            error(request_id, -32602, "valid kearne.describe arguments are required")
            return
        structured = {
            "protocolVersion": PROTOCOL_VERSION,
            "revision": revision,
            "source": "spike",
        }
        respond(
            request_id,
            {
                "content": [{"type": "text", "text": json.dumps(structured, separators=(",", ":"))}],
                "structuredContent": structured,
                "isError": False,
            },
        )
        return
    if request_id is not None:
        error(request_id, -32601, f"unknown method: {method}")


def main() -> int:
    for line in sys.stdin:
        try:
            message = json.loads(line)
        except json.JSONDecodeError:
            error(None, -32700, "invalid JSON")
            continue
        if isinstance(message, dict):
            handle(message)
        else:
            error(None, -32600, "message must be an object")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
