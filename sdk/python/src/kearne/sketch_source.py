"""Canonical native-source calls for typed Sketch values."""

from __future__ import annotations

import ast
from collections.abc import Callable, Iterator
from typing import TypeAlias, cast

from kearne._sketch_schema import (
    HELPERS,
    OBJECT_HELPERS,
    POINT_REFERENCE_HELPERS,
    ArgumentSpec,
    HelperSpec,
    ObjectKind,
)
from kearne.sketch_values import (
    ArcEntity,
    BSplineEntity,
    CircleEntity,
    Constraint,
    EllipseEntity,
    EllipticalArcEntity,
    Entity,
    HyperbolicArcEntity,
    LineEntity,
    ParabolicArcEntity,
    PointEntity,
    PointRef,
    SketchDefinitionError,
    SketchObject,
    validate_sketch_values,
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

SketchValue: TypeAlias = SketchObject | Entity | Constraint
_POINT_HELPER_BY_KEY = {
    HELPERS[name].point_key: name for name in POINT_REFERENCE_HELPERS
}
_OBJECT_HELPER_BY_KIND = {
    spec.object_kind: spec for spec in HELPERS.values() if spec.section == "objects"
}


def _helper(value: SketchValue) -> HelperSpec:
    if isinstance(value, SketchObject):
        return _OBJECT_HELPER_BY_KIND[value.kind]
    elif isinstance(value, PointEntity):
        name = "point"
    elif isinstance(value, LineEntity):
        name = "line"
    elif isinstance(value, CircleEntity):
        name = "circle"
    elif isinstance(value, ArcEntity):
        name = "arc"
    elif isinstance(value, EllipseEntity):
        name = "ellipse"
    elif isinstance(value, EllipticalArcEntity):
        name = "elliptical_arc"
    elif isinstance(value, HyperbolicArcEntity):
        name = "hyperbolic_arc"
    elif isinstance(value, ParabolicArcEntity):
        name = "parabolic_arc"
    elif isinstance(value, BSplineEntity):
        name = "bspline"
    else:
        name = value.kind
    return HELPERS[name]


def _section(spec: HelperSpec) -> Section:
    if spec.section == "objects":
        return "objects"
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
        elif argument.kind == "entity_refs":
            yield argument, tuple(entities)
        elif argument.kind == "point":
            yield argument, value.position
        else:
            yield argument, value.value


def _arguments(
    value: SketchValue, spec: HelperSpec
) -> Iterator[tuple[ArgumentSpec, object]]:
    if isinstance(value, Constraint):
        yield from _constraint_arguments(value, spec)
        return
    if isinstance(value, SketchObject):
        entities = iter(value.entities)
        for argument in spec.positional:
            if argument.kind == "stable_id":
                yield argument, value.id
            elif argument.kind == "label":
                yield argument, value.label
            elif argument.kind == "entity_refs":
                yield argument, tuple(entities)
            else:
                yield argument, next(entities)
        return
    for argument in spec.positional:
        yield argument, getattr(value, argument.name)


def _number(value: float) -> str:
    return repr(value)


def _point_reference(value: PointRef) -> str:
    return f"{_POINT_HELPER_BY_KEY[value.key]}({value.entity!r})"


def _argument(value: object, kind: str) -> str:
    if kind in {"stable_id", "label", "entity_ref"}:
        return repr(cast(str, value))
    if kind == "entity_refs":
        return repr(cast(tuple[str, ...], value))
    if kind == "point_ref":
        return _point_reference(cast(PointRef, value))
    if kind == "point":
        point = cast(tuple[Length, Length], value)
        return f"(m({_number(point[0].metres)}), m({_number(point[1].metres)}))"
    if kind == "points":
        points = cast(tuple[tuple[Length, Length], ...], value)
        content = ", ".join(
            f"(m({_number(point[0].metres)}), m({_number(point[1].metres)}))"
            for point in points
        )
        return f"({content}{',' if len(points) == 1 else ''})"
    if kind == "length":
        return f"m({_number(cast(Length, value).metres)})"
    if kind == "angle":
        return f"rad({_number(cast(Angle, value).radians)})"
    if kind == "scalar":
        return _number(cast(float, value))
    if kind == "scalars":
        values = cast(tuple[float, ...], value)
        return repr(values)
    if kind == "integer":
        return repr(cast(int, value))
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
    constructors: dict[str, tuple[str, Callable[[float], Length | Angle]]] = {
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
    if kind in {"stable_id", "label", "entity_ref"}:
        return _literal(node, str)
    if kind == "entity_refs":
        values = cast(tuple[object, ...], _literal(node, tuple))
        if not values or not all(type(value) is str for value in values):
            raise _source_error()
        return cast(tuple[str, ...], values)
    if kind == "length" or kind == "angle":
        return _quantity(node, kind)
    if kind == "scalar":
        return _numeric(node)
    if kind == "scalars":
        if not isinstance(node, ast.Tuple) or not node.elts:
            raise _source_error()
        return tuple(_numeric(value) for value in node.elts)
    if kind == "integer":
        return _literal(node, int)
    if kind == "point":
        if not isinstance(node, ast.Tuple) or len(node.elts) != 2:
            raise _source_error()
        return tuple(_quantity(value, "length") for value in node.elts)
    if kind == "points":
        if not isinstance(node, ast.Tuple) or not node.elts:
            raise _source_error()
        points: list[tuple[Length, Length]] = []
        for point in node.elts:
            if not isinstance(point, ast.Tuple) or len(point.elts) != 2:
                raise _source_error()
            points.append(
                cast(
                    tuple[Length, Length],
                    tuple(_quantity(value, "length") for value in point.elts),
                )
            )
        return tuple(points)
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
    if spec is None or spec.section not in {"objects", "entities", "constraints"}:
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
        if spec.name in OBJECT_HELPERS:
            entities = tuple(
                cast(str, values[argument.name])
                for argument in spec.positional
                if argument.kind == "entity_ref"
            ) + tuple(
                entity
                for argument in spec.positional
                if argument.kind == "entity_refs"
                for entity in cast(tuple[str, ...], values[argument.name])
            )
            return SketchObject(
                cast(str, values["id"]),
                cast(str, values["label"]),
                cast(ObjectKind, spec.object_kind),
                entities,
            )
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
        if spec.name == "ellipse":
            return EllipseEntity(
                cast(str, values["id"]),
                cast(tuple[Length, Length], values["center"]),
                cast(Length, values["major_radius"]),
                cast(Length, values["minor_radius"]),
                cast(Angle, values["rotation"]),
                construction,
            )
        if spec.name == "elliptical_arc":
            return EllipticalArcEntity(
                cast(str, values["id"]),
                cast(tuple[Length, Length], values["center"]),
                cast(Length, values["major_radius"]),
                cast(Length, values["minor_radius"]),
                cast(Angle, values["rotation"]),
                cast(Angle, values["start_parameter"]),
                cast(Angle, values["end_parameter"]),
                construction,
            )
        if spec.name == "hyperbolic_arc":
            return HyperbolicArcEntity(
                cast(str, values["id"]),
                cast(tuple[Length, Length], values["center"]),
                cast(Length, values["major_radius"]),
                cast(Length, values["minor_radius"]),
                cast(Angle, values["rotation"]),
                cast(float, values["start_parameter"]),
                cast(float, values["end_parameter"]),
                construction,
            )
        if spec.name == "parabolic_arc":
            return ParabolicArcEntity(
                cast(str, values["id"]),
                cast(tuple[Length, Length], values["vertex"]),
                cast(Length, values["focal_length"]),
                cast(Angle, values["rotation"]),
                cast(Length, values["start_parameter"]),
                cast(Length, values["end_parameter"]),
                construction,
            )
        if spec.name == "bspline":
            return BSplineEntity(
                cast(str, values["id"]),
                cast(tuple[tuple[Length, Length], ...], values["control_points"]),
                cast(tuple[float, ...], values["knots"]),
                cast(tuple[float, ...], values["weights"]),
                cast(int, values["degree"]),
                cast(bool, values.get("periodic", False)),
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
        entity_set = next(
            (
                cast(tuple[str, ...], values[value.name])
                for value in arguments
                if value.kind == "entity_refs"
            ),
            (),
        )
        quantity = next(
            (
                cast(Length | Angle, values[value.name])
                for value in arguments
                if value.kind in {"length", "angle"}
            ),
            None,
        )
        position = next(
            (
                cast(tuple[Length, Length], values[value.name])
                for value in arguments
                if value.kind == "point"
            ),
            None,
        )
        return Constraint(
            cast(str, values["id"]),
            spec.name,
            points,
            (*entities, *entity_set),
            quantity,
            cast(str | None, values.get("mode")),
            position,
        )
    except (KeyError, SketchDefinitionError, TypeError) as error:
        raise _source_error() from error


def values_from_source(
    source: str, function: str
) -> tuple[
    str,
    tuple[SketchObject, ...],
    tuple[Entity, ...],
    tuple[Constraint, ...],
]:
    """Recognize and decode one editable Sketch without executing it."""
    session = open_edit_session(source, function)
    if session is None:
        raise SourceError(
            "source.edit.unrecognized", "function is not a recognized Sketch"
        )
    try:
        objects = tuple(
            cast(SketchObject, parse_call(value.code))
            for value in session.recognition.objects
        )
        entities = tuple(
            cast(Entity, parse_call(value.code))
            for value in session.recognition.entities
        )
        constraints = tuple(
            cast(Constraint, parse_call(value.code))
            for value in session.recognition.constraints
        )
        objects, entities, constraints = validate_sketch_values(
            objects, entities, constraints
        )
    except SketchDefinitionError as error:
        raise _source_error() from error
    return session.source_digest, objects, entities, constraints


def emit_call(value: SketchValue) -> str:
    """Emit one recognized native helper call from a validated typed value."""
    if not isinstance(
        value,
        (
            SketchObject,
            PointEntity,
            LineEntity,
            CircleEntity,
            ArcEntity,
            EllipseEntity,
            EllipticalArcEntity,
            HyperbolicArcEntity,
            ParabolicArcEntity,
            BSplineEntity,
            Constraint,
        ),
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
