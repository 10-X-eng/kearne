"""Bounded Protobuf boundary for recognized sketch definitions."""

from __future__ import annotations

from dataclasses import dataclass
from importlib import import_module
from math import isfinite
from types import ModuleType
from typing import cast
from uuid import UUID

from kearne._sketch_schema import (
    CONSTRAINT_HELPERS,
    ENTITY_HELPERS,
    HELPERS,
)
from kearne._wire import (
    WireMessage as _WireMessage,
)
from kearne._wire import (
    child as _child,
)
from kearne._wire import (
    reject_unknown,
)
from kearne._wire import (
    repeated as _repeated,
)
from kearne._wire import (
    scalar as _scalar,
)
from kearne._wire import (
    set_scalar as _set_scalar,
)
from kearne.sketch import (
    ArcEntity,
    CircleEntity,
    Constraint,
    Entity,
    LineEntity,
    PointEntity,
    PointRef,
    SketchDefinition,
    SketchDefinitionError,
    SketchPlane,
)
from kearne.units import Angle, Length

MAXIMUM_SERIALIZED_BYTES = 16_777_216
MAXIMUM_ENTITIES = 65_536
MAXIMUM_CONSTRAINTS = 65_536


class SketchWireError(ValueError):
    """A sketch definition cannot safely cross the native wire boundary."""

    def __init__(self, code: str, message: str) -> None:
        super().__init__(message)
        self.code = code


@dataclass(frozen=True, slots=True)
class DecodedSketchDefinition:
    source_digest: str
    definition: SketchDefinition


def _wire_module() -> ModuleType:
    try:
        return import_module("kearne.api.v1.sketch_pb2")
    except ModuleNotFoundError as error:
        raise SketchWireError(
            "sketch.wire.bindings-missing",
            "generated Kearne protobuf bindings are unavailable",
        ) from error


def _decode_error_type() -> type[Exception]:
    module = import_module("google.protobuf.message")
    return cast(type[Exception], module.DecodeError)


def _message_type(name: str) -> type[_WireMessage]:
    return cast(type[_WireMessage], getattr(_wire_module(), name))


def _enum_number(message: _WireMessage, field: str, name: str) -> int:
    descriptor = message.DESCRIPTOR.fields_by_name[field].enum_type
    return descriptor.values_by_name[name].number


def _assert_schema_coverage() -> None:
    definition = _message_type("SketchDefinition").DESCRIPTOR
    if set(definition.fields_by_name) != {
        "source_digest",
        "entities",
        "constraints",
    }:
        raise SketchWireError(
            "sketch.wire.conversion-registry-stale",
            "sketch definition fields are not fully covered",
        )
    entity = _message_type("SketchEntity").DESCRIPTOR
    geometry = entity.oneofs_by_name.get("geometry")
    if (
        set(entity.fields_by_name)
        != {
            "id",
            "construction",
            *ENTITY_HELPERS,
        }
        or geometry is None
        or {field.name for field in geometry.fields} != ENTITY_HELPERS
    ):
        raise SketchWireError(
            "sketch.wire.conversion-registry-stale",
            "sketch entity members are not fully covered",
        )
    constraint = _message_type("SketchConstraint").DESCRIPTOR
    relation = constraint.oneofs_by_name.get("relation")
    if (
        set(constraint.fields_by_name) != {"id", *CONSTRAINT_HELPERS}
        or relation is None
        or {field.name for field in relation.fields} != CONSTRAINT_HELPERS
    ):
        raise SketchWireError(
            "sketch.wire.conversion-registry-stale",
            "sketch constraint members are not fully covered",
        )
    for kind in (*sorted(ENTITY_HELPERS), *sorted(CONSTRAINT_HELPERS)):
        owner = entity if kind in ENTITY_HELPERS else constraint
        payload = owner.fields_by_name[kind].message_type
        if payload is None:
            raise SketchWireError(
                "sketch.wire.conversion-registry-stale",
                f"{kind} payload is not a message",
            )
        spec = HELPERS[kind]
        expected = {argument.name for argument in spec.positional[1:]}
        if any(keyword.name == "mode" for keyword in spec.keywords):
            expected.add("mode")
        if set(payload.fields_by_name) != expected:
            raise SketchWireError(
                "sketch.wire.conversion-registry-stale",
                f"{kind} payload fields are not fully covered",
            )
    point_reference = _message_type("SketchPointReference").DESCRIPTOR
    point = _message_type("SketchPoint2").DESCRIPTOR
    if set(point_reference.fields_by_name) != {"entity", "key"} or set(
        point.fields_by_name
    ) != {"x", "y"}:
        raise SketchWireError(
            "sketch.wire.conversion-registry-stale",
            "sketch value fields are not fully covered",
        )


def _reject_unknown(message: _WireMessage) -> None:
    if reject_unknown(message):
        raise SketchWireError(
            "sketch.wire.unknown-field",
            "sketch definition contains an unsupported executable field",
        )


def _require_present(message: _WireMessage) -> None:
    selected = {
        oneof.name: message.WhichOneof(oneof.name)
        for oneof in message.DESCRIPTOR.oneofs
    }
    if any(value is None for value in selected.values()):
        raise SketchWireError(
            "sketch.wire.required-oneof", "required sketch choice is missing"
        )
    for field in message.DESCRIPTOR.fields:
        if field.is_repeated:
            if field.message_type is not None:
                for value in _repeated(message, field.name):
                    _require_present(value)
            continue
        if (
            field.containing_oneof is not None
            and selected[field.containing_oneof.name] != field.name
        ):
            continue
        if not message.HasField(field.name):
            raise SketchWireError(
                "sketch.wire.required-field",
                f"{field.full_name} is missing",
            )
        if field.message_type is not None:
            _require_present(_child(message, field.name))


def _write_uuid(message: _WireMessage, value: str) -> None:
    try:
        parsed = UUID(value)
    except ValueError as error:
        raise SketchWireError(
            "sketch.wire.invalid-id", "stable ID is not a UUID"
        ) from error
    if parsed.version != 7 or str(parsed) != value:
        raise SketchWireError(
            "sketch.wire.invalid-id", "stable ID is not a canonical UUIDv7"
        )
    _set_scalar(message, "value", parsed.bytes)


def _read_uuid(message: _WireMessage) -> str:
    raw = cast(bytes, _scalar(message, "value"))
    if len(raw) != 16:
        raise SketchWireError(
            "sketch.wire.invalid-id", "stable ID has the wrong byte length"
        )
    parsed = UUID(bytes=raw)
    value = str(parsed)
    if parsed.version != 7:
        raise SketchWireError("sketch.wire.invalid-id", "stable ID is not a UUIDv7")
    return value


def _write_digest(message: _WireMessage, value: str) -> None:
    algorithm, separator, hexadecimal = value.partition(":")
    if (
        not separator
        or not 1 <= len(algorithm) <= 32
        or any(
            character not in "abcdefghijklmnopqrstuvwxyz0123456789-"
            for character in algorithm
        )
        or len(hexadecimal) != 64
    ):
        raise SketchWireError("sketch.wire.invalid-digest", "source digest is invalid")
    try:
        raw = bytes.fromhex(hexadecimal)
    except ValueError as error:
        raise SketchWireError(
            "sketch.wire.invalid-digest", "source digest is invalid"
        ) from error
    _set_scalar(message, "algorithm", algorithm)
    _set_scalar(message, "value", raw)


def _read_digest(message: _WireMessage) -> str:
    algorithm = cast(str, _scalar(message, "algorithm"))
    raw = cast(bytes, _scalar(message, "value"))
    result = f"{algorithm}:{raw.hex()}"
    probe = type(message)()
    _write_digest(probe, result)
    return result


def _write_point(message: _WireMessage, point: tuple[Length, Length]) -> None:
    _set_scalar(message, "x", point[0].metres)
    _set_scalar(message, "y", point[1].metres)


def _read_point(message: _WireMessage) -> tuple[Length, Length]:
    x = float(cast(float, _scalar(message, "x")))
    y = float(cast(float, _scalar(message, "y")))
    if not isfinite(x) or not isfinite(y):
        raise SketchWireError("sketch.wire.non-finite", "sketch point is not finite")
    return Length(x), Length(y)


_POINT_KEY_NAMES = {
    "point": "SKETCH_POINT_KEY_POINT",
    "start": "SKETCH_POINT_KEY_START",
    "end": "SKETCH_POINT_KEY_END",
    "center": "SKETCH_POINT_KEY_CENTER",
}


def _write_point_reference(message: _WireMessage, value: PointRef) -> None:
    _write_uuid(_child(message, "entity"), value.entity)
    _set_scalar(
        message, "key", _enum_number(message, "key", _POINT_KEY_NAMES[value.key])
    )


def _read_point_reference(message: _WireMessage) -> PointRef:
    number = int(cast(int, _scalar(message, "key")))
    descriptor = message.DESCRIPTOR.fields_by_name["key"].enum_type
    value = descriptor.values_by_number.get(number)
    if value is None or value.name == "SKETCH_POINT_KEY_UNSPECIFIED":
        raise SketchWireError(
            "sketch.wire.invalid-point-key", "wire point key is unsupported"
        )
    by_name = {wire_name: key for key, wire_name in _POINT_KEY_NAMES.items()}
    key = by_name.get(value.name)
    if key is None:
        raise SketchWireError(
            "sketch.wire.invalid-point-key", "wire point key is unsupported"
        )
    return PointRef(_read_uuid(_child(message, "entity")), key)


_ENTITY_TYPES: dict[str, type[object]] = {
    "point": PointEntity,
    "line": LineEntity,
    "circle": CircleEntity,
    "arc": ArcEntity,
}
_ENTITY_KINDS = {value: key for key, value in _ENTITY_TYPES.items()}


def _write_entity(message: _WireMessage, entity: Entity) -> None:
    kind = _ENTITY_KINDS.get(type(entity))
    if kind is None:
        raise SketchWireError(
            "sketch.wire.unsupported-entity", "sketch entity is unsupported"
        )
    _write_uuid(_child(message, "id"), entity.id)
    _set_scalar(message, "construction", entity.construction)
    payload = _child(message, kind)
    for argument in HELPERS[kind].positional[1:]:
        value = getattr(entity, argument.name)
        if argument.kind == "point":
            _write_point(_child(payload, argument.name), value)
        elif argument.kind == "length":
            _set_scalar(payload, argument.name, value.metres)
        elif argument.kind == "angle":
            _set_scalar(payload, argument.name, value.radians)
        else:
            raise SketchWireError(
                "sketch.wire.conversion-registry-stale",
                "entity argument kind is not covered",
            )


def _read_entity(message: _WireMessage) -> Entity:
    kind = message.WhichOneof("geometry")
    if kind is None or kind not in _ENTITY_TYPES:
        raise SketchWireError(
            "sketch.wire.unsupported-entity", "wire sketch entity is unsupported"
        )
    payload = _child(message, kind)
    stable_id = _read_uuid(_child(message, "id"))
    construction = bool(_scalar(message, "construction"))
    if kind == "point":
        return PointEntity(stable_id, _read_point(_child(payload, "at")), construction)
    if kind == "line":
        return LineEntity(
            stable_id,
            _read_point(_child(payload, "start")),
            _read_point(_child(payload, "end")),
            construction,
        )
    center = _read_point(_child(payload, "center"))
    radius = float(cast(float, _scalar(payload, "radius")))
    if not isfinite(radius):
        raise SketchWireError("sketch.wire.non-finite", "sketch radius is not finite")
    if kind == "circle":
        return CircleEntity(stable_id, center, Length(radius), construction)
    start = float(cast(float, _scalar(payload, "start_angle")))
    end = float(cast(float, _scalar(payload, "end_angle")))
    if not isfinite(start) or not isfinite(end):
        raise SketchWireError("sketch.wire.non-finite", "sketch angle is not finite")
    return ArcEntity(
        stable_id, center, Length(radius), Angle(start), Angle(end), construction
    )


def _write_constraint(message: _WireMessage, constraint: Constraint) -> None:
    spec = HELPERS.get(constraint.kind)
    if spec is None or constraint.kind not in CONSTRAINT_HELPERS:
        raise SketchWireError(
            "sketch.wire.unsupported-constraint",
            "sketch constraint is unsupported",
        )
    _write_uuid(_child(message, "id"), constraint.id)
    payload = _child(message, constraint.kind)
    points = iter(constraint.points)
    entities = iter(constraint.entities)
    for argument in spec.positional[1:]:
        if argument.kind == "point_ref":
            _write_point_reference(_child(payload, argument.name), next(points))
        elif argument.kind == "entity_ref":
            _write_uuid(_child(payload, argument.name), next(entities))
        elif argument.kind == "length":
            if not isinstance(constraint.value, Length):
                raise SketchWireError(
                    "sketch.wire.invalid-value", "constraint length is missing"
                )
            _set_scalar(payload, argument.name, constraint.value.metres)
        elif argument.kind == "angle":
            if not isinstance(constraint.value, Angle):
                raise SketchWireError(
                    "sketch.wire.invalid-value", "constraint angle is missing"
                )
            _set_scalar(payload, argument.name, constraint.value.radians)
        else:
            raise SketchWireError(
                "sketch.wire.conversion-registry-stale",
                "constraint argument kind is not covered",
            )
    if any(keyword.name == "mode" for keyword in spec.keywords):
        name = (
            "SKETCH_TANGENCY_INTERNAL"
            if constraint.mode == "internal"
            else "SKETCH_TANGENCY_EXTERNAL"
        )
        _set_scalar(payload, "mode", _enum_number(payload, "mode", name))


def _read_constraint(message: _WireMessage) -> Constraint:
    kind = message.WhichOneof("relation")
    spec = HELPERS.get(kind or "")
    if kind is None or spec is None or kind not in CONSTRAINT_HELPERS:
        raise SketchWireError(
            "sketch.wire.unsupported-constraint",
            "wire sketch constraint is unsupported",
        )
    payload = _child(message, kind)
    points: list[PointRef] = []
    entities: list[str] = []
    quantity: Length | Angle | None = None
    for argument in spec.positional[1:]:
        if argument.kind == "point_ref":
            points.append(_read_point_reference(_child(payload, argument.name)))
        elif argument.kind == "entity_ref":
            entities.append(_read_uuid(_child(payload, argument.name)))
        elif argument.kind in {"length", "angle"}:
            value = float(cast(float, _scalar(payload, argument.name)))
            if not isfinite(value):
                raise SketchWireError(
                    "sketch.wire.non-finite", "constraint value is not finite"
                )
            quantity = Length(value) if argument.kind == "length" else Angle(value)
        else:
            raise SketchWireError(
                "sketch.wire.conversion-registry-stale",
                "constraint argument kind is not covered",
            )
    mode: str | None = None
    if any(keyword.name == "mode" for keyword in spec.keywords):
        number = int(cast(int, _scalar(payload, "mode")))
        descriptor = payload.DESCRIPTOR.fields_by_name["mode"].enum_type
        enum_value = descriptor.values_by_number.get(number)
        modes = {
            "SKETCH_TANGENCY_EXTERNAL": "external",
            "SKETCH_TANGENCY_INTERNAL": "internal",
        }
        mode = modes.get(enum_value.name if enum_value is not None else "")
        if mode is None:
            raise SketchWireError(
                "sketch.wire.invalid-tangency", "wire tangency mode is unsupported"
            )
    return Constraint(
        _read_uuid(_child(message, "id")),
        kind,
        tuple(points),
        tuple(entities),
        quantity,
        mode,
    )


def definition_to_wire(
    definition: SketchDefinition, source_digest: str
) -> _WireMessage:
    """Convert a recognized runtime definition to generated protobuf types."""
    _assert_schema_coverage()
    if not isinstance(definition, SketchDefinition):
        raise SketchWireError(
            "sketch.wire.invalid-definition", "value is not a sketch definition"
        )
    if (
        len(definition.entities) > MAXIMUM_ENTITIES
        or len(definition.constraints) > MAXIMUM_CONSTRAINTS
    ):
        raise SketchWireError(
            "sketch.wire.count-limit", "sketch definition exceeds a count limit"
        )
    result = _message_type("SketchDefinition")()
    _write_digest(_child(result, "source_digest"), source_digest)
    entities = _repeated(result, "entities")
    for entity in definition.entities:
        _write_entity(entities.add(), entity)
    constraints = _repeated(result, "constraints")
    for constraint in definition.constraints:
        _write_constraint(constraints.add(), constraint)
    if result.ByteSize() > MAXIMUM_SERIALIZED_BYTES:
        raise SketchWireError(
            "sketch.wire.byte-limit", "sketch definition exceeds the byte limit"
        )
    return result


def definition_from_wire(
    message: _WireMessage, plane: SketchPlane
) -> DecodedSketchDefinition:
    """Convert generated protobuf types to a validated runtime definition."""
    _assert_schema_coverage()
    if message.DESCRIPTOR.full_name != "kearne.api.v1.SketchDefinition":
        raise SketchWireError(
            "sketch.wire.wrong-type", "message is not a sketch definition"
        )
    if message.ByteSize() > MAXIMUM_SERIALIZED_BYTES:
        raise SketchWireError(
            "sketch.wire.byte-limit", "sketch definition exceeds the byte limit"
        )
    _reject_unknown(message)
    _require_present(message)
    entities_wire = _repeated(message, "entities")
    constraints_wire = _repeated(message, "constraints")
    if (
        len(entities_wire) > MAXIMUM_ENTITIES
        or len(constraints_wire) > MAXIMUM_CONSTRAINTS
    ):
        raise SketchWireError(
            "sketch.wire.count-limit", "sketch definition exceeds a count limit"
        )
    try:
        definition = SketchDefinition(
            plane,
            tuple(_read_entity(value) for value in entities_wire),
            tuple(_read_constraint(value) for value in constraints_wire),
        )
    except SketchDefinitionError as error:
        raise SketchWireError(
            "sketch.wire.invalid-domain", "wire sketch definition is invalid"
        ) from error
    return DecodedSketchDefinition(
        _read_digest(_child(message, "source_digest")), definition
    )


def serialize_definition(definition: SketchDefinition, source_digest: str) -> bytes:
    """Serialize a recognized definition with the pinned protobuf runtime."""
    result = definition_to_wire(definition, source_digest).SerializeToString()
    if len(result) > MAXIMUM_SERIALIZED_BYTES:
        raise SketchWireError(
            "sketch.wire.byte-limit", "sketch definition exceeds the byte limit"
        )
    return result


def parse_definition(payload: bytes, plane: SketchPlane) -> DecodedSketchDefinition:
    """Parse bounded untrusted bytes without importing or executing source."""
    if not isinstance(payload, bytes) or len(payload) > MAXIMUM_SERIALIZED_BYTES:
        raise SketchWireError(
            "sketch.wire.byte-limit", "serialized sketch definition is invalid"
        )
    message = _message_type("SketchDefinition")()
    try:
        message.ParseFromString(payload)
    except _decode_error_type() as error:
        raise SketchWireError(
            "sketch.wire.parse", "serialized sketch definition is invalid"
        ) from error
    return definition_from_wire(message, plane)


__all__ = [
    "MAXIMUM_CONSTRAINTS",
    "MAXIMUM_ENTITIES",
    "MAXIMUM_SERIALIZED_BYTES",
    "DecodedSketchDefinition",
    "SketchWireError",
    "definition_from_wire",
    "definition_to_wire",
    "parse_definition",
    "serialize_definition",
]
