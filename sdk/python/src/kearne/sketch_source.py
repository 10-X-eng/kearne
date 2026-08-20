"""Canonical native-source calls for typed Sketch values."""

from __future__ import annotations

from collections.abc import Iterator
from typing import TypeAlias, cast

from kearne._sketch_schema import (
    HELPERS,
    POINT_REFERENCE_HELPERS,
    ArgumentSpec,
    HelperSpec,
)
from kearne.sketch import (
    ArcEntity,
    CircleEntity,
    Constraint,
    Entity,
    LineEntity,
    PointEntity,
    PointRef,
    SketchDefinitionError,
)
from kearne.source import (
    AppendCall,
    DeleteCall,
    ReplaceCall,
    Section,
    SourceEditResult,
    apply_edit,
)
from kearne.units import Angle, Length

SketchValue: TypeAlias = Entity | Constraint
_POINT_HELPER_BY_KEY = {
    HELPERS[name].point_key: name for name in POINT_REFERENCE_HELPERS
}


def _helper(value: SketchValue) -> HelperSpec:
    if isinstance(value, PointEntity):
        name = "point"
    elif isinstance(value, LineEntity):
        name = "line"
    elif isinstance(value, CircleEntity):
        name = "circle"
    elif isinstance(value, ArcEntity):
        name = "arc"
    else:
        name = value.kind
    return HELPERS[name]


def _section(spec: HelperSpec) -> Section:
    if spec.section == "entities":
        return "entities"
    if spec.section == "constraints":
        return "constraints"
    raise SketchDefinitionError("source emitter helper section is unsupported")


def _constraint_arguments(
    value: Constraint, spec: HelperSpec
) -> Iterator[tuple[ArgumentSpec, object]]:
    points = iter(value.points)
    entities = iter(value.entities)
    for argument in spec.positional:
        if argument.kind == "stable_id":
            yield argument, value.id
        elif argument.kind == "point_ref":
            yield argument, next(points)
        elif argument.kind == "entity_ref":
            yield argument, next(entities)
        else:
            yield argument, value.value


def _arguments(
    value: SketchValue, spec: HelperSpec
) -> Iterator[tuple[ArgumentSpec, object]]:
    if isinstance(value, Constraint):
        yield from _constraint_arguments(value, spec)
        return
    for argument in spec.positional:
        yield argument, getattr(value, argument.name)


def _number(value: float) -> str:
    return repr(value)


def _point_reference(value: PointRef) -> str:
    return f"{_POINT_HELPER_BY_KEY[value.key]}({value.entity!r})"


def _argument(value: object, kind: str) -> str:
    if kind in {"stable_id", "entity_ref"}:
        return repr(cast(str, value))
    if kind == "point_ref":
        return _point_reference(cast(PointRef, value))
    if kind == "point":
        point = cast(tuple[Length, Length], value)
        return f"(m({_number(point[0].metres)}), m({_number(point[1].metres)}))"
    if kind == "length":
        return f"m({_number(cast(Length, value).metres)})"
    if kind == "angle":
        return f"rad({_number(cast(Angle, value).radians)})"
    raise SketchDefinitionError("source emitter argument kind is unsupported")


def emit_call(value: SketchValue) -> str:
    """Emit one recognized native helper call from a validated typed value."""
    if not isinstance(
        value, (PointEntity, LineEntity, CircleEntity, ArcEntity, Constraint)
    ):
        raise SketchDefinitionError("source emitter value is invalid")
    spec = _helper(value)
    arguments = [
        _argument(argument, descriptor.kind)
        for descriptor, argument in _arguments(value, spec)
    ]
    keywords: list[str] = []
    for keyword in spec.keywords:
        current = getattr(value, keyword.name)
        if current != keyword.default:
            keywords.append(f"{keyword.name}={current!r}")
    return f"{spec.name}({', '.join((*arguments, *keywords))})"


def append_value(
    source: str,
    function: str,
    expected_digest: str,
    value: SketchValue,
) -> SourceEditResult:
    """Append one typed Sketch value through the recognized source editor."""
    spec = _helper(value)
    return apply_edit(
        source, function, expected_digest, AppendCall(_section(spec), emit_call(value))
    )


def replace_value(
    source: str,
    function: str,
    expected_digest: str,
    value: SketchValue,
) -> SourceEditResult:
    """Replace one typed Sketch value without changing its stable identity."""
    spec = _helper(value)
    return apply_edit(
        source,
        function,
        expected_digest,
        ReplaceCall(_section(spec), value.id, emit_call(value)),
    )


def delete_value(
    source: str,
    function: str,
    expected_digest: str,
    value: SketchValue,
) -> SourceEditResult:
    """Delete one typed Sketch value through the recognized source editor."""
    spec = _helper(value)
    return apply_edit(
        source, function, expected_digest, DeleteCall(_section(spec), value.id)
    )


__all__ = ["append_value", "delete_value", "emit_call", "replace_value"]
