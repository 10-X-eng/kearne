#!/usr/bin/env python3
"""Measure revision-keyed Sketch source-session edits and retained memory."""

from __future__ import annotations

import argparse
import gc
import json
import platform
import tracemalloc
from time import perf_counter_ns

from kearne.source import ReplaceCall, open_edit_session
from source_edit import distribution, sketch_source, uuid7


def replacement(index: int, value: int) -> ReplaceCall:
    stable = uuid7(index + 1)
    return ReplaceCall(
        "entities",
        stable,
        f'line("{stable}", (mm({value}), mm(0)), (mm({value + 1}), mm(0)))',
    )


def measure(
    count: int, batch_size: int, samples: int, warmup: int
) -> dict[str, object]:
    source = sketch_source(count)
    session = open_edit_session(source, "profile")
    if session is None:
        raise RuntimeError("benchmark source is not recognized")
    batch = tuple(
        replacement(ordinal % count, count + ordinal + 1)
        for ordinal in range(batch_size)
    )

    for _ in range(warmup):
        single = (replacement(0, count + 1),)
        if (
            len(session.apply(session.source_digest, single).recognition.entities)
            != count
        ):
            raise RuntimeError("single-edit invariant failed")
        if (
            len(session.apply(session.source_digest, batch).recognition.entities)
            != count
        ):
            raise RuntimeError("batch invariant failed")

    interactive_times: list[float] = []
    batch_times: list[float] = []
    interactive_session = session
    for sample in range(samples):
        single = (replacement(0, count + sample % 2 + 1),)
        started = perf_counter_ns()
        edited = interactive_session.apply(interactive_session.source_digest, single)
        interactive_times.append((perf_counter_ns() - started) / 1_000_000)
        if len(edited.recognition.entities) != count:
            raise RuntimeError("single-edit invariant failed")
        interactive_session = edited

        started = perf_counter_ns()
        edited = session.apply(session.source_digest, batch)
        batch_times.append((perf_counter_ns() - started) / 1_000_000)
        if len(edited.recognition.entities) != count:
            raise RuntimeError("batch invariant failed")

    gc.collect()
    tracemalloc.start()
    memory_session = open_edit_session(source, "profile")
    if memory_session is None:
        raise RuntimeError("memory source is not recognized")
    for ordinal in range(16):
        edit = replacement(ordinal % count, count + ordinal + 100)
        memory_session = memory_session.apply(memory_session.source_digest, (edit,))
    gc.collect()
    retained_bytes, peak_bytes = tracemalloc.get_traced_memory()
    tracemalloc.stop()
    source_bytes = len(source.encode())
    retained_limit = source_bytes * 12 + 262_144
    peak_limit = source_bytes * 256 + 1_048_576
    if retained_bytes > retained_limit:
        raise RuntimeError("session retained-memory bound exceeded")
    if peak_bytes > peak_limit:
        raise RuntimeError("session peak-memory bound exceeded")

    batch_distribution = distribution(batch_times)
    return {
        "workload": "SKETCH-SOURCE-SESSION",
        "entities": count,
        "source_bytes": source_bytes,
        "batch_edits": batch_size,
        "samples": samples,
        "warmup": warmup,
        "python": platform.python_version(),
        "platform": platform.platform(),
        "interactive_edit": distribution(interactive_times),
        "batch_edit": batch_distribution,
        "batch_p95_per_edit_ms": batch_distribution["p95_ms"] / batch_size,
        "memory_iterations": 16,
        "retained_bytes": retained_bytes,
        "retained_limit_bytes": retained_limit,
        "peak_bytes": peak_bytes,
        "peak_limit_bytes": peak_limit,
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--entities", type=int, default=100)
    parser.add_argument("--batch-edits", type=int, default=8)
    parser.add_argument("--samples", type=int, default=101)
    parser.add_argument("--warmup", type=int, default=10)
    parser.add_argument("--max-interactive-p95-ms", type=float)
    parser.add_argument("--max-batch-p95-ms", type=float)
    arguments = parser.parse_args()
    if (
        arguments.entities < 1
        or arguments.batch_edits < 1
        or arguments.samples < 2
        or arguments.warmup < 0
    ):
        parser.error("counts must be positive; samples must be at least 2")
    result = measure(
        arguments.entities,
        arguments.batch_edits,
        arguments.samples,
        arguments.warmup,
    )
    print(json.dumps(result, indent=2, sort_keys=True))
    interactive = result["interactive_edit"]
    batch = result["batch_edit"]
    if (
        arguments.max_interactive_p95_ms is not None
        and isinstance(interactive, dict)
        and interactive["p95_ms"] > arguments.max_interactive_p95_ms
    ):
        return 1
    if (
        arguments.max_batch_p95_ms is not None
        and isinstance(batch, dict)
        and batch["p95_ms"] > arguments.max_batch_p95_ms
    ):
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
