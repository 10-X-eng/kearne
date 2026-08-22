from __future__ import annotations

import unittest
from inspect import Parameter, signature
from itertools import product
from math import cosh, inf, nextafter, pi, sinh
from uuid import UUID

import kearne.sketch as sketch_api
from build123d import Plane
from hypothesis import given, settings
from hypothesis import strategies as st
from hypothesis.stateful import RuleBasedStateMachine, invariant, precondition, rule
from kearne._sketch_schema import (
    ALL_ENTITIES,
    ANGLE_TOLERANCE_RADIANS,
    HELPER_SPECS,
    MAXIMUM_ARC_SPAN_RADIANS,
    MAXIMUM_COORDINATE_METRES,
    MINIMUM_LENGTH_METRES,
    WIRE_JOIN_TOLERANCE_MILLIMETRES,
)
from kearne.sketch import (
    Constraint,
    PointRef,
    SketchDefinition,
    SketchDefinitionError,
    SketchPlane,
    arc,
    bspline,
    circle,
    ellipse,
    elliptical_arc,
    horizontal,
    hyperbolic_arc,
    line,
    parabolic_arc,
    point,
)
from kearne.source import (
    MAXIMUM_SOURCE_EDIT_BATCH,
    AppendCall,
    DeleteCall,
    ReplaceCall,
    SourceError,
    apply_edit,
    open_edit_session,
    recognize,
    source_digest,
)
from kearne.units import Angle, Length, deg, mm


def uuid7(value: int) -> str:
    timestamp = value & ((1 << 48) - 1)
    random_a = (value >> 48) & 0xFFF
    random_b = (value * 0x9E3779B97F4A7C15) & ((1 << 62) - 1)
    bits = (timestamp << 80) | (7 << 76) | (random_a << 64) | (2 << 62) | random_b
    return str(UUID(int=bits))


def sketch_source(entity_id: str, constraint_id: str) -> str:
    return f'''from build123d import Sketch
from kearne.sketch import SketchDefinition, SketchPlane, horizontal, line, point
from kearne.units import Length, mm

sentinel = "unrelated text"

def profile(plane: SketchPlane, width: Length) -> Sketch:
    untouched = width * 2  # must survive structural edits
    return SketchDefinition(
        plane=plane,
        entities=(
            line("{entity_id}", (mm(0), mm(0)), (width, mm(0))),  # source identity
        ),
        constraints=(
            horizontal("{constraint_id}", "{entity_id}"),
        ),
    ).build()
'''


def layout_source(
    entity_ids: tuple[str, ...],
    *,
    use_list: bool,
    multiline: bool,
    trailing_comma: bool,
    class_scoped: bool,
    newline: str,
) -> tuple[str, str]:
    function_indent = "    " if class_scoped else ""
    body_indent = function_indent + "    "
    keyword_indent = body_indent + "    "
    entry_indent = keyword_indent + "    "
    calls = [
        f'line("{stable}", (mm(len("λ")), mm(0)), (mm(1), mm(0)))'
        for stable in entity_ids
    ]
    left, right = ("[", "]") if use_list else ("(", ")")
    if multiline:
        rows = [
            f"{entry_indent}{call},{'  # retained' if index else ''}"
            for index, call in enumerate(calls)
        ]
        if len(rows) > 1:
            rows.insert(1, f"{entry_indent}# standalone retained")
        if rows and not trailing_comma:
            rows[-1] = rows[-1].replace(",  # retained", "  # retained")
        section = newline.join((left, *rows, keyword_indent + right))
    else:
        suffix = "," if trailing_comma else ""
        section = f"{left}{', '.join(calls)}{suffix}{right}"
    owner = "class Carrier:" + newline if class_scoped else ""
    source = newline.join(
        (
            "from build123d import Sketch",
            "from kearne.sketch import SketchDefinition, SketchPlane, line, point",
            "from kearne.units import mm",
            "",
            owner.rstrip("\r\n"),
            f"{function_indent}def profile(plane: SketchPlane) -> Sketch:",
            f"{body_indent}return SketchDefinition(",
            f"{keyword_indent}plane=plane,",
            f"{keyword_indent}entities={section},",
            f"{keyword_indent}constraints=(),",
            f"{body_indent}).build()",
            "",
        )
    )
    return source, "Carrier.profile" if class_scoped else "profile"


def entity_of_kind(kind: str, stable: str) -> sketch_api.Entity:
    if kind == "point":
        return point(stable, (mm(0), mm(0)))
    if kind == "line":
        return line(stable, (mm(0), mm(0)), (mm(1), mm(0)))
    if kind == "circle":
        return circle(stable, (mm(0), mm(0)), mm(1))
    if kind == "arc":
        return arc(stable, (mm(0), mm(0)), mm(1), deg(0), deg(90))
    if kind == "ellipse":
        return ellipse(stable, (mm(0), mm(0)), mm(2), mm(1), deg(15))
    if kind == "elliptical_arc":
        return elliptical_arc(
            stable, (mm(0), mm(0)), mm(2), mm(1), deg(15), deg(0), deg(90)
        )
    if kind == "hyperbolic_arc":
        return hyperbolic_arc(stable, (mm(0), mm(0)), mm(2), mm(3), deg(15), -0.5, 0.5)
    if kind == "parabolic_arc":
        return parabolic_arc(stable, (mm(0), mm(0)), mm(1), deg(15), mm(-1), mm(1))
    if kind == "bspline":
        return bspline(
            stable,
            ((mm(0), mm(0)), (mm(1), mm(1)), (mm(2), mm(0))),
            (0.0, 0.0, 0.0, 1.0, 1.0, 1.0),
            (1.0, 1.0, 1.0),
            2,
        )
    raise AssertionError(f"unknown entity kind {kind}")


class SourceProperties(unittest.TestCase):
    def test_source_digest_matches_cross_language_content_vectors(self) -> None:
        vectors = {
            "": (
                "blake3:6e82d967b887a378d96d00d3e8d8fc8c"
                "72247cdcb197b6ee6815a9af954f1e4d"
            ),
            "café\r\nΔ = 1\r\n": (
                "blake3:c6f236c078234cfd0939564dab46692"
                "ffb0a937c586e5b32b1b544ffc66fec24"
            ),
        }
        for source, expected in vectors.items():
            with self.subTest(source=source):
                self.assertEqual(source_digest(source), expected)

    @settings(max_examples=300, deadline=None)
    @given(st.integers(min_value=1, max_value=(1 << 46)))
    def test_recognition_and_replacement_are_lossless(self, seed: int) -> None:
        entity_id = uuid7(seed)
        constraint_id = uuid7(seed + (1 << 46))
        source = sketch_source(entity_id, constraint_id)
        recognized = recognize(source, "profile")
        self.assertIsNotNone(recognized)
        assert recognized is not None
        self.assertEqual(recognized.source_digest, source_digest(source))
        self.assertEqual(recognized.entities[0].id, entity_id)
        self.assertEqual(recognized.constraints[0].kind, "horizontal")

        replacement = (
            f'line("{entity_id}", (-width / 2, mm(0)), '
            f"(width / 2, mm(0)), construction=True)"
        )
        edited = apply_edit(
            source,
            "profile",
            recognized.source_digest,
            ReplaceCall("entities", entity_id, replacement),
        )
        self.assertIn(
            "untouched = width * 2  # must survive structural edits",
            edited.source,
        )
        self.assertIn("# source identity", edited.source)
        self.assertIn(replacement, edited.source)
        self.assertEqual(edited.recognition.entities[0].id, entity_id)
        self.assertNotEqual(edited.source_digest, recognized.source_digest)

        with self.assertRaisesRegex(SourceError, "source changed") as stale:
            apply_edit(
                edited.source,
                "profile",
                recognized.source_digest,
                ReplaceCall("entities", entity_id, replacement),
            )
        self.assertEqual(stale.exception.code, "source.edit.stale")

    @settings(max_examples=200, deadline=None)
    @given(st.integers(min_value=1, max_value=(1 << 45)))
    def test_append_and_delete_preserve_recognition(self, seed: int) -> None:
        entity_id = uuid7(seed)
        added_id = uuid7(seed + (1 << 45))
        constraint_id = uuid7(seed + (1 << 46))
        source = sketch_source(entity_id, constraint_id)
        added = apply_edit(
            source,
            "profile",
            source_digest(source),
            AppendCall("entities", f'point("{added_id}", (mm(0), mm(0)))'),
        )
        self.assertEqual(
            {entry.id for entry in added.recognition.entities},
            {entity_id, added_id},
        )
        removed = apply_edit(
            added.source,
            "profile",
            added.source_digest,
            DeleteCall("entities", added_id),
        )
        self.assertEqual(
            tuple(entry.id for entry in removed.recognition.entities),
            (entity_id,),
        )

    def test_referenced_entity_cannot_be_deleted(self) -> None:
        entity_id = uuid7(10)
        source = sketch_source(entity_id, uuid7(20))
        with self.assertRaises(SourceError) as missing:
            apply_edit(
                source,
                "profile",
                source_digest(source),
                DeleteCall("entities", entity_id),
            )
        self.assertEqual(missing.exception.code, "source.sketch.missing-entity")

    @settings(max_examples=150, deadline=None)
    @given(
        st.integers(min_value=1, max_value=(1 << 43)),
        st.booleans(),
        st.booleans(),
        st.booleans(),
        st.sampled_from(("\n", "\r\n")),
    )
    def test_session_batches_match_sequential_source_preservation(
        self,
        seed: int,
        use_list: bool,
        multiline: bool,
        trailing_comma: bool,
        newline: str,
    ) -> None:
        first, second, added = (uuid7(seed + offset) for offset in range(3))
        source, function = layout_source(
            (first, second),
            use_list=use_list,
            multiline=multiline,
            trailing_comma=trailing_comma,
            class_scoped=False,
            newline=newline,
        )
        edits = (
            ReplaceCall("entities", first, f'point("{first}", (mm(1), mm(2)))'),
            DeleteCall("entities", second),
            AppendCall("entities", f'point("{added}", (mm(3), mm(4)))'),
            ReplaceCall("entities", added, f'point("{added}", (mm(5), mm(6)))'),
        )
        session = open_edit_session(source, function)
        self.assertIsNotNone(session)
        assert session is not None
        updated = session.apply(session.source_digest, edits)

        sequential = source
        digest = source_digest(sequential)
        for edit in edits:
            result = apply_edit(sequential, function, digest, edit)
            sequential = result.source
            digest = result.source_digest
        self.assertEqual(updated.source, sequential)
        self.assertEqual(updated.source_digest, digest)
        self.assertEqual(
            tuple(entry.id for entry in updated.recognition.entities),
            (first, added),
        )

    def test_session_batch_is_atomic_and_revision_keyed(self) -> None:
        entity_id = uuid7(700)
        constraint_id = uuid7(701)
        source = sketch_source(entity_id, constraint_id)
        session = open_edit_session(source, "profile")
        self.assertIsNotNone(session)
        assert session is not None

        with self.assertRaises(SourceError) as invalid:
            session.apply(session.source_digest, (DeleteCall("entities", entity_id),))
        self.assertEqual(invalid.exception.code, "source.sketch.missing-entity")
        self.assertEqual(session.source, source)
        self.assertEqual(session.source_digest, source_digest(source))

        updated = session.apply(
            session.source_digest,
            (
                DeleteCall("entities", entity_id),
                DeleteCall("constraints", constraint_id),
            ),
        )
        self.assertEqual(updated.recognition.entities, ())
        self.assertEqual(updated.recognition.constraints, ())
        with self.assertRaises(SourceError) as stale:
            updated.apply(session.source_digest, (AppendCall("entities", "bad"),))
        self.assertEqual(stale.exception.code, "source.edit.stale")

        with self.assertRaises(SourceError) as empty:
            session.apply(session.source_digest, ())
        self.assertEqual(empty.exception.code, "source.edit.empty-batch")
        with self.assertRaises(SourceError) as bounded:
            session.apply(
                session.source_digest,
                (DeleteCall("entities", entity_id),) * (MAXIMUM_SOURCE_EDIT_BATCH + 1),
            )
        self.assertEqual(bounded.exception.code, "source.edit.batch-limit")

    @settings(max_examples=100, deadline=None)
    @given(
        st.sampled_from(("m", "mm", "inch", "rad", "deg")),
        st.sampled_from(("absent", "rebound", "aliased")),
        st.sampled_from(("append", "replace")),
    )
    def test_session_edits_cannot_introduce_unowned_units(
        self, unit: str, binding_state: str, operation: str
    ) -> None:
        entity_id = uuid7(710)
        added_id = uuid7(711)
        if binding_state == "rebound":
            binding = f"{unit} = lambda value: value\n"
            coordinate = "zero"
        elif binding_state == "aliased":
            binding = f"from kearne.units import {unit} as owned_unit\n"
            coordinate = "owned_unit(0)"
        else:
            binding = ""
            coordinate = "zero"
        source = f'''from kearne.sketch import SketchDefinition, line, point

{binding}def profile(plane, zero, one):
    return SketchDefinition(
        plane=plane,
        entities=(line("{entity_id}", ({coordinate}, zero), (one, zero)),),
        constraints=(),
    ).build()
'''
        session = open_edit_session(source, "profile")
        self.assertIsNotNone(session)
        assert session is not None
        edit: AppendCall | ReplaceCall
        if operation == "append":
            edit = AppendCall(
                "entities",
                f'point("{added_id}", ({unit}(0), {unit}(0)))',
            )
        else:
            edit = ReplaceCall(
                "entities",
                entity_id,
                f'line("{entity_id}", ({unit}(0), {unit}(0)), ({unit}(1), {unit}(0)))',
            )
        with self.assertRaises(SourceError) as refused:
            session.apply(session.source_digest, (edit,))
        self.assertEqual(refused.exception.code, "source.edit.invalid-call")
        self.assertEqual(session.source, source)
        self.assertEqual(session.source_digest, source_digest(source))

    @settings(max_examples=200, deadline=None)
    @given(
        st.integers(min_value=1, max_value=(1 << 43)),
        st.booleans(),
        st.booleans(),
        st.booleans(),
        st.booleans(),
        st.booleans(),
        st.sampled_from(("\n", "\r\n")),
    )
    def test_layouts_keep_structural_edits_lossless(
        self,
        seed: int,
        use_list: bool,
        multiline: bool,
        trailing_comma: bool,
        class_scoped: bool,
        delete_first: bool,
        newline: str,
    ) -> None:
        first, second, added = (uuid7(seed + offset) for offset in range(3))
        source, function = layout_source(
            (first, second),
            use_list=use_list,
            multiline=multiline,
            trailing_comma=trailing_comma,
            class_scoped=class_scoped,
            newline=newline,
        )
        recognized = recognize(source, function)
        self.assertIsNotNone(recognized)
        assert recognized is not None
        for entry in recognized.entities:
            line_text = source.splitlines()[entry.span.start_line - 1]
            self.assertEqual(
                line_text[entry.span.start_column : entry.span.end_column],
                entry.code,
            )

        removed = apply_edit(
            source,
            function,
            recognized.source_digest,
            DeleteCall("entities", first if delete_first else second),
        )
        remaining = second if delete_first else first
        self.assertEqual(
            tuple(entry.id for entry in removed.recognition.entities), (remaining,)
        )
        if multiline:
            self.assertIn("# standalone retained", removed.source)
            if delete_first:
                self.assertIn("# retained", removed.source)
        appended = apply_edit(
            removed.source,
            function,
            removed.source_digest,
            AppendCall("entities", f'point("{added}", (mm(0), mm(0)))'),
        )
        self.assertEqual(
            tuple(entry.id for entry in appended.recognition.entities),
            (remaining, added),
        )

    @settings(max_examples=100, deadline=None)
    @given(
        st.integers(min_value=1, max_value=(1 << 43)),
        st.booleans(),
        st.booleans(),
        st.sampled_from(("\n", "\r\n")),
    )
    def test_empty_sections_accept_and_remove_their_first_entry(
        self,
        seed: int,
        use_list: bool,
        multiline: bool,
        newline: str,
    ) -> None:
        added = uuid7(seed)
        source, function = layout_source(
            (),
            use_list=use_list,
            multiline=multiline,
            trailing_comma=False,
            class_scoped=False,
            newline=newline,
        )
        inserted = apply_edit(
            source,
            function,
            source_digest(source),
            AppendCall("entities", f'point("{added}", (mm(0), mm(0)))'),
        )
        removed = apply_edit(
            inserted.source,
            function,
            inserted.source_digest,
            DeleteCall("entities", added),
        )
        self.assertEqual(removed.recognition.entities, ())

    def test_arbitrary_native_source_degrades_without_execution(self) -> None:
        source = """raise RuntimeError("recognizer executed the module")
from build123d import Circle, Sketch

def profile(radius: float) -> Sketch:
    return Circle(radius)
"""
        self.assertIsNone(recognize(source, "profile"))

    @settings(max_examples=100, deadline=None)
    @given(st.sampled_from(("line", "SketchDefinition")))
    def test_locally_shadowed_owned_names_are_not_recognized(
        self, shadowed: str
    ) -> None:
        entity_id = uuid7(500)
        source = f'''from build123d import Sketch
from kearne.sketch import SketchDefinition, SketchPlane, line
from kearne.units import mm

def profile(plane: SketchPlane) -> Sketch:
    {shadowed} = lambda *args: object()
    return SketchDefinition(
        plane=plane,
        entities=(line("{entity_id}", (mm(0), mm(0)), (mm(1), mm(0))),),
        constraints=(),
    ).build()
'''
        self.assertIsNone(recognize(source, "profile"))

    def test_import_aliases_are_owned_but_module_rebinding_is_not(self) -> None:
        entity_id = uuid7(510)
        aliased = f'''from kearne.sketch import (
    SketchDefinition as Definition,
    line as segment,
)
from kearne.units import mm

def profile(plane):
    return Definition(
        plane=plane,
        entities=(segment("{entity_id}", (mm(0), mm(0)), (mm(1), mm(0))),),
        constraints=(),
    ).build()
'''
        recognized = recognize(aliased, "profile")
        self.assertIsNotNone(recognized)
        assert recognized is not None
        with self.assertRaisesRegex(SourceError, "unrecognizable"):
            apply_edit(
                aliased,
                "profile",
                recognized.source_digest,
                ReplaceCall(
                    "entities",
                    entity_id,
                    f'line("{entity_id}", (mm(0), mm(0)), (mm(2), mm(0)))',
                ),
            )
        rebound = aliased.replace(
            "from kearne.units import mm",
            "from kearne.units import mm\nsegment = lambda *args: object()",
        )
        self.assertIsNone(recognize(rebound, "profile"))

    @settings(max_examples=100, deadline=None)
    @given(st.sampled_from(("m", "mm", "inch", "rad", "deg")))
    def test_rebound_unit_constructors_are_not_recognized(self, unit: str) -> None:
        entity_id = uuid7(515)
        source = f'''from kearne.sketch import SketchDefinition, line
from kearne.units import deg, inch, m, mm, rad

{unit} = lambda value: value

def profile(plane):
    return SketchDefinition(
        plane=plane,
        entities=(line("{entity_id}", ({unit}(0), mm(0)), (mm(1), mm(0))),),
        constraints=(),
    ).build()
'''
        self.assertIsNone(recognize(source, "profile"))

    def test_valid_but_dynamic_helper_shape_degrades_to_source_editing(self) -> None:
        source = """from kearne.sketch import SketchDefinition

def profile(plane, entities):
    return SketchDefinition(
        plane=plane,
        entities=tuple(entities),
        constraints=(),
    ).build()
"""
        self.assertIsNone(recognize(source, "profile"))

    def test_decorated_helper_shape_degrades_to_source_editing(self) -> None:
        entity_id = uuid7(520)
        source = f'''from kearne.sketch import SketchDefinition, line
from kearne.units import mm

def identity(function):
    return function

@identity
def profile(plane):
    return SketchDefinition(
        plane=plane,
        entities=(line("{entity_id}", (mm(0), mm(0)), (mm(1), mm(0))),),
        constraints=(),
    ).build()
'''
        self.assertIsNone(recognize(source, "profile"))

    def test_schema_rejects_every_helper_arity_and_keyword_drift(self) -> None:
        imports = ", ".join(("SketchDefinition", *(spec.name for spec in HELPER_SPECS)))
        for index, spec in enumerate(HELPER_SPECS):
            if spec.section == "references":
                continue
            arguments: list[str] = []
            for offset, argument in enumerate(spec.positional):
                stable = f'"{uuid7(1_000 + index * 10 + offset)}"'
                value = {
                    "stable_id": stable,
                    "label": '"Rectangle 1"',
                    "point": "(mm(0), mm(0))",
                    "points": "((mm(0), mm(0)), (mm(1), mm(1)))",
                    "length": "mm(1)",
                    "angle": "deg(1)",
                    "scalar": "0.5",
                    "scalars": "(0.0, 0.0, 1.0, 1.0)",
                    "integer": "1",
                    "entity_ref": stable,
                    "entity_refs": f'({stable}, "{uuid7(99_000 + index)}")',
                    "object_members": f'(("curve", {stable}),)',
                    "point_ref": f"at({stable})",
                }[argument.kind]
                arguments.append(value)
            section = spec.section

            def source_with(call: str, selected_section: str = section) -> str:
                entries = call + ","
                objects = entries if selected_section == "objects" else ""
                entities = entries if selected_section == "entities" else ""
                constraints = entries if selected_section == "constraints" else ""
                return f"""from kearne.sketch import {imports}
from kearne.units import deg, mm

def profile(plane):
    return SketchDefinition(
        plane=plane,
        objects=({objects}),
        entities=({entities}),
        constraints=({constraints}),
    ).build()
"""

            with self.subTest(helper=spec.name, defect="arity"):
                invalid = f"{spec.name}({', '.join(arguments[:-1])})"
                with self.assertRaises(SourceError):
                    recognize(source_with(invalid), "profile")
            with self.subTest(helper=spec.name, defect="keyword"):
                invalid = f"{spec.name}({', '.join(arguments)}, bogus=True)"
                with self.assertRaises(SourceError):
                    recognize(source_with(invalid), "profile")
            for keyword in spec.keywords:
                with self.subTest(helper=spec.name, defect=keyword.name):
                    invalid_value = '"invalid"' if keyword.values else "1"
                    invalid = (
                        f"{spec.name}({', '.join(arguments)}, "
                        f"{keyword.name}={invalid_value})"
                    )
                    with self.assertRaises(SourceError):
                        recognize(source_with(invalid), "profile")

    @settings(max_examples=100, deadline=None)
    @given(st.sampled_from(("horizontal", "tangent", "point-reference")))
    def test_known_constraint_reference_kinds_are_exact(self, defect: str) -> None:
        line_id, circle_id, constraint_id = (uuid7(1_900 + index) for index in range(3))
        constraint = {
            "horizontal": f'horizontal("{constraint_id}", "{circle_id}")',
            "tangent": (f'tangent("{constraint_id}", "{line_id}", "{line_id}")'),
            "point-reference": (
                f'coincident("{constraint_id}", start("{circle_id}"), '
                f'start("{line_id}"))'
            ),
        }[defect]
        source = f'''from kearne.sketch import (
    SketchDefinition, circle, coincident, horizontal, line, start, tangent,
)
from kearne.units import mm

def profile(plane):
    return SketchDefinition(
        plane=plane,
        entities=(
            line("{line_id}", (mm(0), mm(0)), (mm(1), mm(0))),
            circle("{circle_id}", (mm(0), mm(0)), mm(1)),
        ),
        constraints=({constraint},),
    ).build()
'''
        with self.assertRaises(SourceError):
            recognize(source, "profile")


class SourceEditMachine(RuleBasedStateMachine):
    def __init__(self) -> None:
        super().__init__()
        source, self.function = layout_source(
            (),
            use_list=False,
            multiline=True,
            trailing_comma=True,
            class_scoped=False,
            newline="\n",
        )
        self.source = "# state-machine sentinel\n" + source
        self.ids: list[str] = []
        self.next_id = 2_000

    @rule()
    def append(self) -> None:
        stable = uuid7(self.next_id)
        self.next_id += 1
        result = apply_edit(
            self.source,
            self.function,
            source_digest(self.source),
            AppendCall("entities", f'point("{stable}", (mm(0), mm(0)))'),
        )
        self.source = result.source
        self.ids.append(stable)

    @precondition(lambda self: bool(self.ids))
    @rule()
    def replace_latest(self) -> None:
        stable = self.ids[-1]
        result = apply_edit(
            self.source,
            self.function,
            source_digest(self.source),
            ReplaceCall("entities", stable, f'point("{stable}", (mm(1), mm(2)))'),
        )
        self.source = result.source

    @precondition(lambda self: bool(self.ids))
    @rule()
    def delete_oldest(self) -> None:
        stable = self.ids.pop(0)
        result = apply_edit(
            self.source,
            self.function,
            source_digest(self.source),
            DeleteCall("entities", stable),
        )
        self.source = result.source

    @invariant()
    def source_and_model_agree(self) -> None:
        recognized = recognize(self.source, self.function)
        assert recognized is not None
        assert tuple(entry.id for entry in recognized.entities) == tuple(self.ids)
        assert self.source.startswith("# state-machine sentinel\n")


SourceEditStatefulTest = SourceEditMachine.TestCase
SourceEditStatefulTest.settings = settings(
    max_examples=25, stateful_step_count=40, deadline=None
)


class SourceSessionMachine(RuleBasedStateMachine):
    def __init__(self) -> None:
        super().__init__()
        source, function = layout_source(
            (),
            use_list=False,
            multiline=True,
            trailing_comma=True,
            class_scoped=False,
            newline="\n",
        )
        self.session = open_edit_session(
            "# session-state sentinel\n" + source, function
        )
        assert self.session is not None
        self.ids: list[str] = []
        self.next_id = 3_000

    @rule()
    def append_batch(self) -> None:
        added = [uuid7(self.next_id + offset) for offset in range(2)]
        self.next_id += len(added)
        self.session = self.session.apply(
            self.session.source_digest,
            tuple(
                AppendCall("entities", f'point("{stable}", (mm(0), mm(0)))')
                for stable in added
            ),
        )
        self.ids.extend(added)

    @precondition(lambda self: bool(self.ids))
    @rule()
    def replace_batch_edges(self) -> None:
        selected = (self.ids[0], self.ids[-1])
        self.session = self.session.apply(
            self.session.source_digest,
            tuple(
                ReplaceCall("entities", stable, f'point("{stable}", (mm(1), mm(2)))')
                for stable in selected
            ),
        )

    @precondition(lambda self: len(self.ids) >= 2)
    @rule()
    def delete_batch_edges(self) -> None:
        selected = (self.ids[0], self.ids[-1])
        self.session = self.session.apply(
            self.session.source_digest,
            tuple(DeleteCall("entities", stable) for stable in selected),
        )
        del self.ids[-1]
        del self.ids[0]

    @precondition(lambda self: bool(self.ids))
    @rule()
    def rejected_batch_keeps_revision(self) -> None:
        before = self.session
        try:
            self.session.apply(
                self.session.source_digest,
                (
                    AppendCall(
                        "entities",
                        f'point("{self.ids[0]}", (mm(0), mm(0)))',
                    ),
                ),
            )
        except SourceError as error:
            assert error.code == "source.edit.duplicate-stable-id"
        else:
            raise AssertionError("duplicate session edit was accepted")
        assert self.session is before

    @invariant()
    def session_and_model_agree(self) -> None:
        assert self.session.source_digest == source_digest(self.session.source)
        assert tuple(entry.id for entry in self.session.recognition.entities) == tuple(
            self.ids
        )
        assert self.session.source.startswith("# session-state sentinel\n")


SourceSessionStatefulTest = SourceSessionMachine.TestCase
SourceSessionStatefulTest.settings = settings(
    max_examples=25, stateful_step_count=30, deadline=None
)


class HelperProperties(unittest.TestCase):
    def test_runtime_helper_signatures_match_the_shared_schema(self) -> None:
        for spec in HELPER_SPECS:
            parameters = tuple(
                signature(getattr(sketch_api, spec.name)).parameters.values()
            )
            positional = tuple(
                parameter
                for parameter in parameters
                if parameter.kind is Parameter.POSITIONAL_ONLY
            )
            keywords = tuple(
                parameter
                for parameter in parameters
                if parameter.kind is Parameter.KEYWORD_ONLY
            )
            with self.subTest(helper=spec.name):
                self.assertEqual(
                    tuple(parameter.name for parameter in positional),
                    tuple(argument.name for argument in spec.positional),
                )
                self.assertEqual(
                    tuple(parameter.name for parameter in keywords),
                    tuple(keyword.name for keyword in spec.keywords),
                )
                self.assertEqual(
                    tuple(parameter.default for parameter in keywords),
                    tuple(keyword.default for keyword in spec.keywords),
                )

    @settings(max_examples=100, deadline=None)
    @given(
        st.floats(min_value=1.0, max_value=1_000.0, allow_nan=False),
        st.floats(min_value=1.0, max_value=1_000.0, allow_nan=False),
    )
    def test_closed_profile_builds_a_real_build123d_sketch(
        self, width: float, height: float
    ) -> None:
        ids = [uuid7(index + 100) for index in range(4)]
        definition = SketchDefinition(
            plane=SketchPlane(uuid7(90), Plane.XY),
            entities=(
                line(ids[0], (mm(0), mm(0)), (mm(width), mm(0))),
                line(ids[1], (mm(width), mm(0)), (mm(width), mm(height))),
                line(ids[2], (mm(width), mm(height)), (mm(0), mm(height))),
                line(ids[3], (mm(0), mm(height)), (mm(0), mm(0))),
            ),
        )
        result = definition.build()
        self.assertEqual(len(result.faces()), 1)
        self.assertAlmostEqual(result.area, width * height, places=6)

    @settings(max_examples=100, deadline=None)
    @given(
        st.floats(min_value=2.0, max_value=1_000.0, allow_nan=False),
        st.floats(min_value=0.1, max_value=0.9, allow_nan=False),
    )
    def test_nested_profiles_create_holes(self, outer: float, ratio: float) -> None:
        inner = outer * ratio
        definition = SketchDefinition(
            plane=SketchPlane(uuid7(290), Plane.XY),
            entities=(
                circle(uuid7(300), (mm(0), mm(0)), mm(outer)),
                circle(uuid7(301), (mm(0), mm(0)), mm(inner)),
            ),
        )
        result = definition.build()
        self.assertEqual(len(result.faces()), 1)
        self.assertAlmostEqual(result.area, pi * (outer**2 - inner**2), places=5)

    @settings(max_examples=100, deadline=None)
    @given(st.floats(min_value=1.0, max_value=1_000.0, allow_nan=False))
    def test_arc_angles_cross_the_build123d_boundary_in_degrees(
        self, radius: float
    ) -> None:
        definition = SketchDefinition(
            plane=SketchPlane(uuid7(340), Plane.XY),
            entities=(
                arc(
                    uuid7(341),
                    (mm(0), mm(0)),
                    mm(radius),
                    deg(0),
                    deg(180),
                ),
                arc(
                    uuid7(342),
                    (mm(0), mm(0)),
                    mm(radius),
                    deg(180),
                    deg(360),
                ),
            ),
        )
        result = definition.build()
        self.assertEqual(len(result.faces()), 1)
        self.assertAlmostEqual(result.area, pi * radius**2, places=5)

    @settings(max_examples=100, deadline=None)
    @given(
        st.floats(min_value=1.0, max_value=1_000.0, allow_nan=False),
        st.floats(min_value=0.1, max_value=1.0, allow_nan=False),
        st.floats(min_value=-720.0, max_value=720.0, allow_nan=False),
    )
    def test_ellipse_crosses_build123d_as_exact_rotated_geometry(
        self, major: float, ratio: float, rotation: float
    ) -> None:
        minor = major * ratio
        definition = SketchDefinition(
            plane=SketchPlane(uuid7(350), Plane.XY),
            entities=(
                ellipse(
                    uuid7(351),
                    (mm(0), mm(0)),
                    mm(major),
                    mm(minor),
                    deg(rotation),
                ),
            ),
        )
        result = definition.build()
        self.assertEqual(len(result.faces()), 1)
        self.assertAlmostEqual(result.area, pi * major * minor, places=5)

    def test_elliptical_arcs_join_without_polyline_approximation(self) -> None:
        definition = SketchDefinition(
            plane=SketchPlane(uuid7(360), Plane.XY),
            entities=(
                elliptical_arc(
                    uuid7(361),
                    (mm(0), mm(0)),
                    mm(8),
                    mm(3),
                    deg(25),
                    deg(0),
                    deg(180),
                ),
                elliptical_arc(
                    uuid7(362),
                    (mm(0), mm(0)),
                    mm(8),
                    mm(3),
                    deg(25),
                    deg(180),
                    deg(360),
                ),
            ),
        )
        result = definition.build()
        self.assertEqual(len(result.faces()), 1)
        self.assertAlmostEqual(result.area, pi * 8 * 3, places=5)

    @settings(max_examples=100, deadline=None)
    @given(
        st.floats(min_value=1.0, max_value=100.0, allow_nan=False),
        st.floats(min_value=1.1, max_value=3.0, allow_nan=False),
        st.floats(min_value=0.1, max_value=1.5, allow_nan=False),
    )
    def test_hyperbolic_arc_crosses_as_exact_native_geometry(
        self, major: float, ratio: float, parameter: float
    ) -> None:
        minor = major * ratio
        endpoint_x = major * cosh(parameter)
        endpoint_y = minor * sinh(parameter)
        definition = SketchDefinition(
            plane=SketchPlane(uuid7(370), Plane.XY),
            entities=(
                hyperbolic_arc(
                    uuid7(371),
                    (mm(0), mm(0)),
                    mm(major),
                    mm(minor),
                    deg(0),
                    -parameter,
                    parameter,
                ),
                line(
                    uuid7(372),
                    (mm(endpoint_x), mm(endpoint_y)),
                    (mm(endpoint_x), mm(-endpoint_y)),
                ),
            ),
        )
        result = definition.build()
        self.assertEqual(len(result.faces()), 1)
        expected = major * minor * (cosh(parameter) * sinh(parameter) - parameter)
        self.assertAlmostEqual(result.area, expected, places=5)

    @settings(max_examples=100, deadline=None)
    @given(
        st.floats(min_value=0.5, max_value=100.0, allow_nan=False),
        st.floats(min_value=0.5, max_value=100.0, allow_nan=False),
    )
    def test_parabolic_arc_crosses_as_exact_native_geometry(
        self, focal: float, parameter: float
    ) -> None:
        endpoint_x = parameter * parameter / (4.0 * focal)
        definition = SketchDefinition(
            plane=SketchPlane(uuid7(380), Plane.XY),
            entities=(
                parabolic_arc(
                    uuid7(381),
                    (mm(0), mm(0)),
                    mm(focal),
                    deg(0),
                    mm(-parameter),
                    mm(parameter),
                ),
                line(
                    uuid7(382),
                    (mm(endpoint_x), mm(parameter)),
                    (mm(endpoint_x), mm(-parameter)),
                ),
            ),
        )
        result = definition.build()
        self.assertEqual(len(result.faces()), 1)
        self.assertAlmostEqual(result.area, parameter**3 / (3.0 * focal), places=5)

    @settings(max_examples=100, deadline=None)
    @given(
        st.floats(min_value=0.5, max_value=100.0, allow_nan=False),
        st.floats(min_value=0.5, max_value=100.0, allow_nan=False),
    )
    def test_bspline_crosses_as_exact_native_geometry(
        self, length_value: float, height: float
    ) -> None:
        definition = SketchDefinition(
            plane=SketchPlane(uuid7(385), Plane.XY),
            entities=(
                bspline(
                    uuid7(386),
                    (
                        (mm(0), mm(0)),
                        (mm(length_value), mm(height)),
                        (mm(2.0 * length_value), mm(0)),
                    ),
                    (0.0, 0.0, 0.0, 1.0, 1.0, 1.0),
                    (1.0, 1.0, 1.0),
                    2,
                ),
                line(
                    uuid7(387),
                    (mm(2.0 * length_value), mm(0)),
                    (mm(0), mm(0)),
                ),
            ),
        )
        result = definition.build()
        self.assertEqual(len(result.faces()), 1)
        self.assertAlmostEqual(
            result.area, 2.0 * length_value * height / 3.0, places=5
        )

    def test_runtime_definition_rejects_missing_references(self) -> None:
        entity_id = uuid7(400)
        with self.assertRaises(SketchDefinitionError):
            SketchDefinition(
                plane=SketchPlane(uuid7(390), Plane.XY),
                entities=(line(entity_id, (mm(0), mm(0)), (mm(1), mm(0))),),
                constraints=(horizontal(uuid7(401), uuid7(402)),),
            )

    def test_runtime_entity_compatibility_is_generated_from_schema(self) -> None:
        constraint_specs = [
            spec for spec in HELPER_SPECS if spec.section == "constraints"
        ]
        for spec_index, spec in enumerate(constraint_specs):
            base = 5_000 + spec_index * 100
            points: list[PointRef] = []
            entities: list[sketch_api.Entity] = []
            entity_ids: list[str] = []
            entity_arguments = [
                argument
                for argument in spec.positional
                if argument.kind == "entity_ref"
            ]
            has_entity_set = any(
                argument.kind == "entity_refs" for argument in spec.positional
            )
            if spec.entity_combinations:
                selected_kinds = tuple(
                    sorted(allowed)[0] for allowed in spec.entity_combinations[0]
                )
            else:
                selected_kinds = tuple(
                    sorted(argument.entity_kinds)[0] for argument in entity_arguments
                )
            for point_index, argument in enumerate(spec.positional):
                if argument.kind != "point_ref":
                    continue
                stable = uuid7(base + point_index + 1)
                entities.append(entity_of_kind("point", stable))
                points.append(PointRef(stable, "point"))
            for entity_index, kind in enumerate(selected_kinds):
                stable = uuid7(base + 20 + entity_index)
                entities.append(entity_of_kind(kind, stable))
                entity_ids.append(stable)
            if has_entity_set:
                for entity_index in range(2):
                    stable = uuid7(base + 40 + entity_index)
                    entities.append(entity_of_kind("line", stable))
                    entity_ids.append(stable)
            value_argument = next(
                (
                    argument
                    for argument in spec.positional
                    if argument.kind in {"length", "angle"}
                ),
                None,
            )
            value: Length | Angle | None = None
            if value_argument is not None:
                value = mm(1) if value_argument.kind == "length" else deg(1)
            position = (
                (mm(1), mm(2))
                if any(argument.kind == "point" for argument in spec.positional)
                else None
            )
            constraint = Constraint(
                uuid7(base + 90),
                spec.name,
                tuple(points),
                tuple(entity_ids),
                value,
                "external" if spec.name == "tangent" else None,
                position,
            )
            plane = SketchPlane(uuid7(base + 91), Plane.XY)
            SketchDefinition(plane, tuple(entities), (constraint,))

            invalid_kinds = tuple(
                sorted(ALL_ENTITIES - argument.entity_kinds)
                for argument in entity_arguments
            )
            invalid_index = next(
                (index for index, kinds in enumerate(invalid_kinds) if kinds), None
            )
            if invalid_index is not None:
                invalid = list(entities)
                offset = len(points) + invalid_index
                invalid[offset] = entity_of_kind(
                    invalid_kinds[invalid_index][0], entity_ids[invalid_index]
                )
                with (
                    self.subTest(helper=spec.name, defect="entity-kind"),
                    self.assertRaises(SketchDefinitionError),
                ):
                    SketchDefinition(plane, tuple(invalid), (constraint,))

            if spec.entity_combinations:
                possible = product(
                    *(sorted(argument.entity_kinds) for argument in entity_arguments)
                )
                invalid_combination = next(
                    (
                        kinds
                        for kinds in possible
                        if not any(
                            all(
                                kind in allowed
                                for kind, allowed in zip(
                                    kinds, combination, strict=True
                                )
                            )
                            for combination in spec.entity_combinations
                        )
                    ),
                    None,
                )
                if invalid_combination is not None:
                    invalid = [*entities[: len(points)]]
                    invalid.extend(
                        entity_of_kind(kind, stable)
                        for kind, stable in zip(
                            invalid_combination, entity_ids, strict=True
                        )
                    )
                    with (
                        self.subTest(helper=spec.name, defect="combination"),
                        self.assertRaises(SketchDefinitionError),
                    ):
                        SketchDefinition(plane, tuple(invalid), (constraint,))

    def test_open_or_unsolved_geometry_is_not_published(self) -> None:
        open_definition = SketchDefinition(
            plane=SketchPlane(uuid7(405), Plane.XY),
            entities=(
                line(uuid7(406), (mm(0), mm(0)), (mm(1), mm(0))),
                line(
                    uuid7(407),
                    (mm(0), mm(1)),
                    (mm(1), mm(1)),
                    construction=True,
                ),
            ),
        )
        with self.assertRaisesRegex(SketchDefinitionError, "open profile"):
            open_definition.build()
        entity_id = uuid7(408)
        constrained = SketchDefinition(
            plane=SketchPlane(uuid7(409), Plane.XY),
            entities=(line(entity_id, (mm(0), mm(0)), (mm(1), mm(0))),),
            constraints=(horizontal(uuid7(410), entity_id),),
        )
        with self.assertRaisesRegex(SketchDefinitionError, "solved"):
            constrained.build()

    @settings(max_examples=50, deadline=None)
    @given(st.floats(min_value=1.0, max_value=100.0, allow_nan=False))
    def test_profile_join_uses_the_shared_metre_tolerance(self, side: float) -> None:
        gap = WIRE_JOIN_TOLERANCE_MILLIMETRES * 0.5
        ids = [uuid7(6_000 + index) for index in range(4)]
        result = SketchDefinition(
            SketchPlane(uuid7(6_010), Plane.XY),
            (
                line(ids[0], (mm(0), mm(0)), (mm(side), mm(0))),
                line(ids[1], (mm(side), mm(0)), (mm(side), mm(side))),
                line(ids[2], (mm(side), mm(side)), (mm(0), mm(side))),
                line(ids[3], (mm(0), mm(side)), (mm(gap), mm(0))),
            ),
        ).build()
        self.assertEqual(len(result.faces()), 1)

    @settings(max_examples=50, deadline=None)
    @given(
        st.floats(min_value=1.0, max_value=100.0, allow_nan=False),
        st.floats(min_value=0.25, max_value=0.75, allow_nan=False),
    )
    def test_duplicate_and_intersecting_profiles_fail_closed(
        self, radius: float, offset_ratio: float
    ) -> None:
        plane = SketchPlane(uuid7(6_100), Plane.XY)
        duplicate = SketchDefinition(
            plane,
            (
                circle(uuid7(6_101), (mm(0), mm(0)), mm(radius)),
                circle(uuid7(6_102), (mm(0), mm(0)), mm(radius)),
            ),
        )
        with self.assertRaisesRegex(SketchDefinitionError, "touch or intersect"):
            duplicate.build()
        intersecting = SketchDefinition(
            plane,
            (
                circle(uuid7(6_103), (mm(0), mm(0)), mm(radius)),
                circle(
                    uuid7(6_104),
                    (mm(radius * offset_ratio), mm(0)),
                    mm(radius),
                ),
            ),
        )
        with self.assertRaisesRegex(SketchDefinitionError, "touch or intersect"):
            intersecting.build()

    def test_build_publishes_no_unverified_topology_identity(self) -> None:
        ids = [uuid7(6_200 + index) for index in range(4)]
        result = SketchDefinition(
            SketchPlane(uuid7(6_210), Plane.XY),
            (
                line(ids[0], (mm(0), mm(0)), (mm(1), mm(0))),
                line(ids[1], (mm(1), mm(0)), (mm(1), mm(1))),
                line(ids[2], (mm(1), mm(1)), (mm(0), mm(1))),
                line(ids[3], (mm(0), mm(1)), (mm(0), mm(0))),
            ),
        ).build()
        labels = {shape.label for shape in (*result.edges(), *result.faces())}
        self.assertTrue(labels.isdisjoint(ids))

    def test_runtime_definition_rejects_unattached_planes_and_raw_lengths(self) -> None:
        with self.assertRaisesRegex(SketchDefinitionError, "stable attachment"):
            SketchDefinition(plane=Plane.XY, entities=())  # type: ignore[arg-type]
        with self.assertRaisesRegex(SketchDefinitionError, "Length"):
            line(uuid7(6_300), (0.0, 0.0), (1.0, 0.0))  # type: ignore[arg-type]

    def test_geometry_uses_the_shared_numerical_envelope(self) -> None:
        with self.assertRaisesRegex(SketchDefinitionError, "degenerate"):
            line(
                uuid7(6_310),
                (mm(0), mm(0)),
                (Length(MINIMUM_LENGTH_METRES * 0.5), mm(0)),
            )
        with self.assertRaisesRegex(SketchDefinitionError, "angular span"):
            arc(
                uuid7(6_311),
                (mm(0), mm(0)),
                mm(1),
                deg(0),
                Angle(ANGLE_TOLERANCE_RADIANS * 0.5),
            )
        with self.assertRaisesRegex(SketchDefinitionError, "supported range"):
            point(
                uuid7(6_312),
                (Length(MAXIMUM_COORDINATE_METRES * 2.0), mm(0)),
            )

    @settings(max_examples=100, deadline=None)
    @given(
        st.sampled_from((-1.0, 1.0)),
        st.floats(
            min_value=ANGLE_TOLERANCE_RADIANS,
            max_value=MAXIMUM_ARC_SPAN_RADIANS,
            allow_nan=False,
            allow_infinity=False,
        ),
    )
    def test_arc_sweep_within_one_revolution_is_accepted(
        self, direction: float, span: float
    ) -> None:
        result = arc(
            uuid7(6_320),
            (mm(0), mm(0)),
            mm(1),
            Angle(0.0),
            Angle(direction * span),
        )
        self.assertEqual(result.end_angle.radians, direction * span)

    @settings(max_examples=100, deadline=None)
    @given(
        st.sampled_from((-1.0, 1.0)),
        st.floats(
            min_value=nextafter(MAXIMUM_ARC_SPAN_RADIANS, inf),
            max_value=MAXIMUM_ARC_SPAN_RADIANS + ANGLE_TOLERANCE_RADIANS,
            allow_nan=False,
            allow_infinity=False,
        ),
    )
    def test_arc_sweep_beyond_one_revolution_is_rejected(
        self, direction: float, span: float
    ) -> None:
        with self.assertRaisesRegex(SketchDefinitionError, "one revolution"):
            arc(
                uuid7(6_321),
                (mm(0), mm(0)),
                mm(1),
                Angle(0.0),
                Angle(direction * span),
            )


if __name__ == "__main__":
    unittest.main()
