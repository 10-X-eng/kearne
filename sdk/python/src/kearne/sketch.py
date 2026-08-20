"""Native build123d sketch helpers with stable source identities."""

from __future__ import annotations

from collections.abc import Iterator, Sequence
from dataclasses import dataclass
from math import degrees, hypot
from typing import TypeAlias
from uuid import UUID

from build123d import Edge, Face, Plane, Sketch, Wire

from kearne._sketch_schema import (
    ANGLE_TOLERANCE_RADIANS,
    HELPER_SPECS,
    HELPERS,
    MAXIMUM_ARC_SPAN_RADIANS,
    MAXIMUM_COORDINATE_METRES,
    MINIMUM_LENGTH_METRES,
    POINT_REFERENCE_HELPERS,
    WIRE_JOIN_TOLERANCE_MILLIMETRES,
)
from kearne.units import Angle, Length

Point2: TypeAlias = tuple[Length, Length]
Bounds: TypeAlias = tuple[tuple[float, float, float], tuple[float, float, float]]


class SketchDefinitionError(ValueError):
    """A recognized sketch definition is structurally invalid."""


def _stable_id(value: str) -> str:
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
        object.__setattr__(self, "id", _stable_id(self.id))
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
        object.__setattr__(self, "id", _stable_id(self.id))
        object.__setattr__(self, "start", _point(self.start))
        object.__setattr__(self, "end", _point(self.end))
        if (
            hypot(
                self.end[0].metres - self.start[0].metres,
                self.end[1].metres - self.start[1].metres,
            )
            < MINIMUM_LENGTH_METRES
        ):
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
        object.__setattr__(self, "id", _stable_id(self.id))
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
        object.__setattr__(self, "id", _stable_id(self.id))
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
        object.__setattr__(self, "entity", _stable_id(self.entity))
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
        object.__setattr__(self, "id", _stable_id(self.id))
        object.__setattr__(self, "points", tuple(self.points))
        object.__setattr__(
            self, "entities", tuple(_stable_id(value) for value in self.entities)
        )
        spec = HELPERS.get(self.kind)
        if spec is None or spec.section != "constraints":
            raise SketchDefinitionError("constraint kind is invalid")
        arguments = spec.positional[1:]
        point_count = sum(argument.kind == "point_ref" for argument in arguments)
        entity_count = sum(argument.kind == "entity_ref" for argument in arguments)
        if len(self.points) != point_count or len(self.entities) != entity_count:
            raise SketchDefinitionError("constraint reference count is invalid")
        if not all(isinstance(reference, PointRef) for reference in self.points):
            raise SketchDefinitionError("constraint point reference is invalid")
        value = next(
            (
                argument
                for argument in arguments
                if argument.kind in {"length", "angle"}
            ),
            None,
        )
        value_type = Length if value is not None and value.kind == "length" else Angle
        if value is not None and not isinstance(self.value, value_type):
            raise SketchDefinitionError("constraint value has the wrong quantity type")
        if value is None and self.value is not None:
            raise SketchDefinitionError("constraint does not accept a value")
        if value is not None and isinstance(self.value, Length):
            if value.limit == "positive" and self.value.metres <= 0.0:
                raise SketchDefinitionError("constraint value is not positive")
            if value.limit == "nonnegative" and self.value.metres < 0.0:
                raise SketchDefinitionError("constraint value is negative")
        mode_spec = next(
            (keyword for keyword in spec.keywords if keyword.name == "mode"), None
        )
        if mode_spec is not None:
            mode = self.mode or "external"
            if mode not in mode_spec.values:
                raise SketchDefinitionError("tangency mode is invalid")
            object.__setattr__(self, "mode", mode)
        elif self.mode is not None:
            raise SketchDefinitionError("constraint does not accept a mode")


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


@dataclass(frozen=True, slots=True)
class SketchPlane:
    """A trusted evaluated plane bound to stable project identity."""

    attachment_id: str
    frame: Plane

    def __post_init__(self) -> None:
        object.__setattr__(self, "attachment_id", _stable_id(self.attachment_id))
        if not isinstance(self.frame, Plane):
            raise SketchDefinitionError("sketch frame is not a build123d Plane")


def _millimetres(value: Length) -> float:
    return value.in_unit(0.001)


def _edges_intersect_away_from_shared_endpoints(first: Edge, second: Edge) -> bool:
    intersection = first.intersect(
        second,
        tolerance=WIRE_JOIN_TOLERANCE_MILLIMETRES,
        include_touched=True,
    )
    if intersection is None:
        return False
    if intersection.edges():
        return True
    first_ends = first.vertices()
    second_ends = second.vertices()
    return any(
        not all(
            any(
                vertex.distance_to(endpoint) <= WIRE_JOIN_TOLERANCE_MILLIMETRES
                for endpoint in endpoints
            )
            for endpoints in (first_ends, second_ends)
        )
        for vertex in intersection.vertices()
    )


def _bounds(shapes: Sequence[Edge | Wire]) -> list[Bounds]:
    result: list[Bounds] = []
    for shape in shapes:
        box = shape.bounding_box(optimal=False)
        result.append(
            (
                (box.min.X, box.min.Y, box.min.Z),
                (box.max.X, box.max.Y, box.max.Z),
            )
        )
    return result


def _overlapping_pairs(bounds: Sequence[Bounds]) -> Iterator[tuple[int, int]]:
    if not bounds:
        return
    axis = max(
        range(3),
        key=lambda selected: (
            max(box[1][selected] for box in bounds)
            - min(box[0][selected] for box in bounds)
        ),
    )
    ordered = sorted(range(len(bounds)), key=lambda index: bounds[index][0][axis])
    active: list[int] = []
    for current in ordered:
        lower = bounds[current][0][axis] - WIRE_JOIN_TOLERANCE_MILLIMETRES
        active = [index for index in active if bounds[index][1][axis] >= lower]
        for previous in active:
            if all(
                bounds[previous][0][coordinate]
                <= bounds[current][1][coordinate] + WIRE_JOIN_TOLERANCE_MILLIMETRES
                and bounds[current][0][coordinate]
                <= bounds[previous][1][coordinate] + WIRE_JOIN_TOLERANCE_MILLIMETRES
                for coordinate in range(3)
            ):
                yield previous, current
        active.append(current)


def _contains_bounds(container: Bounds, contained: Bounds) -> bool:
    return all(
        container[0][coordinate]
        <= contained[0][coordinate] + WIRE_JOIN_TOLERANCE_MILLIMETRES
        and container[1][coordinate]
        >= contained[1][coordinate] - WIRE_JOIN_TOLERANCE_MILLIMETRES
        for coordinate in range(3)
    )


@dataclass(frozen=True, slots=True)
class SketchDefinition:
    plane: SketchPlane
    entities: tuple[Entity, ...]
    constraints: tuple[Constraint, ...] = ()

    def __post_init__(self) -> None:
        if not isinstance(self.plane, SketchPlane):
            raise SketchDefinitionError("sketch plane has no stable attachment")
        entities = tuple(self.entities)
        constraints = tuple(self.constraints)
        object.__setattr__(self, "entities", entities)
        object.__setattr__(self, "constraints", constraints)
        if not all(
            isinstance(entity, (PointEntity, LineEntity, CircleEntity, ArcEntity))
            for entity in entities
        ):
            raise SketchDefinitionError("sketch entity is invalid")
        if not all(isinstance(constraint, Constraint) for constraint in constraints):
            raise SketchDefinitionError("sketch constraint is invalid")
        by_id = {entity.id: entity for entity in entities}
        if len(by_id) != len(entities):
            raise SketchDefinitionError("entity stable ID is duplicated")
        if len({constraint.id for constraint in constraints}) != len(constraints):
            raise SketchDefinitionError("constraint stable ID is duplicated")
        for constraint in constraints:
            references = (
                *constraint.entities,
                *(reference.entity for reference in constraint.points),
            )
            if any(reference not in by_id for reference in references):
                raise SketchDefinitionError("constraint references a missing entity")
            for reference in constraint.points:
                selected = by_id[reference.entity]
                entity_kind = _ENTITY_KIND_BY_TYPE[type(selected)]
                if entity_kind not in _POINT_TARGETS[reference.key]:
                    raise SketchDefinitionError("point key does not match entity")
            spec = HELPERS[constraint.kind]
            entity_arguments = tuple(
                argument
                for argument in spec.positional
                if argument.kind == "entity_ref"
            )
            entity_kinds = tuple(
                _ENTITY_KIND_BY_TYPE[type(by_id[reference])]
                for reference in constraint.entities
            )
            if any(
                kind not in argument.entity_kinds
                for kind, argument in zip(entity_kinds, entity_arguments, strict=True)
            ):
                raise SketchDefinitionError(
                    "constraint reference has an incompatible entity kind"
                )
            if spec.entity_combinations and not any(
                all(
                    kind in allowed
                    for kind, allowed in zip(entity_kinds, combination, strict=True)
                )
                for combination in spec.entity_combinations
            ):
                raise SketchDefinitionError("constraint entity combination is invalid")

    def build(self) -> Sketch:
        """Build faces from the definition's current solved coordinates."""
        if self.constraints:
            raise SketchDefinitionError(
                "constrained sketch must be solved before build"
            )
        edges: list[Edge] = []
        for entity in self.entities:
            if entity.construction or isinstance(entity, PointEntity):
                continue
            if isinstance(entity, LineEntity):
                edge = Edge.make_line(
                    self.plane.frame.from_local_coords(
                        (*(_millimetres(value) for value in entity.start), 0.0)
                    ),
                    self.plane.frame.from_local_coords(
                        (*(_millimetres(value) for value in entity.end), 0.0)
                    ),
                )
            else:
                curve_plane = Plane(
                    origin=self.plane.frame.from_local_coords(
                        (*(_millimetres(value) for value in entity.center), 0.0)
                    ),
                    x_dir=self.plane.frame.x_dir,
                    z_dir=self.plane.frame.z_dir,
                )
                if isinstance(entity, CircleEntity):
                    edge = Edge.make_circle(_millimetres(entity.radius), curve_plane)
                else:
                    edge = Edge.make_circle(
                        _millimetres(entity.radius),
                        curve_plane,
                        degrees(entity.start_angle.radians),
                        degrees(entity.end_angle.radians),
                    )
            edges.append(edge)

        edge_bounds = _bounds(edges)
        if any(
            _edges_intersect_away_from_shared_endpoints(edges[first], edges[second])
            for first, second in _overlapping_pairs(edge_bounds)
        ):
            raise SketchDefinitionError("sketch profiles touch or intersect")
        wires = Wire.combine(edges, tol=WIRE_JOIN_TOLERANCE_MILLIMETRES)
        closed_wires = [wire for wire in wires if wire.is_closed]
        open_wires = [wire for wire in wires if not wire.is_closed]
        if open_wires:
            raise SketchDefinitionError("sketch contains an open profile")
        try:
            profile_faces = [Face(wire) for wire in closed_wires]
        except (RuntimeError, ValueError) as error:
            raise SketchDefinitionError("sketch profile is invalid") from error
        if any(not wire.is_valid for wire in closed_wires) or any(
            not face.is_valid or face.area <= WIRE_JOIN_TOLERANCE_MILLIMETRES**2
            for face in profile_faces
        ):
            raise SketchDefinitionError("sketch profile is invalid")
        wire_bounds = _bounds(closed_wires)
        if any(
            closed_wires[first].distance_to(closed_wires[second])
            <= WIRE_JOIN_TOLERANCE_MILLIMETRES
            for first, second in _overlapping_pairs(wire_bounds)
        ):
            raise SketchDefinitionError("sketch profiles touch or intersect")
        areas = [face.area for face in profile_faces]
        parents: list[int | None] = []
        for index, wire in enumerate(closed_wires):
            sample = wire.position_at(0.173)
            containers = [
                candidate
                for candidate, candidate_face in enumerate(profile_faces)
                if candidate != index
                and areas[candidate] > areas[index]
                and _contains_bounds(wire_bounds[candidate], wire_bounds[index])
                and candidate_face.is_inside(sample)
            ]
            parent = min(containers, key=areas.__getitem__) if containers else None
            parents.append(parent)

        def depth(index: int) -> int:
            result = 0
            parent = parents[index]
            while parent is not None:
                result += 1
                parent = parents[parent]
            return result

        faces: list[Face] = []
        for index, wire in enumerate(closed_wires):
            if depth(index) % 2 != 0:
                continue
            holes = [
                candidate_wire
                for candidate, candidate_wire in enumerate(closed_wires)
                if parents[candidate] == index and depth(candidate) % 2 == 1
            ]
            face = Face(wire, holes)
            if not face.is_valid:
                raise SketchDefinitionError("sketch profile nesting is invalid")
            face.created_on = self.plane.frame
            faces.append(face)
        return Sketch(faces)


__all__ = [
    "SketchDefinition",
    "SketchDefinitionError",
    "SketchPlane",
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
    "vertical",
    "vertical_distance",
]
