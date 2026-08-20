# ADR-0019: Deterministic Evaluation Scheduler

- **Status:** Accepted
- **Date:** 2026-08-19
- **Related:** `EVAL-008`–`EVAL-016`, [process ownership](../foundations/07-processes-and-ipc.md)

## Context

Evaluation needs CAD-specific dependency, revision, priority-aging, resource, deduplication, subscriber-cancellation, and stale-publication rules. A task executor does not own those semantics. Implementing policy separately for tests, threads, and workers would make interleavings disagree.

## Decision

Kearne owns one deterministic scheduling state machine. Serialized events produce dispatch, cancellation, and terminal-publication decisions without creating threads, reading clocks, or executing evaluators.

Here, deterministic describes reproducible policy decisions and test replay. It does not serialize engineering work or require concurrent jobs to finish in a deterministic order.

A bounded native thread pool and the worker supervisor are executor adapters. Both receive admitted work from the same state machine and return sequenced events through the same boundary. Tests drive that boundary with a virtual clock and deterministic executor.

Ready queues use bounded priority aging. Evaluation-key deduplication and subscriber state remain scheduler concerns; executor adapters do not infer them.

## Evidence

The production state machine and generated model suite pass Debug, Release, and ASan/UBSan. On the recorded benchmark host, Release p95 for 1,000/10,000/100,000 queued jobs is 5.884/4.833/5.494 µs for admission, 4.835/6.544/6.591 µs for cancel-retire-replace lifecycle churn, and 3.581/5.527/5.723 µs for deduplicated priority submission. The benchmark retires 100,000 subscribers from one terminal shared job in 24.737 ms; the stress suite verifies that retirement preserves a newer projection. Boundaries and scaling data are recorded in the [performance program](../delivery/04-performance.md#evaluation-scheduler-gate).

Executor adapters, end-to-end cancellation timing, and worker retry policy remain open.

## Consequences

Every executor runs the same generated conformance model. Scheduling can be replayed from events, stale work cannot bypass one publication gate, and policy remains independent of Qt and third-party runtimes.

The coordinator serializes scheduler events. Executors may run concurrently but never mutate scheduler state. Scheduler data structures and admission decisions require scaling benchmarks.

## Alternatives rejected

- A third-party task graph still requires a second CAD policy layer and complicates deterministic replay.
- Qt thread-pool ownership would couple headless evaluation to the desktop runtime.
- Separate in-process and worker schedulers would duplicate correctness rules.
