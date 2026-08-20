#!/usr/bin/env python3
"""Fault-controllable framed worker used by the transport contract."""

from __future__ import annotations

import sys

MAXIMUM_FRAME = 4096


def read_exact(size: int) -> bytes:
    result = bytearray()
    while len(result) < size:
        chunk = sys.stdin.buffer.read(size - len(result))
        if not chunk:
            raise EOFError
        result.extend(chunk)
    return bytes(result)


while header := sys.stdin.buffer.read(4):
    if len(header) != 4:
        break
    size = int.from_bytes(header, "big")
    if not 1 <= size <= MAXIMUM_FRAME:
        break
    try:
        payload = read_exact(size)
    except EOFError:
        break
    if payload == b"exit":
        raise SystemExit(17)
    if payload == b"oversize":
        sys.stdout.buffer.write((MAXIMUM_FRAME + 1).to_bytes(4, "big"))
        sys.stdout.buffer.flush()
        continue
    if payload == b"zero":
        sys.stdout.buffer.write(bytes(4))
        sys.stdout.buffer.flush()
        continue
    if payload == b"log":
        sys.stderr.buffer.write(b"x" * (MAXIMUM_FRAME + 1))
        sys.stderr.buffer.flush()
    result = payload[::-1]
    sys.stdout.buffer.write(len(result).to_bytes(4, "big") + result)
    sys.stdout.buffer.flush()
