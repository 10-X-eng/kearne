"""Declarative source and runtime contract for recognized sketch helpers."""

from __future__ import annotations

from collections.abc import Mapping
from dataclasses import dataclass
from math import tau
from types import MappingProxyType
from typing import Literal, TypeAlias

EntityKind: TypeAlias = Literal["point", "line", "circle", "arc"]
Section: TypeAlias = Literal["entities", "constraints", "references"]
ValueKind: TypeAlias = Literal[
    "stable_id", "point", "length", "angle", "entity_ref", "point_ref"
]
ValueLimit: TypeAlias = Literal["positive", "nonnegative"]
KeywordKind: TypeAlias = Literal["boolean", "enum"]

ALL_ENTITIES = frozenset({"point", "line", "circle", "arc"})
POINTS = frozenset({"point"})
LINES = frozenset({"line"})
RADIAL = frozenset({"circle", "arc"})

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
    point_key: str | None = None
    entity_combinations: tuple[tuple[frozenset[str], ...], ...] = ()


ID = ArgumentSpec("id", "stable_id")
ANY_ENTITY = ArgumentSpec("entity", "entity_ref", ALL_ENTITIES)
LINE = ArgumentSpec("line", "entity_ref", LINES)
FIRST_LINE = ArgumentSpec("first", "entity_ref", LINES)
SECOND_LINE = ArgumentSpec("second", "entity_ref", LINES)
FIRST_RADIAL = ArgumentSpec("first", "entity_ref", RADIAL)
SECOND_RADIAL = ArgumentSpec("second", "entity_ref", RADIAL)
FIRST_POINT = ArgumentSpec("first", "point_ref")
SECOND_POINT = ArgumentSpec("second", "point_ref")
CONSTRUCTION = KeywordSpec("construction", "boolean", default=False)

HELPER_SPECS = (
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
        "at",
        "references",
        (ArgumentSpec("entity", "entity_ref", POINTS),),
        point_key="point",
    ),
    HelperSpec(
        "start",
        "references",
        (ArgumentSpec("entity", "entity_ref", LINES),),
        point_key="start",
    ),
    HelperSpec(
        "end",
        "references",
        (ArgumentSpec("entity", "entity_ref", LINES),),
        point_key="end",
    ),
    HelperSpec(
        "center",
        "references",
        (ArgumentSpec("entity", "entity_ref", RADIAL),),
        point_key="center",
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
    HelperSpec("concentric", "constraints", (ID, FIRST_RADIAL, SECOND_RADIAL)),
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
    HelperSpec("fixed", "constraints", (ID, ANY_ENTITY)),
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
ENTITY_HELPERS = frozenset(
    spec.name for spec in HELPER_SPECS if spec.section == "entities"
)
CONSTRAINT_HELPERS = frozenset(
    spec.name for spec in HELPER_SPECS if spec.section == "constraints"
)
POINT_REFERENCE_HELPERS = frozenset(
    spec.name for spec in HELPER_SPECS if spec.section == "references"
)
