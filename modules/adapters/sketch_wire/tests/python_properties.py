#!/usr/bin/env python3
"""Generated and cross-language properties for the sketch wire boundary."""

from __future__ import annotations

import argparse
from math import pi
from pathlib import Path
import subprocess
import sys
import unittest
from uuid import UUID

arguments_parser = argparse.ArgumentParser()
arguments_parser.add_argument("--bridge", type=Path, required=True)
arguments_parser.add_argument("--python-root", type=Path, required=True)
arguments_parser.add_argument("--sdk-root", type=Path, required=True)
arguments = arguments_parser.parse_args()
sys.path.insert(0, str(arguments.sdk_root))
sys.path.append(str(arguments.python_root))

from build123d import Plane  # noqa: E402
from hypothesis import given, settings, strategies as st  # noqa: E402

from kearne.sketch import (  # noqa: E402
    ArcEntity,
    CircleEntity,
    Constraint,
    LineEntity,
    PointEntity,
    PointRef,
    SketchDefinition,
    SketchPlane,
)
from kearne.sketch_wire import (  # noqa: E402
    SketchWireError,
    definition_from_wire,
    definition_to_wire,
    parse_definition,
    serialize_definition,
)
from kearne.source import source_digest  # noqa: E402
from kearne.units import Angle, Length  # noqa: E402


def uuid7(index: int) -> str:
    timestamp = (1_700_000_000_000 + index % 1_000_000_000).to_bytes(6, "big")
    tail = bytearray((index >> ((offset % 8) * 8)) & 0xFF for offset in range(10))
    tail[0] = 0x70 | (tail[0] & 0x0F)
    tail[2] = 0x80 | (tail[2] & 0x3F)
    return str(UUID(bytes=timestamp + bytes(tail)))


PLANE = SketchPlane(uuid7(900_000), Plane.XY)


def complete_definition(seed: int) -> SketchDefinition:
    offset = (seed % 1000) * 1.0e-6
    point_id = uuid7(seed + 1)
    first_line = uuid7(seed + 2)
    second_line = uuid7(seed + 3)
    first_circle = uuid7(seed + 4)
    second_circle = uuid7(seed + 5)
    arc_id = uuid7(seed + 6)
    entities = (
        PointEntity(point_id, (Length(offset), Length(0.002))),
        LineEntity(
            first_line,
            (Length(0.0), Length(0.0)),
            (Length(0.04 + offset), Length(0.0)),
        ),
        LineEntity(
            second_line,
            (Length(0.0), Length(0.02)),
            (Length(0.04), Length(0.02 + offset)),
        ),
        CircleEntity(first_circle, (Length(0.0), Length(0.0)), Length(0.01)),
        CircleEntity(second_circle, (Length(0.02), Length(0.0)), Length(0.01)),
        ArcEntity(
            arc_id,
            (Length(0.03), Length(0.03)),
            Length(0.005),
            Angle(0.1),
            Angle(1.7),
            seed % 2 == 0,
        ),
    )
    point = PointRef(point_id, "point")
    start = PointRef(first_line, "start")
    end = PointRef(first_line, "end")
    center = PointRef(first_circle, "center")

    def constraint_id(index: int) -> str:
        return uuid7(seed + 100 + index)

    constraints = (
        Constraint(constraint_id(1), "coincident", (point, start)),
        Constraint(constraint_id(2), "horizontal", entities=(first_line,)),
        Constraint(constraint_id(3), "vertical", entities=(second_line,)),
        Constraint(constraint_id(4), "parallel", entities=(first_line, second_line)),
        Constraint(
            constraint_id(5), "perpendicular", entities=(first_line, second_line)
        ),
        Constraint(
            constraint_id(6),
            "tangent",
            entities=(first_line, first_circle),
            mode="internal" if seed % 2 == 0 else "external",
        ),
        Constraint(
            constraint_id(7),
            "concentric",
            entities=(first_circle, second_circle),
        ),
        Constraint(constraint_id(8), "equal", entities=(first_line, second_line)),
        Constraint(constraint_id(9), "midpoint", (center,), (first_line,)),
        Constraint(constraint_id(10), "fixed", entities=(arc_id,)),
        Constraint(constraint_id(11), "collinear", entities=(first_line, second_line)),
        Constraint(constraint_id(12), "distance", (point, end), value=Length(0.03)),
        Constraint(
            constraint_id(13),
            "horizontal_distance",
            (point, end),
            value=Length(0.03 + offset),
        ),
        Constraint(
            constraint_id(14),
            "vertical_distance",
            (point, end),
            value=Length(-0.002),
        ),
        Constraint(
            constraint_id(15), "radius", entities=(first_circle,), value=Length(0.01)
        ),
        Constraint(
            constraint_id(16),
            "diameter",
            entities=(second_circle,),
            value=Length(0.02),
        ),
        Constraint(
            constraint_id(17),
            "angle",
            entities=(first_line, second_line),
            value=Angle(pi / 7.0),
        ),
    )
    if seed % 2 == 0:
        entities = tuple(reversed(entities))
    if seed % 3 == 0:
        constraints = tuple(reversed(constraints))
    return SketchDefinition(PLANE, entities, constraints)


class PythonSketchWireProperties(unittest.TestCase):
    @settings(max_examples=300, deadline=None)
    @given(st.integers(min_value=1, max_value=(1 << 63) - 1))
    def test_generated_round_trip_preserves_typed_definition(self, seed: int) -> None:
        definition = complete_definition(seed)
        digest = source_digest(f"seed={seed}\n")
        message = definition_to_wire(definition, digest)
        recovered = definition_from_wire(message, PLANE)
        self.assertEqual(recovered.source_digest, digest)
        self.assertEqual(recovered.definition, definition)
        parsed = parse_definition(serialize_definition(definition, digest), PLANE)
        self.assertEqual(parsed, recovered)

    def test_unknown_missing_and_nonfinite_fields_fail_closed(self) -> None:
        definition = complete_definition(17)
        digest = source_digest("")
        message = definition_to_wire(definition, digest)

        unknown = type(message)()
        unknown.ParseFromString(message.SerializeToString() + b"\xba\x3e\x06future")
        with self.assertRaisesRegex(SketchWireError, "unsupported executable"):
            definition_from_wire(unknown, PLANE)

        missing = type(message)()
        missing.CopyFrom(message)
        missing.entities[0].ClearField("construction")
        with self.assertRaisesRegex(SketchWireError, "missing"):
            definition_from_wire(missing, PLANE)

        nonfinite = type(message)()
        nonfinite.CopyFrom(message)
        first = nonfinite.entities[0]
        payload = getattr(first, first.WhichOneof("geometry"))
        point = payload.at if first.WhichOneof("geometry") == "point" else payload.start
        point.x = float("nan")
        with self.assertRaisesRegex(SketchWireError, "finite"):
            definition_from_wire(nonfinite, PLANE)

        with self.assertRaisesRegex(SketchWireError, "invalid"):
            parse_definition(message.SerializeToString() + b"\x80", PLANE)

    def test_cpp_round_trip_accepts_every_registered_member(self) -> None:
        for seed in range(1, 65):
            with self.subTest(seed=seed):
                definition = complete_definition(seed)
                digest = source_digest(f"cross-language={seed}\n")
                message = definition_to_wire(definition, digest)
                result = subprocess.run(
                    [arguments.bridge],
                    input=message.SerializeToString(),
                    capture_output=True,
                    check=True,
                )
                recovered = type(message)()
                recovered.ParseFromString(result.stdout)
                self.assertEqual(
                    definition_from_wire(recovered, PLANE),
                    definition_from_wire(message, PLANE),
                )


if __name__ == "__main__":
    unittest.main(argv=[sys.argv[0]], verbosity=2)
