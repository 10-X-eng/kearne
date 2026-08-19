# Dependency Evaluation and Jobs

- **Status:** Proposed
- **Requirement prefix:** `EVAL`
- **Depends on:** [Document model](01-document-model.md), [commands and revisions](02-commands-transactions-revisions.md)
- **Unblocks:** modeling, rendering, imports, Python, simulation

## 1. Purpose

Turn immutable semantic snapshots into geometry and other artifacts through bounded multithreaded and multiprocess execution while keeping the UI responsive and stale work harmless.

## 2. Evaluation graph

Dependencies are derived from typed references and evaluator-declared inputs. The graph is scoped by document revision and configuration context.

### EVAL-001 — Declared inputs only

An evaluator MUST declare every semantic entity, result slot, source artifact, configuration value, external database record, and numerical profile that can affect its output. Hidden reads of global “active” state are prohibited.

### EVAL-002 — Cycle detection before execution

The engine derives the affected dependency graph and reports typed cycles before scheduling nodes. A cycle diagnostic identifies the shortest cycle path; no node in the cycle executes.

### EVAL-003 — Dirty is derived

Persistent entities do not own mutable dirty flags. A requested result is clean when a valid artifact exists for its exact `EvaluationKey`; otherwise it is missing/dirty. Operational projections may expose user-friendly states.

## 3. Evaluation key

```text
EvaluationKey = digest(
  evaluator fingerprint,
  feature kind and schema version,
  canonical parameter payload,
  input result/artifact digests,
  resolved semantic references,
  configuration context,
  units/numerical profile,
  explicitly declared environment inputs
)
```

### EVAL-004 — Content-addressed reuse

Equal evaluation keys may reuse immutable results across undo/redo, branches, workspaces, and process restarts. Cache identity MUST NOT use pointer identity, display name, OCCT's process-local hash, or document revision alone.

### EVAL-005 — Evaluator fingerprint

The fingerprint includes the native/plugin evaluator version and relevant third-party versions. If deterministic compatibility across patch versions is proven, an adapter may expose a stable compatibility fingerprint rather than the raw build ID.

## 4. Evaluation contract

```text
EvaluationRequest
  revision_id
  configuration_context
  target ResultRefs
  priority
  generation_id
  resource_budget

FeatureEvaluationResult
  key
  status
  output artifacts by stable output key
  topology publication
  measurements and bounds
  diagnostics
  timing/resource metadata
  determinism classification
```

Evaluators consume immutable values and artifact handles. They do not mutate the document, publish UI objects, enqueue undeclared work, or inspect the current workspace head.

### EVAL-006 — Result completeness

An evaluator publishes one complete immutable result or a failure/cancellation result. Partial artifacts are private until finalization and are garbage-collectable after worker failure.

### EVAL-007 — Last-known-good separation

The UI may display a clearly marked last-known-good artifact from an ancestor revision while current evaluation is pending or failed. Downstream engineering evaluation MUST NOT treat that artifact as current input unless the feature contract explicitly defines a degraded mode.

## 5. Scheduling

Job priorities are:

```text
InteractivePreview > VisibleResult > UserRequested > Normal > Background > Idle
```

### EVAL-008 — Dependency and revision correctness

A job runs only after all required input results for the same requested revision/configuration are valid. Completion from an older generation may populate the content-addressed cache but MUST NOT publish as the active projection for a newer generation.

### EVAL-009 — Bounded queues

Each job class has queue, concurrency, memory, and CPU limits. Superseded preview work is cancelled or dropped before durable user-requested work. Background jobs cannot starve visible results.

### EVAL-010 — Deduplication

Concurrent requests for the same evaluation key share one execution and receive independent subscriber cancellation. Underlying work is cancelled only when policy permits and no required subscriber remains.

### EVAL-011 — Fairness

Sustained interactive edits MUST NOT permanently starve explicit exports, saves, or user-requested solves. The scheduler uses bounded priority aging and reports resource contention.

### EVAL-012 — Bounded multithreading

In-process work uses configured pools and resource classes rather than a thread per job. The same dependency scheduler dispatches isolated process work; CPU, memory, and third-party thread use count against one resource budget.

## 6. Cancellation and failure

### EVAL-013 — Cooperative first, isolation second

Evaluators check cancellation between safe stages. An uninterruptible third-party call runs in a process that may be terminated within the operation's cancellation SLA when practical.

### EVAL-014 — Cancellation is not failure

Cancellation yields a distinct status and does not attach a persistent feature-error diagnostic. A superseding generation is normal operational behavior.

### EVAL-015 — Worker death containment

Worker death produces `WorkerTerminated` for its in-flight jobs, releases shared artifacts after lease expiry, and may retry according to bounded policy. It cannot modify the document or durable artifact index directly.

### EVAL-016 — Retry classification

Jobs declare whether failures are deterministic, transient, resource-related, or process faults. Deterministic geometry failures are not automatically retried with the same key.

## 7. Operational state machine

```text
Absent -> Queued -> WaitingForInputs -> Running
Running -> Succeeded | Failed | Cancelled | WorkerLost
Queued/Waiting -> Cancelled | Superseded
WorkerLost -> Queued (bounded retry) | Failed
```

User-facing node health is derived from requested key, job state, available current artifact, and diagnostics. `Warning` is a result attribute, not a mutually exclusive scheduling state.

## 8. Progress

Progress events contain stage, completed units, total units when known, monotonic sequence, job ID, and revision/generation. Unknown-duration operations report stages and activity without inventing percentages.

## 9. Verification strategy

Use a deterministic virtual scheduler and fake evaluators to generate DAGs, durations, failures, cancellation points, memory costs, stale generations, duplicate keys, and worker deaths.

Required properties:

- a node never observes inputs from mixed revisions/configurations;
- unrelated subgraphs do not execute after a local edit;
- execution order respects dependencies;
- stale completions never replace current projections;
- equal keys execute at most once concurrently;
- bounded queues never exceed configured resources;
- cancellation cannot publish a partial result;
- schedules converge to the same successful artifacts independent of task interleaving.

The same scheduler conformance suite runs against deterministic single-thread, production thread-pool, and process-dispatch executors.

## 10. Performance budgets

- Command commit and invalidation planning for the MVP reference part: p95 under 10 ms on recommended hardware.
- Queue-to-start for visible cached work under nominal load: p95 under 16 ms.
- Superseded preview cancellation acknowledgement: p95 under 50 ms outside an identified uninterruptible kernel call.
- Incremental invalidation visits only the reverse-reachable affected graph plus bounded index overhead.

Exact fixtures and hardware are defined in the performance plan.

## 11. Open decisions

- **EVAL-OPEN-001:** Scheduler implementation: custom dependency scheduler over a standard executor versus a task-graph library.
- **EVAL-OPEN-002:** Cache storage tiers and eviction policy.
- **EVAL-OPEN-003:** Which OCCT calls are safe in parallel within one process for the pinned version.
- **EVAL-OPEN-004:** Whether mass properties are feature result metadata or a separately keyed derived evaluator.

## 12. Definition of done

The plan is implemented when all executors pass the generated scheduler suite, fault injection demonstrates stale/cancelled result safety, and incremental benchmarks show work proportional to affected graph size rather than total document size.
