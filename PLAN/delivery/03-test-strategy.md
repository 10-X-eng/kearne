# Scalable Test and Assurance Strategy

- **Status:** Proposed
- **Requirement prefix:** `TST`
- **Depends on:** [System architecture](../01-system-architecture.md), [Engineering API](../foundations/08-engineering-api.md)
- **Unblocks:** every implementation stage and release

## 1. Objective

Maintain confidence as Kearne grows from a small core to a million-line system without growing a brittle test for every method, coordinate, BREP byte sequence, screenshot, or timing race.

Tests target stable contracts, invariants, state transitions, and mathematical relations. Fixed examples remain only where they represent a user-critical scenario, external compatibility fixture, or defect that generators cannot yet express.

## 2. Assurance architecture

Reusable test libraries are product infrastructure:

```text
kearne_testkit
  deterministic IDs/time/randomness
  schema-aware value generators and shrinkers
  source/function/project revision reference model
  virtual scheduler and clocks
  fake artifact store, workers, renderer, provider, filesystem
  port conformance suites
  command/scenario runner
  application lifecycle and Desktop Observation drivers
  semantic equivalence and geometry invariant matchers
  fault-injection controls
  benchmark model generators
```

### TST-001 — Test public behavior

Tests SHOULD enter through domain ports, Engineering API, command logs, or public adapter contracts. Direct tests of private class layout are limited to isolated algorithms whose contract cannot be observed efficiently otherwise.

### TST-002 — Registration implies conformance

Registering a command, record schema, function contract, graphical operation, worker role, format, plugin, solver, or backend automatically enrolls it in applicable suites. Registration fails if required generators, effects, migrations, or conformance metadata are missing.

### TST-003 — Scale by parameters

One test definition runs with PR, nightly, and release profiles that vary seed count, graph size, operation length, concurrency, fault rate, model scale, and platform matrix. Do not copy a test to create its large variant.

## 3. Verification methods

### Contract suites

A port publishes one behavioral suite run against all implementations: memory and SQLite stores, fake and process workers, null and GPU renderers, solver adapters, provider adapters, and format adapters. This prevents mocks from describing behavior that production does not share.

### Property-based tests

Schema-aware generators create valid and targeted-invalid IDs, quantities, expressions, source modules, functions, calls, records, commands, graphs, sketches, configurations, and protocols. Shrinkers retain required structure while reducing failures to a useful reproducer.

### Model-based state machines

A deliberately simple reference model interprets source and record commands, transactions, revisions, undo/redo, persistence, jobs, permissions, merge, and collaboration. The production system is compared after generated transitions and fault points.

### Metamorphic tests

When exact output is unavailable or inappropriate, verify relations:

- rigid transforms preserve mass/topology relations;
- unit-equivalent inputs produce equivalent geometry;
- extrude volume relates to profile area and distance;
- independent graph edits do not evaluate unrelated nodes;
- mesh refinement follows declared convergence behavior;
- serialization/reordering does not change declared outcome;
- branch merge combines independent changes regardless of storage order.

### Fuzzing

Continuously fuzz untrusted byte and message boundaries: project headers/chunks, IPC, schemas, STEP and other parsers, compressed artifacts, plugin packages, expressions, Python conversion, AI tool arguments, and repair/migration inputs. Corpus entries are deduplicated and promoted to generators or regressions when possible.

### Differential tests

Use an independent implementation or mathematically equivalent construction where it adds evidence: alternate solver/backend, analytic formula, import/export round trip, reference-model reducer, or distinct build123d construction. Agreement is tolerance- and capability-aware; shared-library behavior is not independent proof.

### Deterministic replay

Failures record seed, generator profile, command/event log, evaluator fingerprints, platform, and fault schedule. A replay tool reconstructs the case through the public API. Reproducers remain small data, not new bespoke test programs.

### Fault injection

Inject allocation/resource limits, disk/full/I/O boundaries, worker crash/hang, cancellation races, process restart, stale messages, device loss, schema skew, unavailable plugin, network partition, provider failure, and corrupted cache. Fault hooks are compiled/test-only ports, not scattered production conditionals.

## 4. Scenario format

A versioned scenario log contains commands, queries, fault controls, and semantic assertions:

```text
scenario schema/version
initial project builder/seed
actor/capability context
ordered actions
named semantic references
asserted invariants and diagnostics
adapter matrix
resource/performance profile
```

The runner supports in-process, CLI/replay, local IPC, Python, AI-tool, and selected GUI adapters. Assertions use IDs, dimensions, entity fields, diagnostic codes, shape invariants, topology evidence, and revision relationships—not localized text or widget coordinates.

### TST-004 — Adapter parity

An adapter claiming a command subset runs the same scenario semantics. Adapter-specific tests cover only translation, presentation, accessibility, and transport behavior.

## 5. Domain assurance

### Document and history

Generated state machines check source-tree and record immutability, function/reference/ownership invariants, atomic transactions, idempotency, undo/redo divergence, merge, save/reload, checkpoint, migration, and unknown-source/payload preservation.

### Geometry and topology

Function and graphical-operation descriptors provide valid/invalid domains, output/body rules, analytic/metamorphic relations, topology labels, and edit matrices. Suites also generate valid unrecognized algebra, builder, and mixed-mode source. Oracles check shape validity, finite bounds, mass/property relations, ancestry, and honest ambiguity. Exact BREP bytes are diagnostic only.

### Solvers and simulation

Generate equation/mechanism families with known rank/solutions and manufactured/analytic simulation families with convergence expectations. Validate residuals independently of backend status flags.

### Rendering and UI

Use a reference scene model, semantic picking, accessibility tree, controller state machine, and frame instrumentation. Pixel tests are a bounded smoke layer for design primitives and visual integration.

### AI

A scripted adversarial model drives tool/policy state machines. Live model evaluations are statistical versioned benchmarks; merge gates do not assert exact prose or one sampled tool trace.

### Security

Capability, parser, package, IPC, and prompt-injection generators run alongside targeted penetration tests and dependency scanning.

## 6. Suite profiles

### Per change

- compile/schema/architecture fitness;
- deterministic unit/contract suites;
- small property/state-machine seed budget;
- changed fuzz-target smoke corpus;
- headless reference scenarios;
- targeted performance smoke.

The preset records its time budget; dependency analysis selects affected suites.

### Nightly

- larger seeds and operation sequences;
- sanitizer builds and thread/concurrency schedules;
- persistent fuzzing budget;
- fault matrices;
- migration/compatibility fixtures;
- renderer/platform integration;
- performance distributions and leak/soak tests.

### Release

- full supported OS/compiler/GPU/package matrix;
- long histories, large models/assemblies, cache eviction, recovery;
- old-version API/SDK/project/plugin compatibility;
- security and license gates;
- signed installer update/rollback;
- manual exploratory and domain-expert review for novel CAD behavior.

### TST-005 — Time budgets are explicit

Suites declare expected duration/resource class. Slow tests are sharded by stable seed and profile. A test is not removed merely for being slow; its schedule changes while preserving release coverage.

## 7. Failure reproducibility and flake policy

### TST-006 — Controlled nondeterminism

Tests control time, IDs, random seeds, scheduler order, locale, units, provider responses, and external versions. Concurrency tests record schedules when possible.

### TST-007 — No automatic retry as success

A retry may gather evidence but does not turn an initial failure green. Quarantined tests require an owner, linked defect, retained signal, and expiry. Flake rate is a release metric.

### TST-008 — Shrink before fossilize

Generated failures are shrunk and the missing invariant/generator is repaired. Add a fixed regression only when it protects external bytes, a kernel edge the generator cannot encode, or a historically important workflow.

## 8. Coverage measures

Line coverage is diagnostic, not the quality target. Required dashboards include:

- requirement IDs with passing evidence;
- registered schemas/types versus conformance enrollment;
- state-machine transition and state coverage;
- generator domain/invalid-class coverage;
- topology edit-matrix coverage;
- supported API/version/platform matrix;
- mutation-testing score sampled on pure domain libraries;
- fuzz execution, corpus growth, and unique crash status;
- fault-point coverage;
- performance distributions and regressions;
- escaped-defect classification and missing assurance mechanism.

### TST-009 — No coverage gaming

Generated code, unreachable defensive branches, and third-party code are reported separately. A coverage percentage cannot waive missing behavior or acceptance criteria.

## 9. Test data policy

- Builders/generators are preferred over copied project fixtures.
- External format fixtures record license, origin, digest, expected capability profile, and privacy status.
- Large fixtures live in a versioned artifact store, not normal Git history.
- Golden data is schema-versioned and regenerated only through reviewed commands showing semantic change.
- Proprietary customer models never enter general CI without explicit sanitized rights.

## 10. Architecture for one million lines

### TST-010 — Tests follow contracts, not directory count

Adding a subsystem implementation should mostly add generators and adapter-specific cases to existing contract suites. Test infrastructure remains in owned libraries with compatibility rules; teams do not fork local copies.

### TST-011 — Hierarchical composition

Small value generators compose into source modules, functions, calls, records, snapshots, command sequences, assemblies, and collaborative histories. Shrinkers follow the same hierarchy. This keeps failures tractable as system size grows.

### TST-012 — Reference models stay simpler

Reference models implement only semantic rules needed for comparison, using simple independent data structures. They MUST NOT reuse production reducers in ways that make both share the same defect.

### TST-013 — Assurance ownership is part of extension

A public extension point is incomplete without its conformance kit. New implementations prove substitutability before product-specific scenarios.

### TST-014 — Manual exploration is not regression infrastructure

Manual testing is reserved for exploration, domain judgment, and visual review. A discovered failure is captured as a replayable seed, minimized scenario, generator class, state transition, contract, or external fixture. Teams MUST NOT maintain hand-rewritten step lists as the primary regression suite when the same behavior can be generated or expressed through a stable public contract.

### TST-015 — Change the contract, not every test

When behavior changes intentionally, update the owning schema, descriptor, reference model, generator, or contract once and regenerate enrolled cases. A change that makes unrelated tests copy setup or expected values exposes a missing testkit abstraction and blocks expansion until corrected.

### TST-016 — Desktop work produces observable evidence

Every desktop scenario uses the Observation API for launch, semantic actions, event-based readiness, complete Kearne-session capture, and shutdown. It records the matching semantic snapshot and image metadata. Screenshots support visual review but do not replace command, state, accessibility, or geometry assertions.

## 11. Prohibited patterns

- Sleeping to wait for asynchronous completion; use virtual clocks/events/deadlines.
- Requiring test execution order or shared mutable project state.
- Asserting localized error prose instead of diagnostic code/parameters.
- Mocking a third-party result and claiming adapter correctness without integration/conformance evidence.
- One screenshot per UI state or exact BREP bytes per function case.
- Exposing private methods solely for tests when the public invariant is testable.
- Broad “end-to-end” tests with no observable intermediate contract or fault localization.
- Copying a contract suite into each adapter.
- Increasing tolerances until intermittent numerical failures disappear.
- Rewriting many fixed tests after a schema or workflow change instead of repairing the shared model/generator.
- Claiming desktop completion from QML/source inspection without a returned full-session capture.

## 12. Definition of done

The assurance foundation is implemented when a sample source/function contract, record schema, command, persistence backend, worker transport, and UI/Python adapter each enroll through registries and shared suites; generated failures replay and shrink; suite profiles run in CI; and quality reporting uses requirement, contract, and state coverage rather than test count.
