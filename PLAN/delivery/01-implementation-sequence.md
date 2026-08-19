# Implementation Sequence

- **Status:** Proposed
- **Requirement prefix:** `SEQ`
- **Depends on:** [MVP definition](../02-mvp-definition.md), [technical spikes](02-technical-spikes.md), [test strategy](03-test-strategy.md)
- **Unblocks:** repository implementation

## 1. Delivery rule

Kearne grows through permanent vertical slices. Each stage uses the final document, command, revision, evaluation, diagnostic, and API paths. A stage may use fake adapters, but it cannot create a temporary alternative core.

### SEQ-001 — Gate before breadth

A stage's correctness, recovery, compatibility, and performance exit criteria pass before dependent feature breadth begins. A visible demo does not waive a failed foundation gate.

### SEQ-002 — Buildable increments

Every merged change leaves supported presets buildable and required suites passing. Schema changes include migrations and generated outputs in the same change.

### SEQ-003 — Traceable work

Each implementation change names requirement IDs, updates applicable descriptors/generators, and records verification. Requirements are closed by evidence, not percentage estimates.

## 2. Stage 0 — Retire architecture uncertainty

Run the spikes in [technical spikes](02-technical-spikes.md). Required decisions before persistent production code:

- schema/IDL and generation path;
- project storage and crash semantics;
- OCCT/OCP artifact compatibility and worker topology;
- Qt Quick viewport backend;
- sketch solver;
- numerical/modeling range;
- persistent topology v1 feasibility;
- Codex app-server protocol, packaging, and tool-bridge path;
- complete Kearne-session capture and semantic observation on target display stacks.

Output: accepted/rejected ADRs, benchmark data, small isolated prototypes, license inventory, and revised plans. Prototype code enters production only if it obeys accepted boundaries and has contract tests.

## 3. Stage 1 — Semantic core

Implement in dependency order:

1. `kearne_base`: typed IDs, digests, finite quantities, diagnostics, results, cancellation/progress values.
2. IDL/schema toolchain and compatibility checks.
3. immutable entity records and document snapshots;
4. invariant and reference indexes;
5. command registry, normalization, transactions, immutable revisions, workspace head;
6. in-memory persistence/artifact ports;
7. deterministic scheduler and fake workers;
8. headless command/query/replay adapter;
9. shared generators, reference models, and contract registration.

Exit gate: `MVP-A-001` passes in memory at PR scale and nightly scale. Architecture tests prove no Qt/OCCT dependency in the core.

## 4. Stage 2 — Durable headless project

Implement:

- selected project store, checkpoints, migrations, source artifacts;
- durable request idempotency and workspace heads;
- fault-injecting storage adapter;
- process supervisor and fake worker transport;
- CLI create/open/inspect/edit/replay;
- structured logs and operation inspector.

Exit gate: `MVP-A-002`, persistence fault matrix, protocol fuzz smoke, and migration round trips pass on Windows and Linux. Cache deletion preserves semantic state.

## 5. Stage 3 — First visual parametric slice

Implement:

- Qt/QML shell over fake then real Engine ports;
- application lifecycle and Desktop Observation driver before feature screens;
- visual tokens and the reusable component/state catalog;
- render projection and selected viewport backend;
- semantic picking for datum/sketch/body scope;
- sketch schema, selected solver adapter, profile extraction;
- OCCT geometry worker and extrude-new-body;
- preview generations, last-known-good display, jobs/diagnostics UI.

Exit gate: mounting-plate sketch/extrude workflow passes headless and GUI; the agent receives a complete lossless Kearne-session image and matching semantic snapshot without sleeps; solver, renderer, and observation conformance suites pass; camera remains responsive during blocked/failed workers.

## 6. Stage 4 — Downstream topology and feature system

Implement one topology-critical chain before broad features:

```text
sketch -> extrude -> fillet -> hole -> linear/circular pattern
```

Then add revolve, boolean modes, chamfer, expressions, and target-body policies through the same descriptor/evaluator harness.

Exit gate: topology edit matrix, generated feature contracts, incremental evaluation properties, save/reopen/replay, and failure recovery pass. No feature with incomplete topology publication enters the MVP registry.

## 7. Stage 5 — Interchange and automation

Implement:

- isolated STEP import and retained source artifact;
- STEP/STL atomic export;
- Python worker and typed SDK;
- pinned build123d procedural feature;
- pinned Codex app-server client, Agent Bridge, query/command tools, preview, and approval;
- provenance views.

Exit gate: Gates D acceptance scenarios, parser/worker fault suites, adapter semantic parity, and AI policy state machine pass.

## 8. Stage 6 — MVP hardening

- complete supported-platform installers and updates;
- run full migration, sanitizer, fuzz, fault, security, accessibility, and performance gates;
- document supported feature/topology/import profiles and limitations;
- freeze first public format/API major versions;
- validate crash reporting and offline behavior;
- remove development bypasses and unsigned defaults from release builds.

Exit gate: all MVP requirements have linked evidence; no unresolved critical risk or `OPEN` decision affects persisted/public behavior.

## 9. Post-MVP order

The order minimizes core rewrites:

1. Configurations, direct editing, surfacing, materials, and drawing foundations.
2. Component references, assemblies, solver, LOD, BOM, motion, interference.
3. User-facing versions, branches, semantic merge, AI alternatives.
4. Simulation and validated result visualization.
5. Professional drawings, standards, revision/release workflow.
6. Optional cloud synchronization and collaboration.

Sheet metal, weldments, manufacturing analysis, and CAM receive separate plans after their prerequisite body/topology/configuration contracts are proven.

## 10. Work-package rule

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

## 11. Stop conditions

Pause feature expansion when:

- a persisted schema lacks migration strategy;
- topology guaranteed cells regress;
- generated state machines find unreproducible corruption;
- UI introduces synchronous kernel/persistence work;
- an adapter implements engineering validation independently;
- performance becomes proportional to total document size for a local edit;
- repeated worker input can crash the coordinator;
- a license blocks intended distribution.
- a desktop change cannot be launched, semantically inspected, and captured by the agent harness.

Resume after the owning plan/ADR and regression mechanism are corrected.

## 12. Definition of done

This sequence is accepted when every stage has funded owners, selected release gates, and no dependent implementation is scheduled before its architecture spike and foundation contract.
