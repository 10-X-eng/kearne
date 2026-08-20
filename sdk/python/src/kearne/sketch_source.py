"""Canonical native-source calls for typed Sketch values."""

from __future__ import annotations

import ast
from collections.abc import Callable, Iterator
from typing import TypeAlias, cast

from kearne._sketch_schema import (
    HELPERS,
    POINT_REFERENCE_HELPERS,
    ArgumentSpec,
    HelperSpec,
)
from kearne.sketch_values import (
    ArcEntity,
    CircleEntity,
    Constraint,
    Entity,
    LineEntity,
    PointEntity,
    PointRef,
    SketchDefinitionError,
    validate_definition_values,
)
from kearne.source import (
    AppendCall,
    DeleteCall,
    ReplaceCall,
    Section,
    SourceEditResult,
    SourceError,
    apply_edit,
    open_edit_session,
)
from kearne.units import Angle, Length, deg, inch, m, mm, rad

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


def _source_error() -> SourceError:
    return SourceError(
        "source.sketch.invalid-value",
        "recognized Sketch source contains an invalid helper value",
    )


def _literal(node: ast.expr, expected: type[object]) -> object:
    try:
        value = ast.literal_eval(node)
    except (ValueError, SyntaxError) as error:
        raise _source_error() from error
    if type(value) is not expected:
        raise _source_error()
    return value


def _numeric(node: ast.expr) -> float:
    try:
        value = ast.literal_eval(node)
    except (ValueError, SyntaxError) as error:
        raise _source_error() from error
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        raise _source_error()
    return float(value)


def _quantity(node: ast.expr, kind: str) -> Length | Angle:
    if (
        not isinstance(node, ast.Call)
        or not isinstance(node.func, ast.Name)
        or len(node.args) != 1
        or node.keywords
    ):
        raise _source_error()
    constructors: dict[
        str, tuple[str, Callable[[float], Length | Angle]]
    ] = {
        "m": ("length", m),
        "mm": ("length", mm),
        "inch": ("length", inch),
        "rad": ("angle", rad),
        "deg": ("angle", deg),
    }
    selected = constructors.get(node.func.id)
    if selected is None or selected[0] != kind:
        raise _source_error()
    value = _numeric(node.args[0])
    try:
        return selected[1](value)
    except ValueError as error:
        raise _source_error() from error


def _parse_argument(node: ast.expr, kind: str) -> object:
    if kind in {"stable_id", "entity_ref"}:
        return _literal(node, str)
    if kind == "length" or kind == "angle":
        return _quantity(node, kind)
    if kind == "point":
        if not isinstance(node, ast.Tuple) or len(node.elts) != 2:
            raise _source_error()
        return tuple(_quantity(value, "length") for value in node.elts)
    if kind == "point_ref":
        if (
            not isinstance(node, ast.Call)
            or not isinstance(node.func, ast.Name)
            or len(node.args) != 1
            or node.keywords
        ):
            raise _source_error()
        key = HELPERS.get(node.func.id)
        if key is None or key.point_key is None:
            raise _source_error()
        return PointRef(cast(str, _literal(node.args[0], str)), key.point_key)
    raise _source_error()


def parse_call(code: str) -> SketchValue:
    """Decode one recognized helper call without executing Python."""
    try:
        expression = ast.parse(code, mode="eval").body
    except (SyntaxError, ValueError) as error:
        raise _source_error() from error
    if not isinstance(expression, ast.Call) or not isinstance(
        expression.func, ast.Name
    ):
        raise _source_error()
    spec = HELPERS.get(expression.func.id)
    if spec is None or spec.section not in {"entities", "constraints"}:
        raise _source_error()
    if len(expression.args) != len(spec.positional):
        raise _source_error()
    values = {
        descriptor.name: _parse_argument(node, descriptor.kind)
        for descriptor, node in zip(spec.positional, expression.args, strict=True)
    }
    for keyword in expression.keywords:
        descriptor = next(
            (value for value in spec.keywords if value.name == keyword.arg), None
        )
        if descriptor is None or keyword.arg in values:
            raise _source_error()
        expected = bool if descriptor.kind == "boolean" else str
        values[cast(str, keyword.arg)] = _literal(keyword.value, expected)
    try:
        construction = cast(bool, values.get("construction", False))
        if spec.name == "point":
            return PointEntity(
                cast(str, values["id"]),
                cast(tuple[Length, Length], values["at"]),
                construction,
            )
        if spec.name == "line":
            return LineEntity(
                cast(str, values["id"]),
                cast(tuple[Length, Length], values["start"]),
                cast(tuple[Length, Length], values["end"]),
                construction,
            )
        if spec.name == "circle":
            return CircleEntity(
                cast(str, values["id"]),
                cast(tuple[Length, Length], values["center"]),
                cast(Length, values["radius"]),
                construction,
            )
        if spec.name == "arc":
            return ArcEntity(
                cast(str, values["id"]),
                cast(tuple[Length, Length], values["center"]),
                cast(Length, values["radius"]),
                cast(Angle, values["start_angle"]),
                cast(Angle, values["end_angle"]),
                construction,
            )
        arguments = spec.positional[1:]
        points = tuple(
            cast(PointRef, values[value.name])
            for value in arguments
            if value.kind == "point_ref"
        )
        entities = tuple(
            cast(str, values[value.name])
            for value in arguments
            if value.kind == "entity_ref"
        )
        quantity = next(
            (
                cast(Length | Angle, values[value.name])
                for value in arguments
                if value.kind in {"length", "angle"}
            ),
            None,
        )
        return Constraint(
            cast(str, values["id"]),
            spec.name,
            points,
            entities,
            quantity,
            cast(str | None, values.get("mode")),
        )
    except (KeyError, SketchDefinitionError, TypeError) as error:
        raise _source_error() from error


def values_from_source(
    source: str, function: str
) -> tuple[str, tuple[Entity, ...], tuple[Constraint, ...]]:
    """Recognize and decode one editable Sketch without executing it."""
    session = open_edit_session(source, function)
    if session is None:
        raise SourceError(
            "source.edit.unrecognized", "function is not a recognized Sketch"
        )
    try:
        entities = tuple(
            cast(Entity, parse_call(value.code))
            for value in session.recognition.entities
        )
        constraints = tuple(
            cast(Constraint, parse_call(value.code))
            for value in session.recognition.constraints
        )
        entities, constraints = validate_definition_values(entities, constraints)
    except SketchDefinitionError as error:
        raise _source_error() from error
    return session.source_digest, entities, constraints


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


__all__ = [
    "append_value",
    "delete_value",
    "emit_call",
    "parse_call",
    "replace_value",
    "values_from_source",
]
