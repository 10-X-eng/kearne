"""Length-bounded binary worker stream protocol."""

from __future__ import annotations

from importlib import import_module
from types import ModuleType
from typing import BinaryIO, cast

from kearne._wire import (
    WireMessage as _WireMessage,
)
from kearne._wire import (
    child as _child,
)
from kearne._wire import (
    reject_unknown,
    uuid7_text,
)
from kearne._worker.sketch_source import process_transform

MAXIMUM_FRAME_BYTES = 132_096


class ProtocolError(ValueError):
    """The worker stream is malformed or unsupported."""


def _wire_module() -> ModuleType:
    try:
        return import_module("kearne.api.v1.worker_pb2")
    except ModuleNotFoundError as error:
        raise ProtocolError("generated worker bindings are unavailable") from error


def _message_type(name: str) -> type[_WireMessage]:
    return cast(type[_WireMessage], getattr(_wire_module(), name))


def _reject_unknown(message: _WireMessage) -> None:
    if reject_unknown(message):
        raise ProtocolError("worker envelope contains an unsupported field")


def _read_exact(stream: BinaryIO, size: int) -> bytes:
    result = bytearray()
    while len(result) < size:
        chunk = stream.read(size - len(result))
        if not chunk:
            raise ProtocolError("worker frame ended early")
        result.extend(chunk)
    return bytes(result)


def read_frame(stream: BinaryIO) -> bytes | None:
    """Read one four-byte big-endian length-prefixed frame."""
    first = stream.read(4)
    if not first:
        return None
    if len(first) != 4:
        first += _read_exact(stream, 4 - len(first))
    size = int.from_bytes(first, "big")
    if not 1 <= size <= MAXIMUM_FRAME_BYTES:
        raise ProtocolError("worker frame length is invalid")
    return _read_exact(stream, size)


def write_frame(stream: BinaryIO, payload: bytes) -> None:
    """Write and flush one bounded frame."""
    if not 1 <= len(payload) <= MAXIMUM_FRAME_BYTES:
        raise ProtocolError("worker result length is invalid")
    stream.write(len(payload).to_bytes(4, "big"))
    stream.write(payload)
    stream.flush()


def process_envelope(payload: bytes) -> bytes:
    """Parse one worker job and return its correlated terminal result."""
    if not 1 <= len(payload) <= MAXIMUM_FRAME_BYTES:
        raise ProtocolError("worker job length is invalid")
    envelope = _message_type("WorkerJobEnvelope")()
    try:
        consumed = envelope.ParseFromString(payload)
    except Exception as error:
        raise ProtocolError("worker job is not valid Protobuf") from error
    if consumed != len(payload) or envelope.ByteSize() > MAXIMUM_FRAME_BYTES:
        raise ProtocolError("worker job is not valid Protobuf")
    _reject_unknown(envelope)
    if not all(envelope.HasField(field) for field in ("worker_instance_id", "job_id")):
        raise ProtocolError("worker envelope identity is missing")
    try:
        uuid7_text(_child(envelope, "worker_instance_id"))
        uuid7_text(_child(envelope, "job_id"))
    except ValueError as error:
        raise ProtocolError("worker envelope identity is invalid") from error
    if envelope.WhichOneof("payload") != "sketch_source_transform":
        raise ProtocolError("worker payload is unsupported by this role")
    result = _message_type("WorkerResultEnvelope")()
    _child(result, "worker_instance_id").CopyFrom(
        _child(envelope, "worker_instance_id")
    )
    _child(result, "job_id").CopyFrom(_child(envelope, "job_id"))
    _child(result, "sketch_source_transform").CopyFrom(
        process_transform(_child(envelope, "sketch_source_transform"))
    )
    encoded = result.SerializeToString(deterministic=True)
    if not encoded or len(encoded) > MAXIMUM_FRAME_BYTES:
        raise ProtocolError("worker result exceeds its frame")
    return encoded


def serve(input_stream: BinaryIO, output_stream: BinaryIO) -> None:
    """Serve jobs until the coordinator closes the inherited pipe."""
    while (payload := read_frame(input_stream)) is not None:
        write_frame(output_stream, process_envelope(payload))


__all__ = [
    "MAXIMUM_FRAME_BYTES",
    "ProtocolError",
    "process_envelope",
    "read_frame",
    "serve",
    "write_frame",
]
