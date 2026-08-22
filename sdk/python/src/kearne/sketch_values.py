"""CAD-runtime-independent values for recognized Sketch source."""

from __future__ import annotations

from dataclasses import dataclass
from itertools import pairwise
from math import atanh, cos, cosh, hypot, isfinite, sin, sinh
from typing import TypeAlias
from uuid import UUID

from kearne._sketch_schema import (
    ANGLE_TOLERANCE_RADIANS,
    HELPER_SPECS,
    HELPERS,
    LENGTH_TOLERANCE_METRES,
    MAXIMUM_ARC_SPAN_RADIANS,
    MAXIMUM_COORDINATE_METRES,
    MINIMUM_LENGTH_METRES,
    POINT_REFERENCE_HELPERS,
    ObjectKind,
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


@dataclass(frozen=True, slots=True)
class EllipseEntity:
    id: str
    center: Point2
    major_radius: Length
    minor_radius: Length
    rotation: Angle
    construction: bool = False

    def __post_init__(self) -> None:
        object.__setattr__(self, "id", validate_stable_id(self.id))
        object.__setattr__(self, "center", _point(self.center))
        if (
            not isinstance(self.major_radius, Length)
            or not isinstance(self.minor_radius, Length)
            or not isinstance(self.rotation, Angle)
            or self.major_radius.metres < MINIMUM_LENGTH_METRES
            or self.major_radius.metres > MAXIMUM_COORDINATE_METRES
            or self.minor_radius.metres < MINIMUM_LENGTH_METRES
            or self.minor_radius.metres > self.major_radius.metres
        ):
            raise SketchDefinitionError("ellipse axes are invalid")
        if not isinstance(self.construction, bool):
            raise SketchDefinitionError("construction flag is not boolean")


@dataclass(frozen=True, slots=True)
class EllipticalArcEntity:
    id: str
    center: Point2
    major_radius: Length
    minor_radius: Length
    rotation: Angle
    start_parameter: Angle
    end_parameter: Angle
    construction: bool = False

    def __post_init__(self) -> None:
        EllipseEntity(
            self.id,
            self.center,
            self.major_radius,
            self.minor_radius,
            self.rotation,
            self.construction,
        )
        if not isinstance(self.start_parameter, Angle) or not isinstance(
            self.end_parameter, Angle
        ):
            raise SketchDefinitionError("elliptical arc parameter is invalid")
        span = abs(self.end_parameter.radians - self.start_parameter.radians)
        if span < ANGLE_TOLERANCE_RADIANS:
            raise SketchDefinitionError("elliptical arc has no parameter span")
        if span > MAXIMUM_ARC_SPAN_RADIANS:
            raise SketchDefinitionError(
                "elliptical arc parameter span exceeds one revolution"
            )


def _scalar(value: float) -> float:
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        raise SketchDefinitionError("scalar parameter is not numeric")
    result = float(value)
    if not isfinite(result):
        raise SketchDefinitionError("scalar parameter is not finite")
    return 0.0 if result == 0.0 else result


def _within(parameter: float, first: float, second: float) -> bool:
    return min(first, second) <= parameter <= max(first, second)


@dataclass(frozen=True, slots=True)
class HyperbolicArcEntity:
    id: str
    center: Point2
    major_radius: Length
    minor_radius: Length
    rotation: Angle
    start_parameter: float
    end_parameter: float
    construction: bool = False

    def __post_init__(self) -> None:
        object.__setattr__(self, "id", validate_stable_id(self.id))
        object.__setattr__(self, "center", _point(self.center))
        object.__setattr__(self, "start_parameter", _scalar(self.start_parameter))
        object.__setattr__(self, "end_parameter", _scalar(self.end_parameter))
        if (
            not isinstance(self.major_radius, Length)
            or not isinstance(self.minor_radius, Length)
            or not isinstance(self.rotation, Angle)
            or self.major_radius.metres < MINIMUM_LENGTH_METRES
            or self.major_radius.metres > MAXIMUM_COORDINATE_METRES
            or self.minor_radius.metres < MINIMUM_LENGTH_METRES
            or self.minor_radius.metres > MAXIMUM_COORDINATE_METRES
        ):
            raise SketchDefinitionError("hyperbola axes are invalid")
        if abs(self.end_parameter - self.start_parameter) < ANGLE_TOLERANCE_RADIANS:
            raise SketchDefinitionError("hyperbolic arc has no parameter span")
        if not isinstance(self.construction, bool):
            raise SketchDefinitionError("construction flag is not boolean")
        try:
            cosine = cos(self.rotation.radians)
            sine = sin(self.rotation.radians)
            parameters = [self.start_parameter, self.end_parameter]
            for sinh_coefficient, cosh_coefficient in (
                (cosine * self.major_radius.metres, -sine * self.minor_radius.metres),
                (sine * self.major_radius.metres, cosine * self.minor_radius.metres),
            ):
                if sinh_coefficient and abs(cosh_coefficient) < abs(sinh_coefficient):
                    candidate = atanh(-cosh_coefficient / sinh_coefficient)
                    if _within(candidate, self.start_parameter, self.end_parameter):
                        parameters.append(candidate)
            for parameter in parameters:
                local_x = self.major_radius.metres * cosh(parameter)
                local_y = self.minor_radius.metres * sinh(parameter)
                x = self.center[0].metres + cosine * local_x - sine * local_y
                y = self.center[1].metres + sine * local_x + cosine * local_y
                if (
                    not isfinite(x)
                    or not isfinite(y)
                    or abs(x) > MAXIMUM_COORDINATE_METRES
                    or abs(y) > MAXIMUM_COORDINATE_METRES
                ):
                    raise SketchDefinitionError(
                        "hyperbolic arc exceeds the supported coordinate range"
                    )
        except OverflowError as error:
            raise SketchDefinitionError(
                "hyperbolic arc exceeds the supported coordinate range"
            ) from error


@dataclass(frozen=True, slots=True)
class ParabolicArcEntity:
    id: str
    vertex: Point2
    focal_length: Length
    rotation: Angle
    start_parameter: Length
    end_parameter: Length
    construction: bool = False

    def __post_init__(self) -> None:
        object.__setattr__(self, "id", validate_stable_id(self.id))
        object.__setattr__(self, "vertex", _point(self.vertex))
        if (
            not isinstance(self.focal_length, Length)
            or not isinstance(self.rotation, Angle)
            or not isinstance(self.start_parameter, Length)
            or not isinstance(self.end_parameter, Length)
            or self.focal_length.metres < MINIMUM_LENGTH_METRES
            or self.focal_length.metres > MAXIMUM_COORDINATE_METRES
        ):
            raise SketchDefinitionError("parabola parameters are invalid")
        if (
            abs(self.end_parameter.metres - self.start_parameter.metres)
            < MINIMUM_LENGTH_METRES
        ):
            raise SketchDefinitionError("parabolic arc has no parameter span")
        if not isinstance(self.construction, bool):
            raise SketchDefinitionError("construction flag is not boolean")
        sine = sin(self.rotation.radians)
        cosine = cos(self.rotation.radians)
        first = self.start_parameter.metres
        second = self.end_parameter.metres
        parameters = [first, second]
        for divisor, numerator in ((cosine, sine), (sine, -cosine)):
            if divisor:
                candidate = 2.0 * self.focal_length.metres * numerator / divisor
                if _within(candidate, first, second):
                    parameters.append(candidate)
        for parameter in parameters:
            local_x = parameter * parameter / (4.0 * self.focal_length.metres)
            x = self.vertex[0].metres + cosine * local_x - sine * parameter
            y = self.vertex[1].metres + sine * local_x + cosine * parameter
            if (
                not isfinite(x)
                or not isfinite(y)
                or abs(x) > MAXIMUM_COORDINATE_METRES
                or abs(y) > MAXIMUM_COORDINATE_METRES
            ):
                raise SketchDefinitionError(
                    "parabolic arc exceeds the supported coordinate range"
                )


def _bspline_point(
    control_points: tuple[Point2, ...],
    knots: tuple[float, ...],
    weights: tuple[float, ...],
    degree: int,
    parameter: float,
) -> tuple[float, float]:
    count = len(control_points)
    parameter = min(max(parameter, knots[degree]), knots[count])
    span = count - 1
    if parameter < knots[count]:
        span = next(
            index
            for index in range(degree, count)
            if knots[index] <= parameter < knots[index + 1]
        )
    values = [
        [
            control_points[span - degree + index][0].metres
            * weights[span - degree + index],
            control_points[span - degree + index][1].metres
            * weights[span - degree + index],
            weights[span - degree + index],
        ]
        for index in range(degree + 1)
    ]
    for level in range(1, degree + 1):
        for index in range(degree, level - 1, -1):
            knot = span - degree + index
            denominator = knots[index + 1 + span - level] - knots[knot]
            alpha = (
                0.0 if denominator == 0.0 else (parameter - knots[knot]) / denominator
            )
            values[index] = [
                (1.0 - alpha) * values[index - 1][coordinate]
                + alpha * values[index][coordinate]
                for coordinate in range(3)
            ]
    return values[degree][0] / values[degree][2], values[degree][1] / values[degree][2]


@dataclass(frozen=True, slots=True)
class BSplineEntity:
    id: str
    control_points: tuple[Point2, ...]
    knots: tuple[float, ...]
    weights: tuple[float, ...]
    degree: int
    periodic: bool = False
    construction: bool = False

    def __post_init__(self) -> None:
        object.__setattr__(self, "id", validate_stable_id(self.id))
        if not isinstance(self.control_points, tuple):
            raise SketchDefinitionError("B-spline control points are not a tuple")
        control_points = tuple(_point(point) for point in self.control_points)
        object.__setattr__(self, "control_points", control_points)
        if not 2 <= len(control_points) <= 1_024:
            raise SketchDefinitionError("B-spline control point count is invalid")
        if (
            isinstance(self.degree, bool)
            or not isinstance(self.degree, int)
            or not 1 <= self.degree <= 25
            or self.degree >= len(control_points)
        ):
            raise SketchDefinitionError("B-spline degree is invalid")
        if not isinstance(self.knots, tuple) or not isinstance(self.weights, tuple):
            raise SketchDefinitionError("B-spline knots and weights are not tuples")
        knots = tuple(_scalar(value) for value in self.knots)
        weights = tuple(_scalar(value) for value in self.weights)
        object.__setattr__(self, "knots", knots)
        object.__setattr__(self, "weights", weights)
        if (
            len(knots) != len(control_points) + self.degree + 1
            or any(first > second for first, second in pairwise(knots))
            or not knots[self.degree] < knots[len(control_points)]
        ):
            raise SketchDefinitionError("B-spline knot sequence is invalid")
        if len(weights) != len(control_points) or any(
            weight < 1.0e-12 or weight > 1.0e12 for weight in weights
        ):
            raise SketchDefinitionError("B-spline weights are invalid")
        first = control_points[0]
        if not any(
            hypot(
                point[0].metres - first[0].metres,
                point[1].metres - first[1].metres,
            )
            >= MINIMUM_LENGTH_METRES
            for point in control_points[1:]
        ):
            raise SketchDefinitionError("B-spline is degenerate")
        if not isinstance(self.periodic, bool) or not isinstance(
            self.construction, bool
        ):
            raise SketchDefinitionError("B-spline flags are not boolean")
        if self.periodic:
            first_multiplicity = knots.count(knots[self.degree])
            last_multiplicity = knots.count(knots[len(control_points)])
            tail_count = self.degree + 1 - first_multiplicity
            canonical_seam = (
                first_multiplicity == last_multiplicity
                and 0 < first_multiplicity <= self.degree
                and tail_count <= len(control_points) // 2
                and all(
                    control_points[index]
                    == control_points[len(control_points) - tail_count + index]
                    and weights[index]
                    == weights[len(control_points) - tail_count + index]
                    for index in range(tail_count)
                )
            )
            if not canonical_seam:
                raise SketchDefinitionError(
                    "periodic B-spline has no canonical repeated seam"
                )
            start = _bspline_point(
                control_points, knots, weights, self.degree, knots[self.degree]
            )
            end = _bspline_point(
                control_points,
                knots,
                weights,
                self.degree,
                knots[len(control_points)],
            )
            if hypot(end[0] - start[0], end[1] - start[1]) > LENGTH_TOLERANCE_METRES:
                raise SketchDefinitionError("periodic B-spline is open")


Entity: TypeAlias = (
    PointEntity
    | LineEntity
    | CircleEntity
    | ArcEntity
    | EllipseEntity
    | EllipticalArcEntity
    | HyperbolicArcEntity
    | ParabolicArcEntity
    | BSplineEntity
)
_ENTITY_KIND_BY_TYPE = {
    PointEntity: "point",
    LineEntity: "line",
    CircleEntity: "circle",
    ArcEntity: "arc",
    EllipseEntity: "ellipse",
    EllipticalArcEntity: "elliptical_arc",
    HyperbolicArcEntity: "hyperbolic_arc",
    ParabolicArcEntity: "parabolic_arc",
    BSplineEntity: "bspline",
}
_POINT_TARGETS = {
    spec.point_key: spec.positional[0].entity_kinds
    for spec in HELPER_SPECS
    if spec.name in POINT_REFERENCE_HELPERS
}


@dataclass(frozen=True, slots=True)
class SketchObject:
    id: str
    label: str
    kind: ObjectKind
    entities: tuple[str, ...]
    roles: tuple[str, ...] = ()

    def __post_init__(self) -> None:
        object.__setattr__(self, "id", validate_stable_id(self.id))
        if (
            not isinstance(self.label, str)
            or not self.label.strip()
            or len(self.label.encode()) > 128
            or any(
                ord(character) < 0x20 or ord(character) == 0x7F
                for character in self.label
            )
        ):
            raise SketchDefinitionError("sketch object label is invalid")
        member_counts = {
            "point": 1,
            "line": 1,
            "circle": 1,
            "arc": 1,
            "ellipse": 1,
            "elliptical_arc": 1,
            "hyperbolic_arc": 1,
            "parabolic_arc": 1,
            "bspline": 1,
            "fillet": 1,
            "chamfer": 1,
            "offset": 1,
            "joined_curve": 1,
            "rectangle": 4,
            "slot": 4,
            "oblong": 4,
            "arc_slot": 4,
        }
        if (
            self.kind not in {"polyline", "regular_polygon", "curve_group"}
            and self.kind not in member_counts
        ):
            raise SketchDefinitionError("sketch object kind is invalid")
        object.__setattr__(
            self,
            "entities",
            tuple(validate_stable_id(value) for value in self.entities),
        )
        object.__setattr__(self, "roles", tuple(self.roles))
        if self.kind == "curve_group":
            if (
                not self.entities
                or len(self.roles) != len(self.entities)
                or len(set(self.roles)) != len(self.roles)
                or any(
                    not isinstance(role, str)
                    or not role
                    or len(role.encode()) > 32
                    or any(
                        not (
                            character.isascii()
                            and (character.islower() or character.isdigit())
                        )
                        and character != "_"
                        for character in role
                    )
                    for role in self.roles
                )
            ):
                raise SketchDefinitionError("sketch curve-group members are invalid")
        elif self.roles:
            raise SketchDefinitionError("sketch object roles are inferred by kind")
        expected_count = member_counts.get(self.kind)
        if (
            (
                self.kind != "curve_group"
                and expected_count is None
                and not self.entities
            )
            or (self.kind == "regular_polygon" and len(self.entities) < 3)
            or (expected_count is not None and len(self.entities) != expected_count)
            or len(set(self.entities)) != len(self.entities)
        ):
            raise SketchDefinitionError("sketch object members are invalid")


@dataclass(frozen=True, slots=True)
class PointRef:
    entity: str
    key: str

    def __post_init__(self) -> None:
        object.__setattr__(self, "entity", validate_stable_id(self.entity))
        if self.key not in {"point", "start", "end", "center", "major", "minor"}:
            raise SketchDefinitionError("point reference key is invalid")


@dataclass(frozen=True, slots=True)
class Constraint:
    id: str
    kind: str
    points: tuple[PointRef, ...] = ()
    entities: tuple[str, ...] = ()
    value: Length | Angle | None = None
    mode: str | None = None
    position: Point2 | None = None

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
        has_entity_set = any(value.kind == "entity_refs" for value in arguments)
        fixed_entity_count = sum(value.kind == "entity_ref" for value in arguments)
        if (
            (has_entity_set and not 2 <= len(self.entities) <= 1_024)
            or (not has_entity_set and len(self.entities) != fixed_entity_count)
            or (has_entity_set and len(set(self.entities)) != len(self.entities))
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
        accepts_position = any(value.kind == "point" for value in arguments)
        if accepts_position:
            if self.position is None:
                raise SketchDefinitionError("constraint position is missing")
            object.__setattr__(self, "position", _point(self.position))
        elif self.position is not None:
            raise SketchDefinitionError("constraint does not accept a position")
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


SketchValue: TypeAlias = SketchObject | Entity | Constraint


def point_object(id: str, label: str, point: str, /) -> SketchObject:
    return SketchObject(id, label, "point", (point,))


def line_object(id: str, label: str, curve: str, /) -> SketchObject:
    return SketchObject(id, label, "line", (curve,))


def polyline(id: str, label: str, segments: tuple[str, ...], /) -> SketchObject:
    return SketchObject(id, label, "polyline", segments)


def regular_polygon(id: str, label: str, sides: tuple[str, ...], /) -> SketchObject:
    if len(sides) < 3:
        raise SketchDefinitionError("regular polygon needs at least three sides")
    return SketchObject(id, label, "regular_polygon", sides)


def circle_object(id: str, label: str, curve: str, /) -> SketchObject:
    return SketchObject(id, label, "circle", (curve,))


def arc_object(id: str, label: str, curve: str, /) -> SketchObject:
    return SketchObject(id, label, "arc", (curve,))


def ellipse_object(id: str, label: str, curve: str, /) -> SketchObject:
    return SketchObject(id, label, "ellipse", (curve,))


def elliptical_arc_object(id: str, label: str, curve: str, /) -> SketchObject:
    return SketchObject(id, label, "elliptical_arc", (curve,))


def hyperbolic_arc_object(id: str, label: str, curve: str, /) -> SketchObject:
    return SketchObject(id, label, "hyperbolic_arc", (curve,))


def parabolic_arc_object(id: str, label: str, curve: str, /) -> SketchObject:
    return SketchObject(id, label, "parabolic_arc", (curve,))


def bspline_object(id: str, label: str, curve: str, /) -> SketchObject:
    return SketchObject(id, label, "bspline", (curve,))


def fillet_object(id: str, label: str, curve: str, /) -> SketchObject:
    return SketchObject(id, label, "fillet", (curve,))


def chamfer_object(id: str, label: str, curve: str, /) -> SketchObject:
    return SketchObject(id, label, "chamfer", (curve,))


def offset_object(id: str, label: str, curve: str, /) -> SketchObject:
    return SketchObject(id, label, "offset", (curve,))


def joined_curve_object(id: str, label: str, curve: str, /) -> SketchObject:
    return SketchObject(id, label, "joined_curve", (curve,))


def curve_group(
    id: str, label: str, members: tuple[tuple[str, str], ...], /
) -> SketchObject:
    members = tuple(members)
    if not members or any(
        not isinstance(member, tuple)
        or len(member) != 2
        or not all(isinstance(value, str) for value in member)
        for member in members
    ):
        raise SketchDefinitionError("sketch curve-group members are invalid")
    return SketchObject(
        id,
        label,
        "curve_group",
        tuple(member[1] for member in members),
        tuple(member[0] for member in members),
    )


def slot(
    id: str,
    label: str,
    start_cap: str,
    end_cap: str,
    top_side: str,
    bottom_side: str,
    /,
) -> SketchObject:
    return SketchObject(id, label, "slot", (start_cap, end_cap, top_side, bottom_side))


def oblong(
    id: str,
    label: str,
    start_cap: str,
    end_cap: str,
    top_side: str,
    bottom_side: str,
    /,
) -> SketchObject:
    return SketchObject(
        id, label, "oblong", (start_cap, end_cap, top_side, bottom_side)
    )


def arc_slot(
    id: str,
    label: str,
    outer: str,
    end_cap: str,
    inner: str,
    start_cap: str,
    /,
) -> SketchObject:
    return SketchObject(id, label, "arc_slot", (outer, end_cap, inner, start_cap))


def rectangle(
    id: str,
    label: str,
    bottom: str,
    right: str,
    top: str,
    left: str,
    /,
) -> SketchObject:
    return SketchObject(id, label, "rectangle", (bottom, right, top, left))


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


def ellipse(
    id: str,
    center: Point2,
    major_radius: Length,
    minor_radius: Length,
    rotation: Angle,
    /,
    *,
    construction: bool = False,
) -> EllipseEntity:
    return EllipseEntity(id, center, major_radius, minor_radius, rotation, construction)


def elliptical_arc(
    id: str,
    center: Point2,
    major_radius: Length,
    minor_radius: Length,
    rotation: Angle,
    start_parameter: Angle,
    end_parameter: Angle,
    /,
    *,
    construction: bool = False,
) -> EllipticalArcEntity:
    return EllipticalArcEntity(
        id,
        center,
        major_radius,
        minor_radius,
        rotation,
        start_parameter,
        end_parameter,
        construction,
    )


def hyperbolic_arc(
    id: str,
    center: Point2,
    major_radius: Length,
    minor_radius: Length,
    rotation: Angle,
    start_parameter: float,
    end_parameter: float,
    /,
    *,
    construction: bool = False,
) -> HyperbolicArcEntity:
    return HyperbolicArcEntity(
        id,
        center,
        major_radius,
        minor_radius,
        rotation,
        start_parameter,
        end_parameter,
        construction,
    )


def parabolic_arc(
    id: str,
    vertex: Point2,
    focal_length: Length,
    rotation: Angle,
    start_parameter: Length,
    end_parameter: Length,
    /,
    *,
    construction: bool = False,
) -> ParabolicArcEntity:
    return ParabolicArcEntity(
        id,
        vertex,
        focal_length,
        rotation,
        start_parameter,
        end_parameter,
        construction,
    )


def bspline(
    id: str,
    control_points: tuple[Point2, ...],
    knots: tuple[float, ...],
    weights: tuple[float, ...],
    degree: int,
    /,
    *,
    periodic: bool = False,
    construction: bool = False,
) -> BSplineEntity:
    return BSplineEntity(
        id,
        control_points,
        knots,
        weights,
        degree,
        periodic,
        construction,
    )


def at(entity: str, /) -> PointRef:
    return PointRef(entity, "point")


def start(entity: str, /) -> PointRef:
    return PointRef(entity, "start")


def end(entity: str, /) -> PointRef:
    return PointRef(entity, "end")


def center(entity: str, /) -> PointRef:
    return PointRef(entity, "center")


def major(entity: str, /) -> PointRef:
    return PointRef(entity, "major")


def minor(entity: str, /) -> PointRef:
    return PointRef(entity, "minor")


def focus(entity: str, /) -> PointRef:
    return PointRef(entity, "focus")


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


def point_on_object(id: str, point: PointRef, curve: str, /) -> Constraint:
    return Constraint(id, "point_on_object", points=(point,), entities=(curve,))


def symmetric(id: str, first: PointRef, second: PointRef, axis: str, /) -> Constraint:
    return Constraint(id, "symmetric", points=(first, second), entities=(axis,))


def symmetric_about_point(
    id: str, first: PointRef, second: PointRef, center: PointRef, /
) -> Constraint:
    return Constraint(id, "symmetric_about_point", points=(first, second, center))


def lock(id: str, point: PointRef, position: Point2, /) -> Constraint:
    return Constraint(id, "lock", points=(point,), position=position)


def block(id: str, entity: str, /) -> Constraint:
    return Constraint(id, "block", entities=(entity,))


def group(id: str, entities: tuple[str, ...], /) -> Constraint:
    return Constraint(id, "group", entities=entities)


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
        entity_set = next(
            (value for value in spec.positional if value.kind == "entity_refs"), None
        )
        kinds = tuple(
            _ENTITY_KIND_BY_TYPE[type(by_id[reference])]
            for reference in constraint.entities
        )
        if (
            entity_set is not None
            and any(kind not in entity_set.entity_kinds for kind in kinds)
        ) or (
            entity_set is None
            and any(
                kind not in argument.entity_kinds
                for kind, argument in zip(kinds, arguments, strict=True)
            )
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


def validate_sketch_values(
    objects: tuple[SketchObject, ...],
    entities: tuple[Entity, ...],
    constraints: tuple[Constraint, ...],
) -> tuple[tuple[SketchObject, ...], tuple[Entity, ...], tuple[Constraint, ...]]:
    objects = tuple(objects)
    entities, constraints = validate_definition_values(entities, constraints)
    if not all(isinstance(value, SketchObject) for value in objects):
        raise SketchDefinitionError("sketch object is invalid")
    if len({value.id for value in objects}) != len(objects):
        raise SketchDefinitionError("sketch object stable ID is duplicated")
    if len({value.label for value in objects}) != len(objects):
        raise SketchDefinitionError("sketch object label is duplicated")
    by_id = {value.id: value for value in entities}
    owned: set[str] = set()
    expected_types: dict[ObjectKind, type[object] | tuple[type[object], ...]] = {
        "point": PointEntity,
        "line": LineEntity,
        "circle": CircleEntity,
        "arc": ArcEntity,
        "ellipse": EllipseEntity,
        "elliptical_arc": EllipticalArcEntity,
        "hyperbolic_arc": HyperbolicArcEntity,
        "parabolic_arc": ParabolicArcEntity,
        "bspline": BSplineEntity,
        "fillet": ArcEntity,
        "chamfer": LineEntity,
        "offset": (LineEntity, CircleEntity, ArcEntity),
        "joined_curve": BSplineEntity,
        "curve_group": (
            LineEntity,
            CircleEntity,
            ArcEntity,
            EllipseEntity,
            EllipticalArcEntity,
            HyperbolicArcEntity,
            ParabolicArcEntity,
            BSplineEntity,
        ),
        "rectangle": LineEntity,
        "polyline": LineEntity,
        "regular_polygon": LineEntity,
        "arc_slot": ArcEntity,
    }
    for value in objects:
        for index, entity_id in enumerate(value.entities):
            if entity_id in owned:
                raise SketchDefinitionError("sketch entity has multiple owners")
            entity = by_id.get(entity_id)
            expected = (
                (ArcEntity, ArcEntity, LineEntity, LineEntity)[index]
                if value.kind in {"slot", "oblong"}
                else expected_types[value.kind]
            )
            if not isinstance(entity, expected):
                raise SketchDefinitionError("sketch object member has the wrong kind")
            owned.add(entity_id)
    return objects, entities, constraints


__all__ = [
    "ArcEntity",
    "BSplineEntity",
    "CircleEntity",
    "Constraint",
    "EllipseEntity",
    "EllipticalArcEntity",
    "Entity",
    "HyperbolicArcEntity",
    "LineEntity",
    "ParabolicArcEntity",
    "Point2",
    "PointEntity",
    "PointRef",
    "SketchDefinitionError",
    "SketchObject",
    "SketchValue",
    "angle",
    "arc",
    "arc_object",
    "arc_slot",
    "at",
    "block",
    "bspline",
    "bspline_object",
    "center",
    "chamfer_object",
    "circle",
    "circle_object",
    "coincident",
    "collinear",
    "concentric",
    "curve_group",
    "diameter",
    "distance",
    "ellipse",
    "ellipse_object",
    "elliptical_arc",
    "elliptical_arc_object",
    "end",
    "equal",
    "fillet_object",
    "focus",
    "group",
    "horizontal",
    "horizontal_distance",
    "hyperbolic_arc",
    "hyperbolic_arc_object",
    "joined_curve_object",
    "line",
    "line_object",
    "lock",
    "major",
    "midpoint",
    "minor",
    "oblong",
    "offset_object",
    "parabolic_arc",
    "parabolic_arc_object",
    "parallel",
    "perpendicular",
    "point",
    "point_object",
    "point_on_object",
    "polyline",
    "radius",
    "rectangle",
    "regular_polygon",
    "slot",
    "start",
    "symmetric",
    "symmetric_about_point",
    "tangent",
    "validate_definition_values",
    "validate_sketch_values",
    "validate_stable_id",
    "vertical",
    "vertical_distance",
]
