"""Native build123d sketch helpers with stable source identities."""

from __future__ import annotations

from collections.abc import Iterator, Sequence
from dataclasses import dataclass
from math import cos, degrees, sin
from typing import TypeAlias

from build123d import AngularDirection, Edge, Face, Plane, Sketch, Wire
from OCP.BRepBuilderAPI import BRepBuilderAPI_MakeEdge
from OCP.GC import GC_MakeArcOfHyperbola, GC_MakeArcOfParabola
from OCP.gp import gp_Hypr, gp_Parab

from kearne._sketch_schema import WIRE_JOIN_TOLERANCE_MILLIMETRES
from kearne.sketch_values import (
    ArcEntity as ArcEntity,
)
from kearne.sketch_values import (
    BSplineEntity,
    CircleEntity,
    Constraint,
    Entity,
    LineEntity,
    PointEntity,
    SketchDefinitionError,
    SketchObject,
    angle,
    arc,
    arc_object,
    arc_slot,
    at,
    block,
    bspline,
    bspline_object,
    center,
    chamfer_object,
    circle,
    circle_object,
    coincident,
    collinear,
    concentric,
    curve_group,
    diameter,
    distance,
    ellipse,
    ellipse_object,
    elliptical_arc,
    elliptical_arc_object,
    end,
    equal,
    fillet_object,
    focus,
    group,
    horizontal,
    horizontal_distance,
    hyperbolic_arc,
    hyperbolic_arc_object,
    joined_curve_object,
    line,
    line_object,
    lock,
    major,
    midpoint,
    minor,
    oblong,
    offset_object,
    parabolic_arc,
    parabolic_arc_object,
    parallel,
    perpendicular,
    point,
    point_object,
    point_on_object,
    polyline,
    radius,
    rectangle,
    regular_polygon,
    slot,
    snell,
    start,
    symmetric,
    symmetric_about_point,
    tangent,
    validate_sketch_values,
    validate_stable_id,
    vertical,
    vertical_distance,
)
from kearne.sketch_values import (
    EllipseEntity as EllipseEntity,
)
from kearne.sketch_values import (
    EllipticalArcEntity as EllipticalArcEntity,
)
from kearne.sketch_values import (
    HyperbolicArcEntity as HyperbolicArcEntity,
)
from kearne.sketch_values import (
    ParabolicArcEntity as ParabolicArcEntity,
)
from kearne.sketch_values import (
    PointRef as PointRef,
)
from kearne.units import Length

Bounds: TypeAlias = tuple[tuple[float, float, float], tuple[float, float, float]]


@dataclass(frozen=True, slots=True)
class SketchPlane:
    """A trusted evaluated plane bound to stable project identity."""

    attachment_id: str
    frame: Plane

    def __post_init__(self) -> None:
        object.__setattr__(
            self, "attachment_id", validate_stable_id(self.attachment_id)
        )
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
    objects: tuple[SketchObject, ...] = ()

    def __post_init__(self) -> None:
        if not isinstance(self.plane, SketchPlane):
            raise SketchDefinitionError("sketch plane has no stable attachment")
        objects, entities, constraints = validate_sketch_values(
            self.objects, self.entities, self.constraints
        )
        object.__setattr__(self, "objects", objects)
        object.__setattr__(self, "entities", entities)
        object.__setattr__(self, "constraints", constraints)

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
            elif isinstance(entity, BSplineEntity):
                edge = Edge.make_bspline(
                    [
                        self.plane.frame.from_local_coords(
                            (*(_millimetres(value) for value in point), 0.0)
                        )
                        for point in entity.control_points
                    ],
                    entity.knots,
                    entity.degree,
                    entity.weights,
                )
            else:
                anchor = (
                    entity.vertex
                    if isinstance(entity, ParabolicArcEntity)
                    else entity.center
                )
                curve_plane = Plane(
                    origin=self.plane.frame.from_local_coords(
                        (*(_millimetres(value) for value in anchor), 0.0)
                    ),
                    x_dir=self.plane.frame.x_dir,
                    z_dir=self.plane.frame.z_dir,
                )
                if isinstance(entity, CircleEntity):
                    edge = Edge.make_circle(_millimetres(entity.radius), curve_plane)
                elif isinstance(entity, ArcEntity):
                    edge = Edge.make_circle(
                        _millimetres(entity.radius),
                        curve_plane,
                        degrees(entity.start_angle.radians),
                        degrees(entity.end_angle.radians),
                    )
                else:
                    rotation = entity.rotation.radians
                    ellipse_plane = Plane(
                        origin=curve_plane.origin,
                        x_dir=(
                            curve_plane.x_dir * cos(rotation)
                            + curve_plane.y_dir * sin(rotation)
                        ),
                        z_dir=curve_plane.z_dir,
                    )
                    arguments: tuple[float, ...] = ()
                    direction = AngularDirection.COUNTER_CLOCKWISE
                    if isinstance(entity, EllipticalArcEntity):
                        arguments = (
                            degrees(entity.start_parameter.radians),
                            degrees(entity.end_parameter.radians),
                        )
                        if (
                            entity.end_parameter.radians
                            < entity.start_parameter.radians
                        ):
                            direction = AngularDirection.CLOCKWISE
                    if isinstance(entity, HyperbolicArcEntity):
                        curve = GC_MakeArcOfHyperbola(
                            gp_Hypr(
                                ellipse_plane.to_gp_ax2(),
                                _millimetres(entity.major_radius),
                                _millimetres(entity.minor_radius),
                            ),
                            entity.start_parameter,
                            entity.end_parameter,
                            True,
                        ).Value()
                        edge = Edge(BRepBuilderAPI_MakeEdge(curve).Edge())
                    elif isinstance(entity, ParabolicArcEntity):
                        curve = GC_MakeArcOfParabola(
                            gp_Parab(
                                ellipse_plane.to_gp_ax2(),
                                _millimetres(entity.focal_length),
                            ),
                            _millimetres(entity.start_parameter),
                            _millimetres(entity.end_parameter),
                            True,
                        ).Value()
                        edge = Edge(BRepBuilderAPI_MakeEdge(curve).Edge())
                    else:
                        edge = Edge.make_ellipse(
                            _millimetres(entity.major_radius),
                            _millimetres(entity.minor_radius),
                            ellipse_plane,
                            *arguments,
                            angular_direction=direction,
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
    "SketchObject",
    "SketchPlane",
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
    "snell",
    "start",
    "symmetric",
    "symmetric_about_point",
    "tangent",
    "vertical",
    "vertical_distance",
]
