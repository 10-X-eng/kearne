"""Small typed helpers shared by dynamic Protobuf boundaries."""

from __future__ import annotations

from collections.abc import Iterable, Iterator, Mapping, Sequence
from typing import Protocol, cast
from uuid import UUID


class EnumValueDescriptor(Protocol):
    name: str
    number: int


class EnumDescriptor(Protocol):
    values_by_name: Mapping[str, EnumValueDescriptor]
    values_by_number: Mapping[int, EnumValueDescriptor]


class OneofDescriptor(Protocol):
    name: str
    fields: Sequence[FieldDescriptor]


class FieldDescriptor(Protocol):
    name: str
    full_name: str
    is_repeated: bool
    message_type: MessageDescriptor | None
    enum_type: EnumDescriptor
    containing_oneof: OneofDescriptor | None


class MessageDescriptor(Protocol):
    full_name: str
    fields: Sequence[FieldDescriptor]
    fields_by_name: Mapping[str, FieldDescriptor]
    oneofs: Sequence[OneofDescriptor]
    oneofs_by_name: Mapping[str, OneofDescriptor]


class WireMessage(Protocol):
    DESCRIPTOR: MessageDescriptor

    def ByteSize(self) -> int: ...

    def CopyFrom(self, other: WireMessage) -> None: ...

    def DiscardUnknownFields(self) -> None: ...

    def HasField(self, name: str) -> bool: ...

    def ParseFromString(self, payload: bytes) -> int: ...

    def SerializeToString(self, *, deterministic: bool = False) -> bytes: ...

    def WhichOneof(self, name: str) -> str | None: ...


class RepeatedComposite(Protocol):
    def add(self) -> WireMessage: ...

    def extend(self, values: Iterable[object]) -> None: ...

    def __iter__(self) -> Iterator[WireMessage]: ...

    def __len__(self) -> int: ...


def child(message: WireMessage, name: str) -> WireMessage:
    return cast(WireMessage, getattr(message, name))


def repeated(message: WireMessage, name: str) -> RepeatedComposite:
    return cast(RepeatedComposite, getattr(message, name))


def scalar(message: WireMessage, name: str) -> object:
    return getattr(message, name)


def set_scalar(message: WireMessage, name: str, value: object) -> None:
    setattr(message, name, value)


def reject_unknown(message: WireMessage) -> bool:
    known = type(message)()
    known.CopyFrom(message)
    known.DiscardUnknownFields()
    return known.SerializeToString(deterministic=True) != message.SerializeToString(
        deterministic=True
    )


def uuid7_text(message: WireMessage) -> str:
    raw = cast(bytes, scalar(message, "value"))
    if len(raw) != 16:
        raise ValueError("identifier has the wrong byte length")
    value = UUID(bytes=raw)
    if value.version != 7:
        raise ValueError("identifier is not UUIDv7")
    return str(value)


__all__ = [
    "EnumDescriptor",
    "EnumValueDescriptor",
    "FieldDescriptor",
    "MessageDescriptor",
    "OneofDescriptor",
    "RepeatedComposite",
    "WireMessage",
    "child",
    "reject_unknown",
    "repeated",
    "scalar",
    "set_scalar",
    "uuid7_text",
]
