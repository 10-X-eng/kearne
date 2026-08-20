# Implementation Sequence

- **Status:** Proposed
- **Requirement prefix:** `SEQ`
- **Depends on:** [MVP definition](../02-mvp-definition.md), [technical prototypes](02-technology-gates.md), [test strategy](03-test-strategy.md)
- **Unblocks:** repository implementation

## 1. Delivery rule

Kearne grows through permanent vertical slices. Each stage uses the final source/function, typed-record, command, revision, evaluation, diagnostic, and API paths. A stage may use fake adapters, but it cannot create a temporary alternative core.

### SEQ-001 — Gate before breadth

A stage's correctness, recovery, compatibility, and performance exit criteria pass before dependent feature breadth begins. A visible demo does not waive a failed foundation gate.

### SEQ-002 — Buildable increments

Every merged change leaves supported presets buildable and required suites passing. Schema changes include migrations and generated outputs in the same change.

### SEQ-003 — Traceable work

Each implementation change names requirement IDs, updates applicable descriptors/generators, and records verification. Requirements are closed by evidence, not percentage estimates.

### SEQ-004 — Workspace completion

Each production workspace ships as a complete vertical capability, not a menu of placeholders. Its owning plan defines the minimum workflow, real adapters, failure states, performance profile, and UI/headless/Python/AI conformance. Release packaging rejects development providers and visible actions without registered handlers.

## 2. Stage 0 — Production repository and UI foundation

Establish the root production build, module boundaries, design tokens, reusable controls, typed frontend ports, deterministic UI data provider, application lifecycle, semantic observation, and complete-session capture. The root build excludes `prototype/`.

Exit gate: a packaged desktop launches deterministically, exposes build and session identity, returns a correlated semantic snapshot and lossless complete-session image, and passes architecture checks proving no prototype dependency.

## 3. Stage 1 — Complete desktop interaction system

Implement the production UI before engineering backends:

- project/start, editor, settings, recovery, and operation-inspector surfaces;
- Model, Sketch, Assemble, Sheet Metal, Simulate, CAM, Drawing, and BOM workspaces;
- version controls in History and Agent/Jobs/Diagnostics in the right dock;
- command search, contextual command groups, structure/history, properties, jobs, diagnostics, AI proposals, and status;
- native model-function source, signature, input, output, recognition, and diagnostic surfaces;
- complete empty, loading, current, preview, pending, stale, failed, unavailable, read-only, and permission-denied states;
- responsive panel behavior, keyboard navigation, accessibility semantics, density/theme variants, and workspace restoration;
- typed requests and projections that define the backend ports without implementing engineering rules in QML.

Exit gate: every declared UI surface and state is reachable through public controls using deterministic data, responsive and accessibility suites pass, and the observation driver captures every visible Kearne surface without sleeps.

## 4. Stage 2 — Project core behind frontend ports

Implement in dependency order:

1. `kearne_base`: typed IDs, digests, finite quantities, diagnostics, results, cancellation/progress values.
2. IDL/schema toolchain and compatibility checks.
3. immutable content trees, function contracts/calls, typed records, and project snapshots;
4. source, invariant, dependency, and reference indexes;
5. command registry, source/function mutations, transactions, immutable revisions, workspace head;
6. in-memory persistence/artifact ports;
7. deterministic scheduler and fake workers;
8. headless command/query/replay adapter;
9. shared generators, reference models, and contract registration.

Exit gate: `MVP-A-001` passes in memory at PR and nightly scales. Architecture tests prove source inspection executes no project code and the core has no Qt, OCCT, or Python-runtime dependency.

## 5. Stage 3 — Durable headless project

Implement:

- selected project store, content trees, checkpoints, migrations, and artifacts;
- durable request idempotency and workspace heads;
- fault-injecting storage adapter;
- process supervisor and fake worker transport;
- CLI create/open/inspect/edit/replay;
- structured logs and operation inspector.

Exit gate: `MVP-A-002`, persistence fault matrix, protocol fuzz smoke, and migration round trips pass on Windows and Linux. Cache deletion preserves source and typed intent.

## 6. Stage 4 — First connected parametric slice

Implement:

- replace deterministic UI providers with real Engine port implementations without changing QML contracts;
- render projection and selected viewport backend;
- semantic picking for datum, profile, named output, and topology scope;
- selected sketch solver adapter and native build123d sketch source generation;
- pinned isolated Python/build123d/OCP worker;
- function contracts, calls, output validation, and extrude source generation/recognition;
- preview generations, last-known-good display, jobs/diagnostics UI.

Exit gate: the mounting plate is generated graphically as inspectable native source and can be edited directly without changing its evaluation path; the agent receives a complete lossless Kearne-session image and matching semantic snapshot without sleeps; function, solver, renderer, and observation suites pass; camera remains responsive during blocked or failed workers.

## 7. Stage 5 — Downstream topology and modeling tools

Implement one topology-critical chain before broad features:

```text
sketch() -> extrude() -> fillet() -> holes() -> pattern()
```

Then add revolve, boolean modes, chamfer, expressions, and target-body policies through the same source generator/recognizer and function evaluator.

Exit gate: topology edit matrix, generated function contracts, unrecognized-source evaluation, incremental evaluation properties, save/reopen/replay, and failure recovery pass. No function claims labeled topology without passing its publication suite.

## 8. Stage 6 — Interchange and automation

Implement:

- isolated STEP import and retained source artifact;
- STEP/STL atomic export;
- typed Python SDK for source/function and product commands;
- pinned Codex app-server client, Agent Bridge, query/command tools, preview, and approval;
- provenance views.

Exit gate: Gates D acceptance scenarios, parser/worker fault suites, source/function parity, and AI policy state machine pass.

## 9. Stage 7 — MVP hardening

- complete supported-platform installers and updates;
- run full migration, sanitizer, fuzz, fault, security, accessibility, and performance gates;
- document supported graphical-operation, source, topology, and import profiles and limitations;
- freeze first public format/API major versions;
- validate crash reporting and offline behavior;
- remove development bypasses and unsigned defaults from release builds.

Exit gate: all MVP requirements have linked evidence; no unresolved critical risk or `OPEN` decision affects persisted/public behavior.

## 10. Post-MVP order

The order minimizes core rewrites:

1. Materials/standard components, configurations, direct editing, surfacing, and rule-driven sheet metal.
2. Component references, assemblies, solver, LOD, BOM, motion, and interference.
3. Structural/modal simulation and validated result visualization.
4. CAM setup, core 3-axis strategies, removal simulation, and postprocessing.
5. Professional drawings, standards, revision/release workflow, branches, semantic merge, and AI alternatives.
6. Weldments, broader analysis/manufacturing profiles, and optional collaboration.

## 11. Work-package rule

A work package should deliver one observable vertical behavior and normally contain:

- schema/descriptor change;
- domain validation/normalization;
- port/adapter implementation;
- diagnostic mapping;
- generator/reference-model extension;
- conformance/property/scenario verification;
- performance or security measurement if affected;
- migration/documentation.

Splitting these into long-lived separate branches is discouraged because incomplete paths create duplicate temporary behavior.

## 12. Stop conditions

Pause feature expansion when:

- a persisted schema lacks migration strategy;
- topology guaranteed cells regress;
- generated state machines find unreproducible corruption;
- UI introduces synchronous kernel/persistence work;
- an adapter implements engineering validation independently;
- performance becomes proportional to total project size for a local edit;
- repeated worker input can crash the coordinator;
- a license blocks intended distribution;
- a desktop change cannot be launched, semantically inspected, and captured by the agent harness.

Resume after the owning plan/ADR and regression mechanism are corrected.

## 13. Definition of done

This sequence is accepted when every stage has funded owners, selected release gates, and no dependent implementation is scheduled before its architecture prototype and foundation contract.
