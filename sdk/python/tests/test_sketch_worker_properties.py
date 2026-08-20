from __future__ import annotations

import io
import subprocess
import sys
import unittest
from dataclasses import replace
from uuid import UUID

from build123d import Plane
from hypothesis import given, settings
from hypothesis import strategies as st
from kearne._worker.protocol import (
    MAXIMUM_FRAME_BYTES,
    ProtocolError,
    process_envelope,
    read_frame,
)
from kearne._worker.sketch_source import create_sketch_source, process_transform
from kearne.api.v1 import worker_pb2  # type: ignore[import-not-found]
from kearne.sketch import (
    Constraint,
    Entity,
    LineEntity,
    SketchDefinition,
    SketchPlane,
    coincident,
    end,
    horizontal,
    line,
    start,
    vertical,
)
from kearne.sketch_source import emit_call
from kearne.sketch_wire import definition_to_wire, definition_values_from_wire
from kearne.source import recognize, source_digest
from kearne.units import Length, m


def uuid7(index: int) -> str:
    timestamp = (1_700_000_000_000 + index % 1_000_000_000).to_bytes(6, "big")
    tail = bytearray((index >> ((offset % 8) * 8)) & 0xFF for offset in range(10))
    tail[0] = 0x70 | (tail[0] & 0x0F)
    tail[2] = 0x80 | (tail[2] & 0x3F)
    return str(UUID(bytes=timestamp + bytes(tail)))


def write_digest(message: object, value: str) -> None:
    algorithm, hexadecimal = value.split(":", 1)
    message.algorithm = algorithm  # type: ignore[attr-defined]
    message.value = bytes.fromhex(hexadecimal)  # type: ignore[attr-defined]


def rectangle(
    seed: int, width_millimetres: int, height_millimetres: int
) -> tuple[SketchDefinition, tuple[Entity | Constraint, ...]]:
    ids = tuple(uuid7(seed * 100 + index) for index in range(1, 5))
    width = m(width_millimetres / 1000)
    height = m(height_millimetres / 1000)
    points = ((m(0), m(0)), (width, m(0)), (width, height), (m(0), height))
    entities: tuple[Entity, ...] = tuple(
        line(ids[index], points[index], points[(index + 1) % 4]) for index in range(4)
    )
    constraints: tuple[Constraint, ...] = (
        *(
            coincident(
                uuid7(seed * 100 + 20 + index),
                end(ids[index]),
                start(ids[(index + 1) % 4]),
            )
            for index in range(4)
        ),
        horizontal(uuid7(seed * 100 + 30), ids[0]),
        vertical(uuid7(seed * 100 + 31), ids[1]),
        horizontal(uuid7(seed * 100 + 32), ids[2]),
        vertical(uuid7(seed * 100 + 33), ids[3]),
    )
    definition = SketchDefinition(
        SketchPlane(uuid7(seed * 100 + 99), Plane.XY), entities, constraints
    )
    return definition, (*entities, *constraints)


def edit_job(
    source: str,
    target: SketchDefinition,
    values: tuple[Entity | Constraint, ...],
    action: int,
    order: tuple[int, ...] | None = None,
) -> worker_pb2.SketchSourceTransformJob:
    prior = source_digest(source)
    job = worker_pb2.SketchSourceTransformJob()
    job.edit.source_utf8 = source.encode()
    job.edit.function_name = "profile"
    write_digest(job.edit.expected_prior, prior)
    job.edit.target.CopyFrom(definition_to_wire(target, prior))
    selected = order if order is not None else tuple(range(len(values)))
    for index in selected:
        value = values[index]
        operation = job.edit.edits.add()
        operation.action = action
        operation.section = (
            worker_pb2.SKETCH_SOURCE_SECTION_ENTITIES
            if isinstance(value, LineEntity)
            else worker_pb2.SKETCH_SOURCE_SECTION_CONSTRAINTS
        )
        operation.target_id.value = UUID(value.id).bytes
    return job


def transformed(job: worker_pb2.SketchSourceTransformJob) -> tuple[str, str]:
    result = process_transform(job)
    if result.WhichOneof("outcome") != "success":
        raise AssertionError([value.code for value in result.failure.diagnostics])
    source = result.success.source_utf8.decode()
    digest = f"{result.success.source_digest.algorithm}:" + (
        result.success.source_digest.value.hex()
    )
    return source, digest


class FragmentedInput(io.BytesIO):
    def __init__(self, data: bytes, fragment: int) -> None:
        super().__init__(data)
        self.fragment = fragment

    def read(self, size: int = -1) -> bytes:
        bounded = self.fragment if size < 0 else min(size, self.fragment)
        return super().read(bounded)


class SketchWorkerProperties(unittest.TestCase):
    def test_source_worker_does_not_load_the_cad_runtime(self) -> None:
        completed = subprocess.run(
            [
                sys.executable,
                "-c",
                "import sys; import kearne._worker.protocol; "
                "assert 'build123d' not in sys.modules",
            ],
            capture_output=True,
            check=False,
            timeout=5,
        )
        self.assertEqual(completed.returncode, 0, completed.stderr.decode())

    def test_source_replacement_recognizes_geometry_without_execution(self) -> None:
        initial = create_sketch_source("profile")
        target, values = rectangle(700, 80, 50)
        source, prior = transformed(
            edit_job(
                initial,
                target,
                values,
                worker_pb2.SKETCH_SOURCE_EDIT_ACTION_APPEND,
            )
        )
        edited = source.replace("m(0.08)", "m(0.09)")
        job = worker_pb2.SketchSourceTransformJob()
        job.replace.source_utf8 = edited.encode()
        job.replace.function_name = "profile"
        write_digest(job.replace.expected_prior, prior)
        result = process_transform(job)
        self.assertEqual(result.WhichOneof("outcome"), "success")
        decoded = definition_values_from_wire(result.success.definition)
        self.assertEqual(decoded.source_digest, source_digest(edited))
        self.assertEqual(len(decoded.entities), 4)
        self.assertNotEqual(decoded.entities, target.entities)

    @settings(max_examples=50, deadline=None)
    @given(
        seed=st.integers(min_value=1, max_value=5_000_000),
        width=st.integers(min_value=1, max_value=2_000),
        height=st.integers(min_value=1, max_value=2_000),
        order=st.permutations(tuple(range(12))),
    )
    def test_rectangle_batch_round_trips_as_native_source(
        self, seed: int, width: int, height: int, order: tuple[int, ...]
    ) -> None:
        initial = create_sketch_source("profile")
        target, values = rectangle(seed, width, height)
        updated, digest = transformed(
            edit_job(
                initial,
                target,
                values,
                worker_pb2.SKETCH_SOURCE_EDIT_ACTION_APPEND,
                order,
            )
        )
        self.assertEqual(digest, source_digest(updated))
        recognition = recognize(updated, "profile")
        self.assertIsNotNone(recognition)
        assert recognition is not None
        calls = {
            value.id: value.code
            for value in (*recognition.entities, *recognition.constraints)
        }
        self.assertEqual(set(calls), {value.id for value in values})
        self.assertEqual(calls, {value.id: emit_call(value) for value in values})

    @settings(max_examples=35, deadline=None)
    @given(
        seed=st.integers(min_value=1, max_value=5_000_000),
        revisions=st.integers(min_value=1, max_value=20),
    )
    def test_repeated_edits_and_complete_delete_preserve_identity_contract(
        self, seed: int, revisions: int
    ) -> None:
        source = create_sketch_source("profile")
        definition, values = rectangle(seed, 80, 50)
        source, _ = transformed(
            edit_job(
                source,
                definition,
                values,
                worker_pb2.SKETCH_SOURCE_EDIT_ACTION_APPEND,
            )
        )
        entities = list(definition.entities)
        for index in range(revisions):
            selected = entities[index % len(entities)]
            assert isinstance(selected, LineEntity)
            delta = Length((index + 1) * 1.0e-6)
            replacement = replace(
                selected,
                start=(selected.start[0] + delta, selected.start[1]),
                end=(selected.end[0] + delta, selected.end[1]),
            )
            entities[index % len(entities)] = replacement
            definition = SketchDefinition(
                definition.plane, tuple(entities), definition.constraints
            )
            source, digest = transformed(
                edit_job(
                    source,
                    definition,
                    (replacement,),
                    worker_pb2.SKETCH_SOURCE_EDIT_ACTION_REPLACE,
                )
            )
            self.assertEqual(digest, source_digest(source))

        all_values = (*definition.entities, *definition.constraints)
        empty = SketchDefinition(definition.plane, ())
        source, _ = transformed(
            edit_job(
                source,
                empty,
                all_values,
                worker_pb2.SKETCH_SOURCE_EDIT_ACTION_DELETE,
                tuple(reversed(range(len(all_values)))),
            )
        )
        recognition = recognize(source, "profile")
        self.assertIsNotNone(recognition)
        assert recognition is not None
        self.assertEqual((recognition.entities, recognition.constraints), ((), ()))

    @settings(max_examples=40, deadline=None)
    @given(seed=st.integers(min_value=1, max_value=5_000_000))
    def test_stale_and_duplicate_edits_fail_atomically(self, seed: int) -> None:
        source = create_sketch_source("profile")
        definition, values = rectangle(seed, 40, 30)
        stale = edit_job(
            source,
            definition,
            values,
            worker_pb2.SKETCH_SOURCE_EDIT_ACTION_APPEND,
        )
        stale.edit.source_utf8 += b"\n"
        result = process_transform(stale)
        self.assertEqual(result.WhichOneof("outcome"), "failure")
        self.assertEqual(result.failure.diagnostics[0].code, "source.edit.stale")

        duplicate = edit_job(
            source,
            definition,
            (values[0], values[0]),
            worker_pb2.SKETCH_SOURCE_EDIT_ACTION_APPEND,
        )
        result = process_transform(duplicate)
        self.assertEqual(result.WhichOneof("outcome"), "failure")
        self.assertEqual(
            result.failure.diagnostics[0].code, "source.edit.duplicate-target"
        )
        self.assertEqual(recognize(source, "profile").entities, ())  # type: ignore[union-attr]

    def test_framing_handles_fragmentation_and_a_warm_process(self) -> None:
        jobs: list[bytes] = []
        expected: list[tuple[bytes, bytes]] = []
        for index, name in enumerate(("first_profile", "second_profile"), 1):
            envelope = worker_pb2.WorkerJobEnvelope()
            instance = UUID(uuid7(10_000))
            job = UUID(uuid7(10_000 + index))
            envelope.worker_instance_id.value = instance.bytes
            envelope.job_id.value = job.bytes
            envelope.sketch_source_transform.create.function_name = name
            payload = envelope.SerializeToString(deterministic=True)
            jobs.append(len(payload).to_bytes(4, "big") + payload)
            expected.append((instance.bytes, job.bytes))

            fragmented = FragmentedInput(jobs[-1], index)
            self.assertEqual(read_frame(fragmented), payload)
            self.assertIsNone(read_frame(fragmented))
            direct = worker_pb2.WorkerResultEnvelope.FromString(
                process_envelope(payload)
            )
            self.assertEqual(direct.worker_instance_id.value, instance.bytes)
            self.assertEqual(direct.job_id.value, job.bytes)

        completed = subprocess.run(
            [sys.executable, "-m", "kearne._worker"],
            input=b"".join(jobs),
            capture_output=True,
            check=True,
            timeout=20,
        )
        self.assertEqual(completed.stderr, b"")
        stream = io.BytesIO(completed.stdout)
        for instance, job in expected:
            payload = read_frame(stream)
            self.assertIsNotNone(payload)
            result = worker_pb2.WorkerResultEnvelope.FromString(payload)
            self.assertEqual(result.worker_instance_id.value, instance)
            self.assertEqual(result.job_id.value, job)
            self.assertEqual(
                result.sketch_source_transform.WhichOneof("outcome"), "success"
            )
        self.assertIsNone(read_frame(stream))

    def test_frame_limits_reject_before_allocation(self) -> None:
        for size in (0, MAXIMUM_FRAME_BYTES + 1):
            with self.assertRaises(ProtocolError):
                read_frame(io.BytesIO(size.to_bytes(4, "big")))


if __name__ == "__main__":
    unittest.main()
