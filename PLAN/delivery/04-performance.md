# Performance Program

- **Status:** In progress; reference profiles and end-to-end gates remain open
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
DIRECT        selected faces, propagation/healing policy, topology growth
SURFACE       boundaries/guides, degree, continuity, sampling, topology growth
SHEET         bends, reliefs, rules, folded/flat complexity
IMPORT        source size/entity count/format complexity
HISTORY       revisions, branches, checkpoints, artifact ratio
VIEW          unique meshes, instances, triangles, topology ranges, styles
ASSEMBLY      occurrences, nesting, unique definitions, loaded BREP ratio
SIM           elements, result fields, iterations/backend
CAM           stock, operations, path segments, removal-grid resolution
DRAWING       views, exact edges, annotations, sheets
BOM           occurrences, hierarchy, configurations, columns, groups
MATERIAL      catalog rows/properties, representations, repeated selections
API           request/query volume, subscribers, page size
```

Seeds, generator/schema versions, and artifacts accompany every result.

### PERF-005 — Scaling curves

Benchmarks run multiple sizes and report complexity trends. A single “10,000 components” result is invalid without unique mesh count, triangle count, occurrence depth, styles, viewport, loaded exact geometry, and camera path.

### PERF-008 — Versioned workload manifests

Each named workload is a versioned manifest containing generator/schema versions, seed or immutable inputs, expected correctness relations, input and output cardinalities, quality/tolerance profile, warm/cold policy, cancellation points, and required trace counters. Changing workload meaning allocates a new version; a faster result from less work cannot replace a baseline.

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
| Recognized model source edit | `MODEL-EDIT-100`, source transform + reparse + invalidation, evaluation excluded | < 16 ms p95 |
| Analytic direct-edit preview | `DIRECT-100`, first valid coarse preview | < 100 ms p95 |
| Surface preview | `SURFACE-100`, first valid coarse preview | < 500 ms p95 |
| Superseded preview acknowledgement | outside uninterruptible call | < 50 ms p95 |
| Project open | MVP metadata/checkpoint, caches excluded | < 500 ms p95 |
| 10k assembly navigation | defined `VIEW-10K` profile | 60 FPS p95 frame target |
| CAM path generation | `CAM-REF-1`, complete current paths | < 2 s p95 |
| CAM removal simulation | `CAM-REF-1`, first update / complete at reference resolution | < 500 ms / < 10 s p95 |
| CAM postprocess | `CAM-REF-1`, atomic validated output | < 1 s p95 |
| Drawing full regeneration | `DRAWING-REF-1`, ten-view sheet | < 2 s p95 |
| Drawing affected-view regeneration | `DRAWING-REF-1`, one changed source view | < 500 ms p95 |
| Drawing layout edit visible | `DRAWING-REF-1`, HLR excluded | < 16.7 ms p95 |
| Material catalog availability | `MATERIAL-REF-1`, 100k rows, cold index | < 250 ms p95 |
| Material catalog search | `MATERIAL-REF-1`, 100k rows, warm index | < 50 ms p95 |
| Effective material resolution | `MATERIAL-REF-1`, hot project/library indexes | < 1 ms p95 |

Candidate targets become binding only after their fixture and reference profile are accepted.

### Sketch solver candidate

The current production [benchmark](../../modules/adapters/sketch_ceres/tests/solver_benchmark.cpp) solves a perturbed chain of points with one fixed anchor and horizontal/vertical distance constraints. Each timed call includes solver setup, nonlinear solve, independent residual validation, rank analysis, and result construction; it excludes source recognition, job/IPC, and UI publication. Every sample verifies `Solved` and zero DOF after one warm-up.

Host: KDE neon 24.04, GCC 13.3.0, x86-64, two 4-core/8-thread Intel Xeon Silver 4112 sockets at up to 3.0 GHz, 125 GiB RAM. Times use `steady_clock`; p95 equals the maximum at these sample counts.

| Build | Entities | Constraints | Iterations | Samples | p50 | p95 |
|---|---:|---:|---:|---:|---:|---:|
| Release | 100 | 199 | 3 | 11 | 2.219 ms | 3.260 ms |
| Release | 1,000 | 1,999 | 6 | 7 | 33.510 ms | 37.050 ms |
| Debug | 100 | 199 | 3 | 11 | 52.147 ms | 52.707 ms |
| Debug | 1,000 | 1,999 | 6 | 7 | 1,187.466 ms | 1,199.437 ms |

The 100-entity solver contribution is below the provisional 16 ms budget on this host. This does not establish end-to-end preview latency or an accepted reference-machine baseline. The 1,000-entity result spans multiple frames and keeps its final target open.

### Sketch source session candidate

The production [source-session benchmark](../../sdk/python/benchmarks/source_session.py) opens one recognized native Python revision, applies validated replacement batches, and verifies cardinality and bounded retained/peak memory. Timings include byte splicing, AST reparse, semantic validation, and immutable result publication; they exclude evaluation and UI publication. Host matches the solver candidate; Python is 3.12.3.

| Entities | Source | Operation | Samples | p50 | p95 | p99 | max |
|---:|---:|---|---:|---:|---:|---:|---:|
| 100 | 9,470 B | one replacement | 101 | 4.842 ms | 6.312 ms | 7.066 ms | 8.811 ms |
| 100 | 9,470 B | eight replacements | 101 | 6.166 ms | 7.337 ms | 8.515 ms | 8.637 ms |
| 1,000 | 94,071 B | one replacement | 11 | 61.794 ms | 76.308 ms | 76.308 ms | 76.308 ms |
| 1,000 | 94,071 B | eight replacements | 11 | 63.701 ms | 86.172 ms | 86.172 ms | 86.172 ms |

Retained/peak memory was 70,838/2,309,283 B at 100 entities and 727,373/17,266,388 B at 1,000. The 100-entity session passes the provisional 16 ms source-edit budget. Larger revisions must use scheduled work; synchronous batches are capped at 32 edits.

### Document local-edit candidate

The production [local-edit benchmark](../../modules/document/benchmarks/local_edit_scaling.cpp) replaces one source content reference in an immutable project whose functions all reference that stable path. Timed work includes mutation validation, persistent-table update, Merkle/root digest update, and snapshot construction. It excludes content hashing/storage, command handling, source recognition, invalidation, and evaluation. Every sample verifies the new content, function count, and changed project identity after five warm-ups.

Host and toolchain match the solver candidate above. Release results:

| Functions | Samples | p50 | p95 | p99 | max | population σ |
|---:|---:|---:|---:|---:|---:|---:|
| 100 | 50 | 0.019 ms | 0.019 ms | 0.028 ms | 0.028 ms | 0.001 ms |
| 1,000 | 25 | 0.019 ms | 0.019 ms | 0.019 ms | 0.019 ms | <0.001 ms |
| 10,000 | 10 | 0.018 ms | 0.019 ms | 0.019 ms | 0.019 ms | <0.001 ms |

Content replacement does not revalidate unrelated ownership or call graphs. Structural mutations retain full validation; their incremental-index scaling target remains open.

### Evaluation scheduler gate

The production [scheduler benchmark](../../modules/evaluation/benchmarks/scheduler_benchmark.cpp) holds one dispatched one-slot blocker while growing the ready queue. Admission times one `Submit`. Lifecycle churn times `CancelSubscription`, `RetireJob`, `RetireProjection`, and replacement `Submit` together. Deduplication times one `Submit` that attaches to an existing key and refreshes priority. Every boundary verifies queue population, identity, currentness, and resource occupancy. No warm-up samples are removed; the executable reports p50, p95, p99, maximum, and population standard deviation.

Host and toolchain match the solver candidate above. Release timings use `steady_clock`.

| Queued jobs | Admission samples | Admission p95 | Lifecycle samples | Lifecycle p95 | Deduplication samples | Deduplication p95 |
|---:|---:|---:|---:|---:|---:|---:|
| 1,000 | 1,000 | 5.884 µs | 2,001 | 4.835 µs | 2,001 | 3.581 µs |
| 10,000 | 10,000 | 4.833 µs | 2,001 | 6.544 µs | 2,001 | 5.527 µs |
| 100,000 | 100,000 | 5.494 µs | 2,001 | 6.591 µs | 2,001 | 5.723 µs |

The retirement boundary times only `RetireJob` after one terminal job has the stated number of shared subscribers and a reachable immutable result. Retirement erases each subscriber once and clears each affected projection once; correctness checks cover reclamation and projection retirement.

| Shared subscribers | Samples | Retirement |
|---:|---:|---:|
| 1,000 | 1 | 148.635 µs |
| 10,000 | 1 | 1.543 ms |
| 100,000 | 1 | 24.737 ms |

This accepts scheduler-policy scaling, not executor queue-to-start, cancellation SLA, artifact-publication latency, or a reference-machine baseline.

### Sketch definition wire candidate

The production [wire benchmark](../../modules/adapters/sketch_wire/benchmarks/sketch_wire_benchmark.cpp) generates equal populations of lines and horizontal constraints. Before timing, both native-to-wire-to-native and native-to-bytes-to-native paths must reproduce the typed definition. Five warm-ups precede each boundary: domain validation plus Protobuf construction; Protobuf serialization; Protobuf parse; or bounded wire validation, unknown-field scan, strong conversion, and sketch-domain validation. Source recognition, solving, IPC, and attachment-plane evaluation are excluded.

Host and toolchain match the solver candidate above. Release results:

| Entities / constraints | Boundary | Samples | p50 | p95 | p99 | max | population σ |
|---:|---|---:|---:|---:|---:|---:|---:|
| 100 / 100 | domain to wire | 101 | 0.124 ms | 0.286 ms | 0.303 ms | 0.303 ms | 0.056 ms |
| 100 / 100 | serialize | 101 | 0.011 ms | 0.011 ms | 0.041 ms | 0.044 ms | 0.005 ms |
| 100 / 100 | parse | 101 | 0.077 ms | 0.217 ms | 0.218 ms | 0.225 ms | 0.052 ms |
| 100 / 100 | wire to domain | 101 | 0.228 ms | 0.515 ms | 0.534 ms | 0.561 ms | 0.092 ms |
| 1,000 / 1,000 | domain to wire | 31 | 1.399 ms | 1.983 ms | 2.126 ms | 2.126 ms | 0.243 ms |
| 1,000 / 1,000 | serialize | 31 | 0.105 ms | 0.360 ms | 0.407 ms | 0.407 ms | 0.075 ms |
| 1,000 / 1,000 | parse | 31 | 1.075 ms | 1.546 ms | 1.991 ms | 1.991 ms | 0.213 ms |
| 1,000 / 1,000 | wire to domain | 31 | 2.348 ms | 3.220 ms | 3.436 ms | 3.436 ms | 0.333 ms |
| 10,000 / 10,000 | domain to wire | 11 | 19.882 ms | 24.229 ms | 24.229 ms | 24.229 ms | 1.616 ms |
| 10,000 / 10,000 | serialize | 11 | 1.375 ms | 2.067 ms | 2.067 ms | 2.067 ms | 0.208 ms |
| 10,000 / 10,000 | parse | 11 | 14.701 ms | 14.850 ms | 14.850 ms | 14.850 ms | 0.143 ms |
| 10,000 / 10,000 | wire to domain | 11 | 24.353 ms | 24.626 ms | 24.626 ms | 24.626 ms | 0.108 ms |

This boundary scales linearly on the measured curve. It does not set an end-to-end sketch latency target.

### Sketch pick-index candidate

The production [pick-index benchmark](../../modules/render/tests/pick_benchmark.cpp) builds packed immutable BVHs for sparse, coincident, concentric, global-line, and far-outlier scenes at 1,000, 10,000, and 100,000 primitives. Validation compares completed queries with an independent analytic scan. Timed queries allocate no memory; ambiguous queries return `WorkBudgetExceeded` without a partial pick. Host and toolchain match the solver candidate.

Release results at 100,000 primitives; each query row contains 2,000 samples and each build row contains three:

| Profile | Targets | Build p95 | Query p95 | Outcome | Peak build payload |
|---|---:|---:|---:|---|---:|
| Sparse | 233,594 | 174.892 ms | 7.792 µs | hit/miss | 29,633,360 B |
| Coincident | 100,000 | 49.375 ms | 31.729 µs | bounded refusal | 12,686,000 B |
| Concentric | 200,000 | 105.962 ms | 32.938 µs | bounded refusal | 25,371,632 B |
| Global lines | 300,000 | 198.324 ms | 3.218 µs | hit | 38,057,456 B |
| Far outlier | 100,000 | 51.280 ms | 3.510 µs | hit | 12,686,000 B |

The sparse all-scene query refuses at exactly 1,024 refined targets in one pass with 63.164 µs p95. A single 100,000-primitive cancellation sample completed in 20.824 ms; a cancellation distribution and reference-machine baseline remain open.

### Sketch scene adapter candidate

The production [sketch scene benchmark](../../apps/desktop/tests/sketch_scene_item_benchmark.cpp) uses deterministic mixed point, line, circle, and arc scenes. It validates bounds and UI-to-canonical picks before timing. Preparation includes CPU tessellation, stable spatial chunks, visibility data, and the pick index. Synchronization consumes a prepared packet. Camera timing uses 2,000 retained-mesh samples. Visibility and upload work is capped at 128 spatial nodes, 64 selected chunks, 2 MiB, and 32 node operations per slice. Upload-copy timing includes a scratch allocation and `memcpy`, not QSG creation or GPU transfer.

Host and toolchain match the solver candidate above. Release results retain all samples without warm-up removal. Preparation and synchronization contain seven complete samples; slice distributions contain every slice from seven complete runs.

| Primitives | Retained mesh | Peak prepare | Prepare p50/p95 | Sync p50/p95 |
|---:|---:|---:|---:|---:|
| 1,000 | 2,032,332 B | 5,727,932 B | 25.681 / 27.134 ms | 0.000 / 0.001 ms |
| 10,000 | 20,717,480 B | 59,431,916 B | 279.851 / 295.231 ms | 0.000 / 0.001 ms |
| 100,000 | 203,283,852 B | 538,277,764 B | 2,830.569 / 3,031.268 ms | 0.000 / 0.002 ms |

| Primitives | Camera p50/p95 | Visibility slice p50/p95 | Upload-copy slice p50/p95 |
|---:|---:|---:|---:|
| 1,000 | 0.223 / 0.239 µs | 1.900 / 5.933 µs | 21.633 / 40.917 µs |
| 10,000 | 0.215 / 0.233 µs | 1.935 / 10.179 µs | 22.143 / 59.247 µs |
| 100,000 | 0.223 / 0.246 µs | 1.899 / 9.338 µs | 23.884 / 60.209 µs |

The complete run took 25.89 seconds and peaked at 764,052 KiB RSS. Retained and peak figures include owned objects and container capacities, source-neutral provenance, and conservative non-overlapping preparation scratch; allocator metadata is excluded. Native OpenGL integration covers progressive publication, cancellation, retained camera/palette updates, render-off behavior, closing one of two controls, hiding and showing an item, item destruction/recreation, render-control invalidation/reinitialization, exact-stamp recovery, and final application reclaimer drain. AddressSanitizer covers the same lifecycle. GPU latency, simultaneous multi-control invalidation, Vulkan output, and an accepted `VIEW` manifest remain open.

The [ADR-0020](../adr/0020-inline-qrhi-sketch-renderer.md) acceptance run uses the production inline renderer on every Qt 6.8/6.11 and OpenGL/Vulkan pair. It records render-thread preparation and draw time, upload bytes, draw calls, render-target count, GPU bytes by render epoch, cache reuse, and input-to-presented-frame latency for the versioned 1,000/10,000/100,000 Sketch workloads; CPU copy timing cannot substitute for GPU evidence.

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

### PERF-009 — Finite resource envelopes

Every accepted workload profile sets peak coordinator/UI, worker, GPU, transient-transfer, published-artifact, and cache-byte limits on minimum and recommended machines. A profile without finite release limits is experimental and cannot back an advertised capability. Scaling runs at increasing cardinalities must match the declared complexity and identify retained bytes by artifact/lease/cache owner.

### PERF-010 — Cancellation budgets

Cancellation request acknowledgement is below 50 ms p95. A cooperative reference job reaches terminal `Cancelled` below 250 ms p95 after acknowledgement. After its configured grace period, a dedicated uninterruptible worker terminates below 1 s p95. Cancellation measurements include queue removal, resource/lease release, and proof that no partial or stale result became current.

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
- **PERF-OPEN-005:** Accepted `MODEL-EDIT-100`, `DIRECT-100`, `SURFACE-100`, `CAM-REF-1`, `DRAWING-REF-1`, and `MATERIAL-REF-1` manifests and finite resource envelopes.

## 10. Definition of done

The program is implemented when parameterized workloads run reproducibly, every headline target has a complete fixture/environment definition, traces localize time and allocation by stage, and CI/release gates detect both absolute and complexity regressions.
