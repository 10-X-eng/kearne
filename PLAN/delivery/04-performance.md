# Performance Program

- **Status:** Proposed
- **Requirement prefix:** `PERF`
- **Depends on:** [Evaluation](../foundations/03-evaluation-and-jobs.md), [rendering](../capabilities/01-rendering-and-selection.md), [test strategy](03-test-strategy.md)
- **Unblocks:** MVP and large-assembly release gates

## 1. Purpose

Make responsiveness and scale reproducible requirements. A target is valid only with hardware, OS/build, workload, warm/cold state, percentile, sample method, and measurement boundary.

## 2. Measurement rules

### PERF-001 — Distribution, not best case

Interactive and job metrics report p50, p95, p99, maximum, sample count, and variability after defined warm-up. Release gates use p95 unless a requirement states otherwise.

### PERF-002 — Correctness first

Benchmarks validate output/invariants before recording time. Skipped work, stale artifacts, lower unreported quality, or failed operations cannot count as performance improvements.

### PERF-003 — Separate latency components

Measure command validation/commit, invalidation, queue delay, execution, artifact publication, tessellation, scene publication, and frame latency separately as well as end to end.

### PERF-004 — No UI blocking hidden by averages

UI/render thread stalls are traced individually. A smooth average frame rate does not pass when input has long-tail freezes.

## 3. Reference environments

Before Stage 1 freeze two versioned profiles:

- **Minimum:** lowest supported CPU/RAM/GPU/storage for functional support.
- **Recommended:** hardware used for stated target budgets.

Each profile records CPU topology, RAM, GPU/driver, display resolution/scale, storage/filesystem, power mode, OS build, compiler, Qt/OCCT builds, and background-process policy. CI virtual machines are trend environments, not substitutes for physical GPU/storage gates.

## 4. Parameterized workloads

Generators create reproducible workloads from seeds and profiles:

```text
FUNCTION-DAG  module/function count, fan-in/out, affected-subgraph ratio
SKETCH        entities, constraints, rank/conflict class
PART          analytic function families and topology edit sequence
IMPORT        source size/entity count/format complexity
HISTORY       revisions, branches, checkpoints, artifact ratio
VIEW          unique meshes, instances, triangles, topology ranges, styles
ASSEMBLY      occurrences, nesting, unique definitions, loaded BREP ratio
SIM           elements, result fields, iterations/backend
API           request/query volume, subscribers, page size
```

Seeds, generator/schema versions, and artifacts accompany every result.

### PERF-005 — Scaling curves

Benchmarks run multiple sizes and report complexity trends. A single “10,000 components” result is invalid without unique mesh count, triangle count, occurrence depth, styles, viewport, loaded exact geometry, and camera path.

## 5. Initial budgets

On recommended hardware and release builds:

| Metric | Workload | Target |
|---|---|---:|
| Cold shell start to interactive empty workspace | clean OS file cache policy documented | < 3 s p95 |
| Warm start | warm executable/assets | < 1.5 s p95 |
| Camera input to visible frame | MVP reference part, 60 Hz | < 16.7 ms p95 |
| Preselection visible | reference viewport | < 50 ms p95 |
| Small command commit/invalidation | reference part, no kernel work | < 10 ms p95 engine time |
| Simple model-function evaluation | generated safe-domain primitive | < 100 ms p95 where the construction supports it |
| Superseded preview acknowledgement | outside uninterruptible call | < 50 ms p95 |
| Project open | MVP metadata/checkpoint, caches excluded | < 500 ms p95 |
| 10k assembly navigation | defined `VIEW-10K` profile | 60 FPS p95 frame target |

Targets marked dependent on a prototype become binding only after the fixture/profile is accepted.

## 6. Resource budgets

Track peak and steady:

- process/worker resident and virtual memory;
- artifact/cache bytes and churn;
- GPU allocations and upload volume;
- file descriptors/handles;
- threads/processes;
- queue depth and abandoned work;
- project database amplification;
- network/token/cost for optional AI/cloud.

### PERF-006 — Proportionality

Local document edits should allocate and execute in proportion to changed records and affected graph. Rendering repeated instances should allocate geometry in proportion to unique representations plus occurrence metadata. Deviations require a measured rationale.

### PERF-007 — Bounded caches

Every cache has a measurable key, byte-accounting method, eviction policy, pinned/leased state, and pressure behavior. OS memory pressure must not turn cache growth into application failure.

## 7. Continuous regression control

- PR smoke: stable small fixtures and architecture counters.
- Nightly: distributions, scaling curves, leaks, cancellation storms, and cache pressure.
- Release: physical reference machines, platform/GPU/storage matrix, long soak.

A statistically credible p95 regression above the per-benchmark threshold (initially 10% for stable micro/meso benchmarks) fails or requires a reviewed baseline change linked to cause and tradeoff. Absolute budgets still apply.

## 8. Instrumentation

Correlated traces use revision, transaction, command, evaluation key, job, worker, artifact, scene generation, and frame IDs. Development builds expose queue state, evaluator/cache hits, dependency visits, worker utilization, mesh/GPU memory, and UI/render stalls.

Telemetry in distributed builds is opt-in, documented, redacted, and unnecessary for local benchmark correctness.

## 9. Open decisions

- **PERF-OPEN-001:** Exact minimum/recommended machines and GPU/driver matrix.
- **PERF-OPEN-002:** Benchmark framework and trace format.
- **PERF-OPEN-003:** Accepted `VIEW-10K`, MVP part, history, and import profiles.
- **PERF-OPEN-004:** Per-benchmark regression thresholds and noisy-run policy.

## 10. Definition of done

The program is implemented when parameterized workloads run reproducibly, every headline target has a complete fixture/environment definition, traces localize time and allocation by stage, and CI/release gates detect both absolute and complexity regressions.
