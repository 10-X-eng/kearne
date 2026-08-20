#!/usr/bin/env python3
"""Measure generated-sketch recognition and structural editing."""

from __future__ import annotations

import argparse
import json
import platform
from math import sqrt
from time import perf_counter_ns
from uuid import UUID

from kearne.source import AppendCall, apply_edit, recognize, source_digest


def uuid7(value: int) -> str:
    timestamp = value & ((1 << 48) - 1)
    random_a = (value >> 48) & 0xFFF
    random_b = (value * 0x9E3779B97F4A7C15) & ((1 << 62) - 1)
    bits = (timestamp << 80) | (7 << 76) | (random_a << 64) | (2 << 62) | random_b
    return str(UUID(int=bits))


def sketch_source(count: int) -> str:
    entities = "\n".join(
        f'            line("{uuid7(index + 1)}", '
        f"(mm({index}), mm(0)), (mm({index + 1}), mm(0))),"
        for index in range(count)
    )
    return f"""from build123d import Sketch
from kearne.sketch import SketchDefinition, SketchPlane, line, point
from kearne.units import mm

def profile(plane: SketchPlane) -> Sketch:
    return SketchDefinition(
        plane=plane,
        entities=(
{entities}
        ),
        constraints=(),
    ).build()
"""


def distribution(values: list[float]) -> dict[str, float]:
    ordered = sorted(values)

    def percentile(numerator: int, denominator: int) -> float:
        rank = (numerator * len(ordered) + denominator - 1) // denominator
        return ordered[max(rank, 1) - 1]

    mean = 0.0
    squared_distance = 0.0
    for count, value in enumerate(ordered, start=1):
        delta = value - mean
        mean += delta / count
        squared_distance += delta * (value - mean)

    return {
        "min_ms": ordered[0],
        "p50_ms": percentile(1, 2),
        "p95_ms": percentile(95, 100),
        "p99_ms": percentile(99, 100),
        "max_ms": max(values),
        "mean_ms": mean,
        "population_stddev_ms": sqrt(squared_distance / len(ordered)),
    }


def measure(count: int, samples: int, warmup: int) -> dict[str, object]:
    source = sketch_source(count)
    digest = source_digest(source)
    appended_id = uuid7(count + 1)
    edit = AppendCall("entities", f'point("{appended_id}", (mm(0), mm(0)))')

    for _ in range(warmup):
        recognized = recognize(source, "profile")
        edited = apply_edit(source, "profile", digest, edit)
        if recognized is None or len(edited.recognition.entities) != count + 1:
            raise RuntimeError("benchmark invariant failed")

    recognition: list[float] = []
    editing: list[float] = []
    for _ in range(samples):
        started = perf_counter_ns()
        recognized = recognize(source, "profile")
        recognition.append((perf_counter_ns() - started) / 1_000_000)
        if recognized is None or len(recognized.entities) != count:
            raise RuntimeError("recognition invariant failed")

        started = perf_counter_ns()
        edited = apply_edit(source, "profile", digest, edit)
        editing.append((perf_counter_ns() - started) / 1_000_000)
        if len(edited.recognition.entities) != count + 1:
            raise RuntimeError("edit invariant failed")

    return {
        "workload": "SKETCH-SOURCE",
        "entities": count,
        "source_bytes": len(source.encode()),
        "samples": samples,
        "warmup": warmup,
        "python": platform.python_version(),
        "platform": platform.platform(),
        "recognize": distribution(recognition),
        "edit_and_reparse": distribution(editing),
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--entities", type=int, default=100)
    parser.add_argument("--samples", type=int, default=200)
    parser.add_argument("--warmup", type=int, default=20)
    parser.add_argument("--max-recognize-p95-ms", type=float)
    parser.add_argument("--max-edit-p95-ms", type=float)
    arguments = parser.parse_args()
    if arguments.entities < 1 or arguments.samples < 2 or arguments.warmup < 0:
        parser.error(
            "entities and samples must be positive; samples must be at least 2"
        )

    result = measure(arguments.entities, arguments.samples, arguments.warmup)
    print(json.dumps(result, indent=2, sort_keys=True))
    maximum = arguments.max_recognize_p95_ms
    recognition = result["recognize"]
    if (
        maximum is not None
        and isinstance(recognition, dict)
        and recognition["p95_ms"] > maximum
    ):
        return 1
    maximum = arguments.max_edit_p95_ms
    edit = result["edit_and_reparse"]
    if maximum is not None and isinstance(edit, dict) and edit["p95_ms"] > maximum:
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
