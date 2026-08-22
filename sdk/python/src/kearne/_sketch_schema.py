"""Declarative source and runtime contract for recognized sketch helpers."""

from __future__ import annotations

from collections.abc import Mapping
from dataclasses import dataclass
from math import tau
from types import MappingProxyType
from typing import Literal, TypeAlias

EntityKind: TypeAlias = Literal[
    "point",
    "line",
    "circle",
    "arc",
    "ellipse",
    "elliptical_arc",
    "hyperbolic_arc",
    "parabolic_arc",
    "bspline",
]
ObjectKind: TypeAlias = Literal[
    "rectangle",
    "point",
    "line",
    "circle",
    "arc",
    "slot",
    "arc_slot",
    "polyline",
    "regular_polygon",
    "oblong",
    "ellipse",
    "elliptical_arc",
    "hyperbolic_arc",
    "parabolic_arc",
    "bspline",
    "fillet",
    "chamfer",
    "offset",
    "joined_curve",
    "curve_group",
]
Section: TypeAlias = Literal["objects", "entities", "constraints", "references"]
ValueKind: TypeAlias = Literal[
    "stable_id",
    "label",
    "point",
    "points",
    "length",
    "angle",
    "scalar",
    "scalars",
    "integer",
    "entity_ref",
    "entity_refs",
    "object_members",
    "point_ref",
]
ValueLimit: TypeAlias = Literal["positive", "nonnegative"]
KeywordKind: TypeAlias = Literal["boolean", "enum"]

ALL_ENTITIES = frozenset(
    {
        "point",
        "line",
        "circle",
        "arc",
        "ellipse",
        "elliptical_arc",
        "hyperbolic_arc",
        "parabolic_arc",
        "bspline",
    }
)
POINTS = frozenset({"point"})
LINES = frozenset({"line"})
RADIAL = frozenset({"circle", "arc"})
CENTERED_CURVES = frozenset(
    {"circle", "arc", "ellipse", "elliptical_arc", "hyperbolic_arc"}
)
ELLIPSES = frozenset({"ellipse", "elliptical_arc"})
AXIS_CONICS = frozenset({"ellipse", "elliptical_arc", "hyperbolic_arc"})
FOCAL_CONICS = frozenset({"hyperbolic_arc", "parabolic_arc"})
BSPLINES = frozenset({"bspline"})
ENDPOINT_CURVES = frozenset(
    {
        "line",
        "arc",
        "elliptical_arc",
        "hyperbolic_arc",
        "parabolic_arc",
        "bspline",
    }
)

# The C++ sketch numerical profile stores this tolerance in metres. build123d
# consumes millimetres, so profile assembly must convert it explicitly.
MINIMUM_LENGTH_METRES = 1.0e-9
MAXIMUM_COORDINATE_METRES = 1.0e6
LENGTH_TOLERANCE_METRES = 1.0e-8
ANGLE_TOLERANCE_RADIANS = 1.0e-9
MAXIMUM_ARC_SPAN_RADIANS = tau
WIRE_JOIN_TOLERANCE_MILLIMETRES = LENGTH_TOLERANCE_METRES * 1_000.0


@dataclass(frozen=True, slots=True)
class ArgumentSpec:
    name: str
    kind: ValueKind
    entity_kinds: frozenset[str] = frozenset()
    limit: ValueLimit | None = None


@dataclass(frozen=True, slots=True)
class KeywordSpec:
    name: str
    kind: KeywordKind
    values: frozenset[str] = frozenset()
    default: bool | str | None = None


@dataclass(frozen=True, slots=True)
class HelperSpec:
    name: str
    section: Section
    positional: tuple[ArgumentSpec, ...]
    keywords: tuple[KeywordSpec, ...] = ()
    entity_kind: EntityKind | None = None
    object_kind: ObjectKind | None = None
    point_key: str | None = None
    entity_combinations: tuple[tuple[frozenset[str], ...], ...] = ()


ID = ArgumentSpec("id", "stable_id")
ANY_ENTITY = ArgumentSpec("entity", "entity_ref", ALL_ENTITIES)
ANY_ENTITIES = ArgumentSpec("entities", "entity_refs", ALL_ENTITIES)
LINE = ArgumentSpec("line", "entity_ref", LINES)
FIRST_LINE = ArgumentSpec("first", "entity_ref", LINES)
SECOND_LINE = ArgumentSpec("second", "entity_ref", LINES)
FIRST_RADIAL = ArgumentSpec("first", "entity_ref", RADIAL)
SECOND_RADIAL = ArgumentSpec("second", "entity_ref", RADIAL)
CURVE = ArgumentSpec(
    "curve", "entity_ref", LINES | RADIAL | ELLIPSES | FOCAL_CONICS | BSPLINES
)
FIRST_POINT = ArgumentSpec("first", "point_ref")
SECOND_POINT = ArgumentSpec("second", "point_ref")
CONSTRUCTION = KeywordSpec("construction", "boolean", default=False)
PERIODIC = KeywordSpec("periodic", "boolean", default=False)

HELPER_SPECS = (
    HelperSpec(
        "point_object",
        "objects",
        (
            ID,
            ArgumentSpec("label", "label"),
            ArgumentSpec("point", "entity_ref", POINTS),
        ),
        object_kind="point",
    ),
    HelperSpec(
        "line_object",
        "objects",
        (
            ID,
            ArgumentSpec("label", "label"),
            ArgumentSpec("curve", "entity_ref", LINES),
        ),
        object_kind="line",
    ),
    HelperSpec(
        "polyline",
        "objects",
        (
            ID,
            ArgumentSpec("label", "label"),
            ArgumentSpec("segments", "entity_refs", LINES),
        ),
        object_kind="polyline",
    ),
    HelperSpec(
        "regular_polygon",
        "objects",
        (
            ID,
            ArgumentSpec("label", "label"),
            ArgumentSpec("sides", "entity_refs", LINES),
        ),
        object_kind="regular_polygon",
    ),
    HelperSpec(
        "circle_object",
        "objects",
        (
            ID,
            ArgumentSpec("label", "label"),
            ArgumentSpec("curve", "entity_ref", frozenset({"circle"})),
        ),
        object_kind="circle",
    ),
    HelperSpec(
        "arc_object",
        "objects",
        (
            ID,
            ArgumentSpec("label", "label"),
            ArgumentSpec("curve", "entity_ref", frozenset({"arc"})),
        ),
        object_kind="arc",
    ),
    HelperSpec(
        "ellipse_object",
        "objects",
        (
            ID,
            ArgumentSpec("label", "label"),
            ArgumentSpec("curve", "entity_ref", frozenset({"ellipse"})),
        ),
        object_kind="ellipse",
    ),
    HelperSpec(
        "elliptical_arc_object",
        "objects",
        (
            ID,
            ArgumentSpec("label", "label"),
            ArgumentSpec("curve", "entity_ref", frozenset({"elliptical_arc"})),
        ),
        object_kind="elliptical_arc",
    ),
    HelperSpec(
        "hyperbolic_arc_object",
        "objects",
        (
            ID,
            ArgumentSpec("label", "label"),
            ArgumentSpec("curve", "entity_ref", frozenset({"hyperbolic_arc"})),
        ),
        object_kind="hyperbolic_arc",
    ),
    HelperSpec(
        "parabolic_arc_object",
        "objects",
        (
            ID,
            ArgumentSpec("label", "label"),
            ArgumentSpec("curve", "entity_ref", frozenset({"parabolic_arc"})),
        ),
        object_kind="parabolic_arc",
    ),
    HelperSpec(
        "bspline_object",
        "objects",
        (
            ID,
            ArgumentSpec("label", "label"),
            ArgumentSpec("curve", "entity_ref", frozenset({"bspline"})),
        ),
        object_kind="bspline",
    ),
    HelperSpec(
        "fillet_object",
        "objects",
        (
            ID,
            ArgumentSpec("label", "label"),
            ArgumentSpec("curve", "entity_ref", frozenset({"arc"})),
        ),
        object_kind="fillet",
    ),
    HelperSpec(
        "chamfer_object",
        "objects",
        (
            ID,
            ArgumentSpec("label", "label"),
            ArgumentSpec("curve", "entity_ref", frozenset({"line"})),
        ),
        object_kind="chamfer",
    ),
    HelperSpec(
        "offset_object",
        "objects",
        (
            ID,
            ArgumentSpec("label", "label"),
            ArgumentSpec("curve", "entity_ref", frozenset({"line", "circle", "arc"})),
        ),
        object_kind="offset",
    ),
    HelperSpec(
        "joined_curve_object",
        "objects",
        (
            ID,
            ArgumentSpec("label", "label"),
            ArgumentSpec("curve", "entity_ref", frozenset({"bspline"})),
        ),
        object_kind="joined_curve",
    ),
    HelperSpec(
        "curve_group",
        "objects",
        (
            ID,
            ArgumentSpec("label", "label"),
            ArgumentSpec("members", "object_members", ALL_ENTITIES - POINTS),
        ),
        object_kind="curve_group",
    ),
    HelperSpec(
        "slot",
        "objects",
        (
            ID,
            ArgumentSpec("label", "label"),
            ArgumentSpec("start_cap", "entity_ref", frozenset({"arc"})),
            ArgumentSpec("end_cap", "entity_ref", frozenset({"arc"})),
            ArgumentSpec("top_side", "entity_ref", LINES),
            ArgumentSpec("bottom_side", "entity_ref", LINES),
        ),
        object_kind="slot",
    ),
    HelperSpec(
        "oblong",
        "objects",
        (
            ID,
            ArgumentSpec("label", "label"),
            ArgumentSpec("start_cap", "entity_ref", frozenset({"arc"})),
            ArgumentSpec("end_cap", "entity_ref", frozenset({"arc"})),
            ArgumentSpec("top_side", "entity_ref", LINES),
            ArgumentSpec("bottom_side", "entity_ref", LINES),
        ),
        object_kind="oblong",
    ),
    HelperSpec(
        "arc_slot",
        "objects",
        (
            ID,
            ArgumentSpec("label", "label"),
            ArgumentSpec("outer", "entity_ref", frozenset({"arc"})),
            ArgumentSpec("end_cap", "entity_ref", frozenset({"arc"})),
            ArgumentSpec("inner", "entity_ref", frozenset({"arc"})),
            ArgumentSpec("start_cap", "entity_ref", frozenset({"arc"})),
        ),
        object_kind="arc_slot",
    ),
    HelperSpec(
        "rectangle",
        "objects",
        (
            ID,
            ArgumentSpec("label", "label"),
            ArgumentSpec("bottom", "entity_ref", LINES),
            ArgumentSpec("right", "entity_ref", LINES),
            ArgumentSpec("top", "entity_ref", LINES),
            ArgumentSpec("left", "entity_ref", LINES),
        ),
        object_kind="rectangle",
    ),
    HelperSpec(
        "point",
        "entities",
        (ID, ArgumentSpec("at", "point")),
        (CONSTRUCTION,),
        entity_kind="point",
    ),
    HelperSpec(
        "line",
        "entities",
        (ID, ArgumentSpec("start", "point"), ArgumentSpec("end", "point")),
        (CONSTRUCTION,),
        entity_kind="line",
    ),
    HelperSpec(
        "circle",
        "entities",
        (
            ID,
            ArgumentSpec("center", "point"),
            ArgumentSpec("radius", "length", limit="positive"),
        ),
        (CONSTRUCTION,),
        entity_kind="circle",
    ),
    HelperSpec(
        "arc",
        "entities",
        (
            ID,
            ArgumentSpec("center", "point"),
            ArgumentSpec("radius", "length", limit="positive"),
            ArgumentSpec("start_angle", "angle"),
            ArgumentSpec("end_angle", "angle"),
        ),
        (CONSTRUCTION,),
        entity_kind="arc",
    ),
    HelperSpec(
        "ellipse",
        "entities",
        (
            ID,
            ArgumentSpec("center", "point"),
            ArgumentSpec("major_radius", "length", limit="positive"),
            ArgumentSpec("minor_radius", "length", limit="positive"),
            ArgumentSpec("rotation", "angle"),
        ),
        (CONSTRUCTION,),
        entity_kind="ellipse",
    ),
    HelperSpec(
        "elliptical_arc",
        "entities",
        (
            ID,
            ArgumentSpec("center", "point"),
            ArgumentSpec("major_radius", "length", limit="positive"),
            ArgumentSpec("minor_radius", "length", limit="positive"),
            ArgumentSpec("rotation", "angle"),
            ArgumentSpec("start_parameter", "angle"),
            ArgumentSpec("end_parameter", "angle"),
        ),
        (CONSTRUCTION,),
        entity_kind="elliptical_arc",
    ),
    HelperSpec(
        "hyperbolic_arc",
        "entities",
        (
            ID,
            ArgumentSpec("center", "point"),
            ArgumentSpec("major_radius", "length", limit="positive"),
            ArgumentSpec("minor_radius", "length", limit="positive"),
            ArgumentSpec("rotation", "angle"),
            ArgumentSpec("start_parameter", "scalar"),
            ArgumentSpec("end_parameter", "scalar"),
        ),
        (CONSTRUCTION,),
        entity_kind="hyperbolic_arc",
    ),
    HelperSpec(
        "parabolic_arc",
        "entities",
        (
            ID,
            ArgumentSpec("vertex", "point"),
            ArgumentSpec("focal_length", "length", limit="positive"),
            ArgumentSpec("rotation", "angle"),
            ArgumentSpec("start_parameter", "length"),
            ArgumentSpec("end_parameter", "length"),
        ),
        (CONSTRUCTION,),
        entity_kind="parabolic_arc",
    ),
    HelperSpec(
        "bspline",
        "entities",
        (
            ID,
            ArgumentSpec("control_points", "points"),
            ArgumentSpec("knots", "scalars"),
            ArgumentSpec("weights", "scalars"),
            ArgumentSpec("degree", "integer"),
        ),
        (PERIODIC, CONSTRUCTION),
        entity_kind="bspline",
    ),
    HelperSpec(
        "at",
        "references",
        (ArgumentSpec("entity", "entity_ref", POINTS),),
        point_key="point",
    ),
    HelperSpec(
        "start",
        "references",
        (ArgumentSpec("entity", "entity_ref", ENDPOINT_CURVES),),
        point_key="start",
    ),
    HelperSpec(
        "end",
        "references",
        (ArgumentSpec("entity", "entity_ref", ENDPOINT_CURVES),),
        point_key="end",
    ),
    HelperSpec(
        "center",
        "references",
        (ArgumentSpec("entity", "entity_ref", CENTERED_CURVES),),
        point_key="center",
    ),
    HelperSpec(
        "major",
        "references",
        (ArgumentSpec("entity", "entity_ref", AXIS_CONICS),),
        point_key="major",
    ),
    HelperSpec(
        "minor",
        "references",
        (ArgumentSpec("entity", "entity_ref", AXIS_CONICS),),
        point_key="minor",
    ),
    HelperSpec(
        "focus",
        "references",
        (ArgumentSpec("entity", "entity_ref", FOCAL_CONICS),),
        point_key="focus",
    ),
    HelperSpec("coincident", "constraints", (ID, FIRST_POINT, SECOND_POINT)),
    HelperSpec("horizontal", "constraints", (ID, LINE)),
    HelperSpec("vertical", "constraints", (ID, LINE)),
    HelperSpec("parallel", "constraints", (ID, FIRST_LINE, SECOND_LINE)),
    HelperSpec("perpendicular", "constraints", (ID, FIRST_LINE, SECOND_LINE)),
    HelperSpec(
        "tangent",
        "constraints",
        (
            ID,
            ArgumentSpec("first", "entity_ref", LINES | RADIAL),
            ArgumentSpec("second", "entity_ref", LINES | RADIAL),
        ),
        (
            KeywordSpec(
                "mode",
                "enum",
                frozenset({"external", "internal"}),
                "external",
            ),
        ),
        entity_combinations=((LINES, RADIAL), (RADIAL, LINES), (RADIAL, RADIAL)),
    ),
    HelperSpec(
        "concentric",
        "constraints",
        (
            ID,
            ArgumentSpec("first", "entity_ref", CENTERED_CURVES),
            ArgumentSpec("second", "entity_ref", CENTERED_CURVES),
        ),
    ),
    HelperSpec(
        "equal",
        "constraints",
        (
            ID,
            ArgumentSpec("first", "entity_ref", LINES | RADIAL),
            ArgumentSpec("second", "entity_ref", LINES | RADIAL),
        ),
        entity_combinations=((LINES, LINES), (RADIAL, RADIAL)),
    ),
    HelperSpec(
        "midpoint", "constraints", (ID, ArgumentSpec("point", "point_ref"), LINE)
    ),
    HelperSpec(
        "point_on_object",
        "constraints",
        (ID, ArgumentSpec("point", "point_ref"), CURVE),
    ),
    HelperSpec(
        "symmetric",
        "constraints",
        (ID, FIRST_POINT, SECOND_POINT, ArgumentSpec("axis", "entity_ref", LINES)),
    ),
    HelperSpec(
        "symmetric_about_point",
        "constraints",
        (ID, FIRST_POINT, SECOND_POINT, ArgumentSpec("center", "point_ref")),
    ),
    HelperSpec(
        "lock",
        "constraints",
        (
            ID,
            ArgumentSpec("point", "point_ref"),
            ArgumentSpec("position", "point"),
        ),
    ),
    HelperSpec("block", "constraints", (ID, ANY_ENTITY)),
    HelperSpec("group", "constraints", (ID, ANY_ENTITIES)),
    HelperSpec("collinear", "constraints", (ID, FIRST_LINE, SECOND_LINE)),
    HelperSpec(
        "distance",
        "constraints",
        (
            ID,
            FIRST_POINT,
            SECOND_POINT,
            ArgumentSpec("value", "length", limit="nonnegative"),
        ),
    ),
    HelperSpec(
        "horizontal_distance",
        "constraints",
        (ID, FIRST_POINT, SECOND_POINT, ArgumentSpec("value", "length")),
    ),
    HelperSpec(
        "vertical_distance",
        "constraints",
        (ID, FIRST_POINT, SECOND_POINT, ArgumentSpec("value", "length")),
    ),
    HelperSpec(
        "radius",
        "constraints",
        (
            ID,
            ArgumentSpec("curve", "entity_ref", RADIAL),
            ArgumentSpec("value", "length", limit="positive"),
        ),
    ),
    HelperSpec(
        "diameter",
        "constraints",
        (
            ID,
            ArgumentSpec("curve", "entity_ref", RADIAL),
            ArgumentSpec("value", "length", limit="positive"),
        ),
    ),
    HelperSpec(
        "angle",
        "constraints",
        (ID, FIRST_LINE, SECOND_LINE, ArgumentSpec("value", "angle")),
    ),
)

HELPERS: Mapping[str, HelperSpec] = MappingProxyType(
    {spec.name: spec for spec in HELPER_SPECS}
)
OBJECT_HELPERS = frozenset(
    spec.name for spec in HELPER_SPECS if spec.section == "objects"
)
ENTITY_HELPERS = frozenset(
    spec.name for spec in HELPER_SPECS if spec.section == "entities"
)
CONSTRAINT_HELPERS = frozenset(
    spec.name for spec in HELPER_SPECS if spec.section == "constraints"
)
POINT_REFERENCE_HELPERS = frozenset(
    spec.name for spec in HELPER_SPECS if spec.section == "references"
)
