from __future__ import annotations

import ast
import unittest
from unittest.mock import patch
from uuid import UUID

import kearne.sketch as sketch_api
from hypothesis import given, settings
from hypothesis import strategies as st
from kearne._sketch_schema import CONSTRAINT_HELPERS, HELPERS
from kearne.sketch import (
    ArcEntity,
    CircleEntity,
    Constraint,
    Entity,
    LineEntity,
    PointEntity,
    PointRef,
)
from kearne.sketch_source import (
    append_value,
    delete_value,
    emit_call,
    parse_call,
    replace_value,
    values_from_source,
)
from kearne.source import (
    AppendCall,
    SourceError,
    open_edit_session,
    recognize,
)
from kearne.units import Angle, Length, m, rad


def uuid7(index: int) -> str:
    timestamp = (1_700_000_000_000 + index % 1_000_000_000).to_bytes(6, "big")
    tail = bytearray((index >> ((offset % 8) * 8)) & 0xFF for offset in range(10))
    tail[0] = 0x70 | (tail[0] & 0x0F)
    tail[2] = 0x80 | (tail[2] & 0x3F)
    return str(UUID(bytes=timestamp + bytes(tail)))


def values(seed: int) -> tuple[tuple[Entity, ...], tuple[Constraint, ...]]:
    offset = (seed % 1_000) * 1.0e-7
    ids = {
        "point": uuid7(seed + 1),
        "line": uuid7(seed + 2),
        "line_2": uuid7(seed + 3),
        "circle": uuid7(seed + 4),
        "circle_2": uuid7(seed + 5),
        "arc": uuid7(seed + 6),
    }
    entities: tuple[Entity, ...] = (
        PointEntity(ids["point"], (Length(offset), Length(0.002))),
        LineEntity(
            ids["line"],
            (Length(0.0), Length(0.0)),
            (Length(0.04 + offset), Length(0.0)),
            True,
        ),
        LineEntity(
            ids["line_2"],
            (Length(0.0), Length(0.02)),
            (Length(0.04), Length(0.02 + offset)),
        ),
        CircleEntity(ids["circle"], (Length(0.0), Length(0.0)), Length(0.01)),
        CircleEntity(ids["circle_2"], (Length(0.02), Length(0.0)), Length(0.01)),
        ArcEntity(
            ids["arc"],
            (Length(0.03), Length(0.03)),
            Length(0.005),
            Angle(0.1),
            Angle(1.7),
        ),
    )
    by_kind = {
        "point": ids["point"],
        "line": ids["line"],
        "circle": ids["circle"],
        "arc": ids["arc"],
    }
    point_references = iter(
        (
            PointRef(ids["point"], "point"),
            PointRef(ids["line"], "start"),
            PointRef(ids["line"], "end"),
            PointRef(ids["circle"], "center"),
        )
        * 16
    )
    constraints: list[Constraint] = []
    for ordinal, name in enumerate(sorted(CONSTRAINT_HELPERS)):
        spec = HELPERS[name]
        points: list[PointRef] = []
        entity_ids: list[str] = []
        entity_ordinal = 0
        for argument in spec.positional[1:]:
            if argument.kind == "point_ref":
                points.append(next(point_references))
            elif argument.kind == "entity_ref":
                allowed = (
                    spec.entity_combinations[0][entity_ordinal]
                    if spec.entity_combinations
                    else argument.entity_kinds
                )
                selected_kind = sorted(allowed)[0]
                selected = by_kind[selected_kind]
                if selected_kind == "line" and entity_ordinal > 0:
                    selected = ids["line_2"]
                elif selected_kind == "circle" and entity_ordinal > 0:
                    selected = ids["circle_2"]
                entity_ids.append(selected)
                entity_ordinal += 1
        quantity = next(
            (
                argument.kind
                for argument in spec.positional
                if argument.kind in {"length", "angle"}
            ),
            None,
        )
        value = Length(0.025) if quantity == "length" else None
        if quantity == "angle":
            value = Angle(0.5)
        constraints.append(
            Constraint(
                uuid7(seed + 100 + ordinal),
                name,
                tuple(points),
                tuple(entity_ids),
                value,
                "internal" if name == "tangent" else None,
            )
        )
    return entities, tuple(constraints)


def source_with(entities: tuple[Entity, ...]) -> str:
    helpers = ", ".join(sorted(HELPERS))
    calls = ",\n            ".join(emit_call(value) for value in entities)
    return f"""from build123d import Sketch
from kearne.sketch import SketchDefinition, SketchPlane, {helpers}
from kearne.units import m, rad

sentinel = "preserve this byte-for-byte"

def profile(plane: SketchPlane) -> Sketch:
    return SketchDefinition(
        plane=plane,
        entities=(
            {calls},
        ),
        constraints=(),
    ).build()
"""


class SketchSourceProperties(unittest.TestCase):
    @given(st.integers(min_value=1, max_value=1_000_000_000))
    @settings(max_examples=50, deadline=None)
    def test_all_typed_values_emit_and_edit_as_recognized_source(
        self, seed: int
    ) -> None:
        entities, constraints = values(seed)
        source = source_with(entities)
        initial = recognize(source, "profile")
        self.assertIsNotNone(initial)
        assert initial is not None
        self.assertEqual(
            {entry.kind for entry in initial.entities},
            {"point", "line", "circle", "arc"},
        )

        namespace = {name: getattr(sketch_api, name) for name in HELPERS}
        namespace.update({"m": m, "rad": rad})
        for value in (*entities, *constraints):
            call = emit_call(value)
            parsed = ast.parse(call, mode="eval")
            self.assertIsInstance(parsed.body, ast.Call)
            self.assertEqual(ast.unparse(parsed.body), call)
            reconstructed = eval(  # noqa: S307 -- generated, validated call only
                compile(parsed, "<generated-sketch-call>", "eval"),
                {"__builtins__": {}},
                namespace,
            )
            self.assertEqual(reconstructed, value)
            self.assertEqual(parse_call(call), value)
        self.assertIn("construction=True", emit_call(entities[1]))
        self.assertIn(
            "mode='internal'",
            emit_call(next(value for value in constraints if value.kind == "tangent")),
        )

        session = open_edit_session(source, "profile")
        self.assertIsNotNone(session)
        assert session is not None
        with patch("kearne.source.ast.parse", wraps=ast.parse) as parse:
            session = session.apply(
                session.source_digest,
                tuple(
                    AppendCall("constraints", emit_call(value)) for value in constraints
                ),
            )
        document_parses = sum(
            call.kwargs.get("mode", "exec") == "exec" for call in parse.call_args_list
        )
        self.assertEqual(document_parses, 1)
        current = session.source
        digest = session.source_digest
        recognized = recognize(current, "profile")
        self.assertIsNotNone(recognized)
        assert recognized is not None
        self.assertEqual(
            tuple(entry.kind for entry in recognized.constraints),
            tuple(value.kind for value in constraints),
        )
        self.assertIn('sentinel = "preserve this byte-for-byte"', current)
        _, decoded_entities, decoded_constraints = values_from_source(
            current, "profile"
        )
        self.assertEqual(decoded_entities, entities)
        self.assertEqual(decoded_constraints, constraints)

        replacement = LineEntity(
            entities[1].id,
            (Length(0.001), Length(0.002)),
            (Length(0.051), Length(0.002)),
            True,
        )
        replaced = replace_value(current, "profile", digest, replacement)
        deleted = delete_value(
            replaced.source,
            "profile",
            replaced.source_digest,
            constraints[-1],
        )
        final = recognize(deleted.source, "profile")
        self.assertIsNotNone(final)
        assert final is not None
        self.assertEqual(len(final.constraints), len(constraints) - 1)
        self.assertEqual(final.entities[1].code, emit_call(replacement))

        with self.assertRaises(SourceError) as stale:
            append_value(deleted.source, "profile", digest, constraints[-1])
        self.assertEqual(stale.exception.code, "source.edit.stale")


if __name__ == "__main__":
    unittest.main()
