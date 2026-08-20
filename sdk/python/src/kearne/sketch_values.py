"""CAD-runtime-independent values for recognized Sketch source."""

from __future__ import annotations

from dataclasses import dataclass
from math import hypot
from typing import TypeAlias
from uuid import UUID

from kearne._sketch_schema import (
    ANGLE_TOLERANCE_RADIANS,
    HELPER_SPECS,
    HELPERS,
    MAXIMUM_ARC_SPAN_RADIANS,
    MAXIMUM_COORDINATE_METRES,
    MINIMUM_LENGTH_METRES,
    POINT_REFERENCE_HELPERS,
)
from kearne.units import Angle, Length

Point2: TypeAlias = tuple[Length, Length]


class SketchDefinitionError(ValueError):
    """A recognized sketch definition is structurally invalid."""


def validate_stable_id(value: str) -> str:
    if not isinstance(value, str):
        raise SketchDefinitionError("stable ID is not a string")
    try:
        parsed = UUID(value)
    except ValueError as error:
        raise SketchDefinitionError("stable ID is not a UUID") from error
    if parsed.version != 7 or str(parsed) != value:
        raise SketchDefinitionError("stable ID is not a canonical UUIDv7")
    return value


def _point(value: Point2) -> Point2:
    if not isinstance(value, tuple) or len(value) != 2:
        raise SketchDefinitionError("point must have two coordinates")
    if not all(isinstance(coordinate, Length) for coordinate in value):
        raise SketchDefinitionError("point coordinates must be Length values")
    if any(abs(coordinate.metres) > MAXIMUM_COORDINATE_METRES for coordinate in value):
        raise SketchDefinitionError("point coordinate exceeds the supported range")
    return value


@dataclass(frozen=True, slots=True)
class PointEntity:
    id: str
    at: Point2
    construction: bool = False

    def __post_init__(self) -> None:
        object.__setattr__(self, "id", validate_stable_id(self.id))
        object.__setattr__(self, "at", _point(self.at))
        if not isinstance(self.construction, bool):
            raise SketchDefinitionError("construction flag is not boolean")


@dataclass(frozen=True, slots=True)
class LineEntity:
    id: str
    start: Point2
    end: Point2
    construction: bool = False

    def __post_init__(self) -> None:
        object.__setattr__(self, "id", validate_stable_id(self.id))
        object.__setattr__(self, "start", _point(self.start))
        object.__setattr__(self, "end", _point(self.end))
        if hypot(
            self.end[0].metres - self.start[0].metres,
            self.end[1].metres - self.start[1].metres,
        ) < MINIMUM_LENGTH_METRES:
            raise SketchDefinitionError("line is degenerate")
        if not isinstance(self.construction, bool):
            raise SketchDefinitionError("construction flag is not boolean")


@dataclass(frozen=True, slots=True)
class CircleEntity:
    id: str
    center: Point2
    radius: Length
    construction: bool = False

    def __post_init__(self) -> None:
        object.__setattr__(self, "id", validate_stable_id(self.id))
        object.__setattr__(self, "center", _point(self.center))
        if (
            not isinstance(self.radius, Length)
            or self.radius.metres < MINIMUM_LENGTH_METRES
            or self.radius.metres > MAXIMUM_COORDINATE_METRES
        ):
            raise SketchDefinitionError("circle radius is invalid")
        if not isinstance(self.construction, bool):
            raise SketchDefinitionError("construction flag is not boolean")


@dataclass(frozen=True, slots=True)
class ArcEntity:
    id: str
    center: Point2
    radius: Length
    start_angle: Angle
    end_angle: Angle
    construction: bool = False

    def __post_init__(self) -> None:
        object.__setattr__(self, "id", validate_stable_id(self.id))
        object.__setattr__(self, "center", _point(self.center))
        if (
            not isinstance(self.radius, Length)
            or not isinstance(self.start_angle, Angle)
            or not isinstance(self.end_angle, Angle)
            or self.radius.metres < MINIMUM_LENGTH_METRES
            or self.radius.metres > MAXIMUM_COORDINATE_METRES
        ):
            raise SketchDefinitionError("arc is degenerate")
        span = abs(self.end_angle.radians - self.start_angle.radians)
        if span < ANGLE_TOLERANCE_RADIANS:
            raise SketchDefinitionError("arc has no angular span")
        if span > MAXIMUM_ARC_SPAN_RADIANS:
            raise SketchDefinitionError("arc angular span exceeds one revolution")
        if not isinstance(self.construction, bool):
            raise SketchDefinitionError("construction flag is not boolean")


Entity: TypeAlias = PointEntity | LineEntity | CircleEntity | ArcEntity
_ENTITY_KIND_BY_TYPE = {
    PointEntity: "point",
    LineEntity: "line",
    CircleEntity: "circle",
    ArcEntity: "arc",
}
_POINT_TARGETS = {
    spec.point_key: spec.positional[0].entity_kinds
    for spec in HELPER_SPECS
    if spec.name in POINT_REFERENCE_HELPERS
}


@dataclass(frozen=True, slots=True)
class PointRef:
    entity: str
    key: str

    def __post_init__(self) -> None:
        object.__setattr__(self, "entity", validate_stable_id(self.entity))
        if self.key not in {"point", "start", "end", "center"}:
            raise SketchDefinitionError("point reference key is invalid")


@dataclass(frozen=True, slots=True)
class Constraint:
    id: str
    kind: str
    points: tuple[PointRef, ...] = ()
    entities: tuple[str, ...] = ()
    value: Length | Angle | None = None
    mode: str | None = None

    def __post_init__(self) -> None:
        object.__setattr__(self, "id", validate_stable_id(self.id))
        object.__setattr__(self, "points", tuple(self.points))
        object.__setattr__(
            self,
            "entities",
            tuple(validate_stable_id(value) for value in self.entities),
        )
        spec = HELPERS.get(self.kind)
        if spec is None or spec.section != "constraints":
            raise SketchDefinitionError("constraint kind is invalid")
        arguments = spec.positional[1:]
        if len(self.points) != sum(value.kind == "point_ref" for value in arguments):
            raise SketchDefinitionError("constraint reference count is invalid")
        if len(self.entities) != sum(
            value.kind == "entity_ref" for value in arguments
        ):
            raise SketchDefinitionError("constraint reference count is invalid")
        if not all(isinstance(reference, PointRef) for reference in self.points):
            raise SketchDefinitionError("constraint point reference is invalid")
        quantity = next(
            (value for value in arguments if value.kind in {"length", "angle"}),
            None,
        )
        quantity_type = (
            Length if quantity is not None and quantity.kind == "length" else Angle
        )
        if quantity is not None and not isinstance(self.value, quantity_type):
            raise SketchDefinitionError("constraint value has the wrong quantity type")
        if quantity is None and self.value is not None:
            raise SketchDefinitionError("constraint does not accept a value")
        if quantity is not None and isinstance(self.value, Length):
            if quantity.limit == "positive" and self.value.metres <= 0.0:
                raise SketchDefinitionError("constraint value is not positive")
            if quantity.limit == "nonnegative" and self.value.metres < 0.0:
                raise SketchDefinitionError("constraint value is negative")
        mode_spec = next(
            (value for value in spec.keywords if value.name == "mode"), None
        )
        if mode_spec is not None:
            mode = self.mode or "external"
            if mode not in mode_spec.values:
                raise SketchDefinitionError("tangency mode is invalid")
            object.__setattr__(self, "mode", mode)
        elif self.mode is not None:
            raise SketchDefinitionError("constraint does not accept a mode")


SketchValue: TypeAlias = Entity | Constraint


def point(id: str, at: Point2, /, *, construction: bool = False) -> PointEntity:
    return PointEntity(id, at, construction)


def line(
    id: str, start: Point2, end: Point2, /, *, construction: bool = False
) -> LineEntity:
    return LineEntity(id, start, end, construction)


def circle(
    id: str, center: Point2, radius: Length, /, *, construction: bool = False
) -> CircleEntity:
    return CircleEntity(id, center, radius, construction)


def arc(
    id: str,
    center: Point2,
    radius: Length,
    start_angle: Angle,
    end_angle: Angle,
    /,
    *,
    construction: bool = False,
) -> ArcEntity:
    return ArcEntity(id, center, radius, start_angle, end_angle, construction)


def at(entity: str, /) -> PointRef:
    return PointRef(entity, "point")


def start(entity: str, /) -> PointRef:
    return PointRef(entity, "start")


def end(entity: str, /) -> PointRef:
    return PointRef(entity, "end")


def center(entity: str, /) -> PointRef:
    return PointRef(entity, "center")


def _point_pair(id: str, kind: str, first: PointRef, second: PointRef) -> Constraint:
    return Constraint(id, kind, points=(first, second))


def _entity_pair(id: str, kind: str, first: str, second: str) -> Constraint:
    return Constraint(id, kind, entities=(first, second))


def coincident(id: str, first: PointRef, second: PointRef, /) -> Constraint:
    return _point_pair(id, "coincident", first, second)


def horizontal(id: str, line: str, /) -> Constraint:
    return Constraint(id, "horizontal", entities=(line,))


def vertical(id: str, line: str, /) -> Constraint:
    return Constraint(id, "vertical", entities=(line,))


def parallel(id: str, first: str, second: str, /) -> Constraint:
    return _entity_pair(id, "parallel", first, second)


def perpendicular(id: str, first: str, second: str, /) -> Constraint:
    return _entity_pair(id, "perpendicular", first, second)


def tangent(
    id: str, first: str, second: str, /, *, mode: str = "external"
) -> Constraint:
    return Constraint(id, "tangent", entities=(first, second), mode=mode)


def concentric(id: str, first: str, second: str, /) -> Constraint:
    return _entity_pair(id, "concentric", first, second)


def equal(id: str, first: str, second: str, /) -> Constraint:
    return _entity_pair(id, "equal", first, second)


def midpoint(id: str, point: PointRef, line: str, /) -> Constraint:
    return Constraint(id, "midpoint", points=(point,), entities=(line,))


def fixed(id: str, entity: str, /) -> Constraint:
    return Constraint(id, "fixed", entities=(entity,))


def collinear(id: str, first: str, second: str, /) -> Constraint:
    return _entity_pair(id, "collinear", first, second)


def distance(
    id: str, first: PointRef, second: PointRef, value: Length, /
) -> Constraint:
    return Constraint(id, "distance", points=(first, second), value=value)


def horizontal_distance(
    id: str, first: PointRef, second: PointRef, value: Length, /
) -> Constraint:
    return Constraint(id, "horizontal_distance", points=(first, second), value=value)


def vertical_distance(
    id: str, first: PointRef, second: PointRef, value: Length, /
) -> Constraint:
    return Constraint(id, "vertical_distance", points=(first, second), value=value)


def radius(id: str, curve: str, value: Length, /) -> Constraint:
    return Constraint(id, "radius", entities=(curve,), value=value)


def diameter(id: str, curve: str, value: Length, /) -> Constraint:
    return Constraint(id, "diameter", entities=(curve,), value=value)


def angle(id: str, first: str, second: str, value: Angle, /) -> Constraint:
    return Constraint(id, "angle", entities=(first, second), value=value)


def validate_definition_values(
    entities: tuple[Entity, ...], constraints: tuple[Constraint, ...]
) -> tuple[tuple[Entity, ...], tuple[Constraint, ...]]:
    entities = tuple(entities)
    constraints = tuple(constraints)
    if not all(type(value) in _ENTITY_KIND_BY_TYPE for value in entities):
        raise SketchDefinitionError("sketch entity is invalid")
    if not all(isinstance(value, Constraint) for value in constraints):
        raise SketchDefinitionError("sketch constraint is invalid")
    by_id = {value.id: value for value in entities}
    if len(by_id) != len(entities):
        raise SketchDefinitionError("entity stable ID is duplicated")
    if len({value.id for value in constraints}) != len(constraints):
        raise SketchDefinitionError("constraint stable ID is duplicated")
    for constraint in constraints:
        references = (
            *constraint.entities,
            *(reference.entity for reference in constraint.points),
        )
        if any(reference not in by_id for reference in references):
            raise SketchDefinitionError("constraint references a missing entity")
        for reference in constraint.points:
            kind = _ENTITY_KIND_BY_TYPE[type(by_id[reference.entity])]
            if kind not in _POINT_TARGETS[reference.key]:
                raise SketchDefinitionError("point key does not match entity")
        spec = HELPERS[constraint.kind]
        arguments = tuple(
            value for value in spec.positional if value.kind == "entity_ref"
        )
        kinds = tuple(
            _ENTITY_KIND_BY_TYPE[type(by_id[reference])]
            for reference in constraint.entities
        )
        if any(
            kind not in argument.entity_kinds
            for kind, argument in zip(kinds, arguments, strict=True)
        ):
            raise SketchDefinitionError(
                "constraint reference has an incompatible entity kind"
            )
        if spec.entity_combinations and not any(
            all(
                kind in allowed
                for kind, allowed in zip(kinds, combination, strict=True)
            )
            for combination in spec.entity_combinations
        ):
            raise SketchDefinitionError("constraint entity combination is invalid")
    return entities, constraints


__all__ = [
    "ArcEntity",
    "CircleEntity",
    "Constraint",
    "Entity",
    "LineEntity",
    "Point2",
    "PointEntity",
    "PointRef",
    "SketchDefinitionError",
    "SketchValue",
    "angle",
    "arc",
    "at",
    "center",
    "circle",
    "coincident",
    "collinear",
    "concentric",
    "diameter",
    "distance",
    "end",
    "equal",
    "fixed",
    "horizontal",
    "horizontal_distance",
    "line",
    "midpoint",
    "parallel",
    "perpendicular",
    "point",
    "radius",
    "start",
    "tangent",
    "validate_definition_values",
    "validate_stable_id",
    "vertical",
    "vertical_distance",
]
