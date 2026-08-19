# Kearne System Architecture

- **Status:** Proposed
- **Requirement prefix:** `ARCH`
- **Depends on:** [Product definition](00-product-definition.md), [glossary](GLOSSARY.md)
- **Unblocks:** all foundation and capability plans

## 1. Architectural objective

Kearne separates durable engineering meaning from computation, presentation, and integration. The semantic core remains usable headlessly and testable without Qt, OCCT, Python, a GPU, or an AI provider.

```text
Adapters: QML | CLI | Python | Codex | Plugins | Replay
                         |
                 Engineering API
                         |
        Command validation / queries / permissions
                         |
          Immutable semantic document revisions
                         |
             Evaluation dependency scheduler
              /             |              \
       OCCT geometry   sketch solver   later solvers
              \             |              /
                 immutable artifacts
                         |
                  render projection
```

## 2. Layer boundaries

### ARCH-001 — Domain independence

Foundation domain libraries MUST NOT depend on Qt, OCCT, Python, GPU APIs, database implementations, network clients, or AI SDKs.

### ARCH-002 — Dependency direction

Dependencies point inward toward stable semantic contracts:

```text
kearne_base
    ^
kearne_schema  <- generated wire/schema support
    ^
kearne_document
    ^
kearne_engine  <- commands, revisions, queries, evaluation planning
    ^
ports: geometry | sketch | artifacts | persistence | render | automation
    ^
adapters: OCCT | solver | SQLite/container | Qt | Python | AI | CLI
```

An adapter may depend on its port and third-party technology. A port MUST NOT depend on an adapter.

### ARCH-003 — Canonical versus derived data

Every record MUST be classified as one of:

- canonical semantic state;
- immutable external/source artifact;
- derived index;
- reproducible evaluation artifact;
- ephemeral workspace or presentation state.

Only the first two categories are required to reproduce user intent. Derived and cache data MUST be safely discardable under the rules in the persistence plan.

### ARCH-004 — Published snapshots

Objects crossing threads or processes MUST be immutable values, immutable artifact handles, or explicitly synchronized operational messages. Live mutable OCCT, Qt, solver, or Python objects MUST NOT cross an ownership boundary.

### ARCH-005 — No presentation backdoors

QML and UI controllers MUST NOT call OCCT, mutate document entities, write the project database, or construct normalized mutations. They submit command requests and render projections.

### ARCH-006 — No automation backdoors

Python, AI, CLI, and plugins MUST use the same Engineering API command/query contracts as the GUI. Privileged maintenance APIs are separately named, permissioned, and excluded from ordinary plugins.

## 3. Small reusable foundations

The architecture standardizes a deliberately small set of cross-cutting types:

```text
TypedId<T>             stable domain identity
RevisionId             immutable document revision identity
Digest                 content/evaluator identity
Quantity<Dimension>    dimension-safe numerical value
Diagnostic             structured user/developer finding
Result<T, Diagnostic>  expected failure contract
CancellationToken      cooperative cancellation signal
ProgressEvent          monotonic progress report
ArtifactHandle         immutable bulk-data reference
CommandEnvelope        versioned intent plus actor/context
MutationBatch          normalized atomic semantic changes
DocumentSnapshot       immutable logical state
EvaluationKey          declared-input and evaluator fingerprint
TopologyRef            stable semantic subshape reference
```

Subsystems MUST reuse these types rather than create subtly different equivalents.

## 4. Domain operation model

### ARCH-007 — Validate, normalize, commit, evaluate

A persistent action follows one pipeline:

```text
Command request
  -> schema and permission validation
  -> domain precondition validation against snapshot R
  -> normalized MutationBatch
  -> atomic commit creating revision R+1
  -> dependency invalidation
  -> asynchronous evaluation against R+1
  -> immutable artifacts and diagnostics
  -> render/query projection publication
```

Geometry failure does not retroactively corrupt or partially commit semantic state. The feature remains present and failed unless the entire command was invalid before commit.

### ARCH-008 — Read consistency

Every query MUST identify the revision it observed. Multi-query workflows may request a read lease on one immutable snapshot without blocking new revisions.

### ARCH-009 — Idempotent boundaries

Externally retried command submissions MUST carry a unique request ID. Repeating a successfully committed request ID returns its original outcome rather than creating a second revision.

## 5. Process and thread model

The logical roles are:

- UI/main process: windowing, QML models, command orchestration, local project coordinator.
- Render thread/backend: GPU resources and frame submission.
- Core worker pool: pure lightweight work and orchestration that is safe in-process.
- Geometry workers: OCCT operations selected for isolation or concurrency control.
- Python workers: scripts and build123d under explicit capabilities.
- Solver workers: sketch, assembly, meshing, and simulation workloads whose isolation or resource policy requires a process.

### ARCH-010 — UI deadline

No potentially unbounded engineering, persistence, parsing, or IPC wait may run on the UI thread. UI calls into the Engineering API MUST either complete within the interactive budget from in-memory state or return an asynchronous operation handle.

### ARCH-011 — Operational state separation

Job queues, process IDs, open database handles, selections, hover state, and GPU resources are operational state and MUST NOT appear in canonical document schemas.

## 6. Error and diagnostic model

Expected failures return structured diagnostics rather than language exceptions across public boundaries. A diagnostic contains:

```text
code
severity
summary key and parameters
technical detail (optional)
affected semantic references
originating subsystem/evaluator
revision and operation IDs
repair actions (optional typed commands)
causal diagnostics
```

Stable error categories include validation, broken reference, ambiguity, evaluation failure, cancellation, timeout, resource exhaustion, unavailable dependency, incompatible schema, permission denial, and internal invariant violation.

Exceptions may terminate a transaction before commit or a worker process, but MUST be converted at the boundary and MUST NOT leak through wire protocols.

## 7. Extensibility

### ARCH-012 — Registry, not inheritance

Feature, command, import/export, solver, and analysis extensions register descriptors and port implementations. The core does not require a new subclass hierarchy per adapter. Native implementations may use static polymorphism or plain functions internally.

### ARCH-013 — Missing evaluators

Loading a project with an unavailable plugin or evaluator MUST preserve opaque canonical payloads and references. It may show retained artifacts as stale/read-only, but MUST NOT discard or rewrite unknown data.

### ARCH-014 — Pinned behavior

Derived artifacts record the evaluator fingerprint. Plugin version, OCCT version, numerical profile, and relevant external database versions participate in reproducibility and cache validity.

### ARCH-015 — Evidence-selected modernity

New technology is selected when supported versions and measurements improve correctness, performance, security, portability, or maintained code. Novelty, fashion, and compatibility with another CAD application's internals are not selection criteria. Material choices record replacement boundaries and upgrade policy.

### ARCH-016 — Separate observation plane

Application lifecycle, semantic UI inspection, input automation, and image capture use an Observation API outside the Engineering API. Observation may report or invoke public UI behavior but cannot mutate document state, call private QML logic, or become an engineering command path.

## 8. Architecture fitness tests

The build MUST continuously enforce:

- forbidden dependency edges and include boundaries;
- no Qt or OCCT symbols in document/schema libraries;
- no direct document mutation outside the transaction engine;
- serialization round-trip for every registered schema;
- every command is discoverable through the same registry used by AI/Python/UI metadata;
- every port implementation passes its shared conformance suite;
- worker messages contain no raw pointers or process-local handles;
- no blocking operation annotated as unbounded is callable from UI-thread code.
- every interactive QML control has stable semantic identity and observation metadata;
- Codex protocol types occur only in its adapter and generated compatibility layer.

These tests operate over dependency metadata, registries, generated schemas, and instrumented runtime behavior rather than enumerating individual source files manually.

## 9. Repository shape

Initial logical libraries should remain fewer and more cohesive than the service list in `SPEC.md`:

```text
/src/base
/src/schema
/src/document
/src/engine
/src/geometry
/src/sketch
/src/artifacts
/src/persistence
/src/render
/src/observation
/src/app
/src/adapters/{occt,qt,cli,codex}
/python
/schemas
/tests/{contract,property,scenario,fuzz,performance}
```

New top-level libraries require a distinct ownership or dependency boundary, not merely a new feature noun.

## 10. Open decisions

- **ARCH-OPEN-001:** Exact IDL and code-generation toolchain; validate Protobuf plus generated JSON Schema in the boundary spike.
- **ARCH-OPEN-002:** Persistent data-structure library versus immutable snapshots constructed with copy-on-write entity tables.
- **ARCH-OPEN-003:** Degree of initial geometry worker isolation given OCCT thread-safety and transfer cost.
- **ARCH-OPEN-004:** Whether the local project coordinator remains in the UI process or becomes a separate long-lived process after MVP.

## 11. Definition of done

This plan is accepted when dependency direction, snapshot/transaction semantics, cross-process ownership, schema approach, and exception/diagnostic policy are validated by ADRs and technical spikes.
