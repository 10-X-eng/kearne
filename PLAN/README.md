# Kearne Engineering Plan

- **Product:** Kearne
- **Purpose:** Convert [`SPEC.md`](../SPEC.md) into implementable, testable engineering contracts.
- **Plan status:** Proposed baseline

## 1. Authority and intent

The repository uses four kinds of engineering records:

1. `SPEC.md` defines the product vision and long-term capability envelope.
2. `PLAN/` defines observable behavior, subsystem boundaries, invariants, and acceptance criteria.
3. `PLAN/adr/` records architectural decisions and their rationale.
4. Source code and executable schemas implement the accepted plans.

When they disagree, an accepted ADR overrides an older plan, and an accepted plan overrides an illustrative example in `SPEC.md`. Product scope changes must update `SPEC.md`; implementation discoveries must update the relevant plan or ADR before they silently become architecture.

[`AGENTS.md`](../AGENTS.md) applies these records to coding work; it does not override them.

Plans are not task journals. A plan describes what remains true after the implementation has changed many times.

## 2. Product naming

Use these names unless superseded by an ADR:

| Surface | Name |
|---|---|
| Product | Kearne |
| Executable and CLI | `kearne` |
| C++ namespace | `kearne` |
| Project extension | `.kearne` |
| Environment-variable prefix | `KEARNE_` |
| Internal library prefix | `kearne_` |

Do not abbreviate Kearne to `cadx` in new interfaces. File-format identity and MIME registration will be finalized with the persistence plan.

## 3. Plan map

### Product and architecture

- [Product definition](00-product-definition.md)
- [System architecture](01-system-architecture.md)
- [MVP definition](02-mvp-definition.md)
- [Shared plan template](TEMPLATE.md)
- [Glossary](GLOSSARY.md)

### Foundations

- [Project and function model](foundations/01-document-model.md)
- [Commands, transactions, and revisions](foundations/02-commands-transactions-revisions.md)
- [Dependency evaluation and jobs](foundations/03-evaluation-and-jobs.md)
- [Persistent topology](foundations/04-persistent-topology.md)
- [Units, expressions, and numerics](foundations/05-units-expressions-numerics.md)
- [Persistence and recovery](foundations/06-persistence-and-recovery.md)
- [Processes and IPC](foundations/07-processes-and-ipc.md)
- [Engineering API and schemas](foundations/08-engineering-api.md)

### Product capabilities

- [Rendering and selection](capabilities/01-rendering-and-selection.md)
- [Sketch](capabilities/02-sketch.md)
- [Sketch constraint inventory](capabilities/02-sketch-constraint-inventory.md)
- [Solid modeling](capabilities/03-solid-modeling.md)
- [Import and export](capabilities/04-import-export.md)
- [Python and build123d](capabilities/05-python-and-build123d.md)
- [AI system](capabilities/06-ai-system.md)
- [Plugins](capabilities/07-plugins.md)
- [Assemblies](capabilities/08-assemblies.md)
- [Versioning, branching, and merge](capabilities/09-versioning-and-merge.md)
- [Simulation](capabilities/10-simulation.md)
- [Drawings and release](capabilities/11-drawings-and-release.md)
- [Collaboration](capabilities/12-collaboration.md)
- [Configurations](capabilities/13-configurations.md)
- [Direct and surface modeling](capabilities/14-direct-and-surface-modeling.md)
- [Qt/QML application shell](capabilities/15-qt-qml-application-shell.md)
- [Visual design system](capabilities/16-visual-design-system.md)
- [Codex app-server harness](capabilities/17-codex-app-server-harness.md)
- [Agent-observable desktop](capabilities/18-agent-observable-desktop.md)
- [Materials and standard components](capabilities/19-materials-and-standard-components.md)
- [Sheet metal](capabilities/20-sheet-metal.md)
- [CAM](capabilities/21-cam.md)
- [Bill of materials](capabilities/22-bill-of-materials.md)

### Delivery and assurance

- [Implementation sequence](delivery/01-implementation-sequence.md)
- [Technical prototypes](delivery/02-technology-gates.md)
- [Scalable test strategy](delivery/03-test-strategy.md)
- [Performance program](delivery/04-performance.md)
- [Build, packaging, and release](delivery/05-build-packaging-release.md)
- [Security and threat model](delivery/06-security-threat-model.md)
- [Risk register](delivery/07-risk-register.md)
- [Executable prototype evidence](../prototype/README.md)
- [Architecture decisions](adr/README.md)

## 4. Requirement identifiers

Every normative requirement has a stable identifier:

```text
AREA-NNN
```

Prefixes are defined in the owning plan, for example `DOC`, `CMD`, `EVAL`, `TOP`, `TST`, and `AI`. Identifiers are never reused. Removed requirements remain recorded as withdrawn.

The terms have these meanings:

- **MUST / MUST NOT:** required for the stated release gate.
- **SHOULD / SHOULD NOT:** expected unless an ADR records a justified exception.
- **MAY:** permitted but not required.
- **OPEN:** implementation must not guess; resolve through an ADR or plan update.

## 4.1 Product-goal coverage

| Product goal | Owning plans |
|---|---|
| Parametric CAD, direct modeling, surfacing | [Sketch](capabilities/02-sketch.md), [solid modeling](capabilities/03-solid-modeling.md), [direct/surface modeling](capabilities/14-direct-and-surface-modeling.md) |
| Assemblies, motion, interference | [Assemblies](capabilities/08-assemblies.md) |
| FEA, modal, thermal, optimization | [Simulation](capabilities/10-simulation.md), [AI](capabilities/06-ai-system.md) |
| AI inspection, modification, generation, simulation, iteration | [AI](capabilities/06-ai-system.md), [Codex harness](capabilities/17-codex-app-server-harness.md) |
| build123d and Python | [Python/build123d](capabilities/05-python-and-build123d.md) |
| Native Qt/QML desktop UI and viewport | [Application shell](capabilities/15-qt-qml-application-shell.md), [visual design](capabilities/16-visual-design-system.md), [rendering](capabilities/01-rendering-and-selection.md) |
| Agent-visible desktop and full Kearne-session capture | [Agent-observable desktop](capabilities/18-agent-observable-desktop.md), [Codex harness](capabilities/17-codex-app-server-harness.md) |
| Asynchronous geometry, meshing, simulation, AI, and imports | [Evaluation/jobs](foundations/03-evaluation-and-jobs.md), [processes/IPC](foundations/07-processes-and-ipc.md) |
| History, branches, merges, alternatives | [Commands/revisions](foundations/02-commands-transactions-revisions.md), [versioning/merge](capabilities/09-versioning-and-merge.md) |
| Configurations and variants | [Configurations](capabilities/13-configurations.md) |
| Drawings, dimensions, GD&T | [Drawings/release](capabilities/11-drawings-and-release.md) |
| STEP, STL, DXF, 3MF, and later formats | [Import/export](capabilities/04-import-export.md) |
| Plugins, Python SDK, headless/API access | [Plugins](capabilities/07-plugins.md), [Engineering API](foundations/08-engineering-api.md) |
| Large assemblies, instancing, LOD, caching, background loading | [Assemblies](capabilities/08-assemblies.md), [rendering](capabilities/01-rendering-and-selection.md), [performance](delivery/04-performance.md) |
| One source/function and command model for all actors | [Project/function model](foundations/01-document-model.md), [Commands/revisions](foundations/02-commands-transactions-revisions.md), [Engineering API](foundations/08-engineering-api.md) |
| Materials and standard fasteners | [Materials/standard components](capabilities/19-materials-and-standard-components.md), [assemblies](capabilities/08-assemblies.md) |
| Rule-driven sheet metal and flat patterns | [Sheet metal](capabilities/20-sheet-metal.md) |
| Toolpaths, removal simulation, and NC export | [CAM](capabilities/21-cam.md) |
| Structured, associative BOMs | [Bill of materials](capabilities/22-bill-of-materials.md) |

## 5. Definition of an implementable plan

A subsystem plan is implementable only when it defines:

- scope and non-goals;
- domain vocabulary and ownership;
- invariants and state transitions;
- public contracts and versioning rules;
- process/thread ownership;
- failure and cancellation behavior;
- persistence and migration behavior;
- security boundaries;
- observable acceptance criteria;
- reusable conformance tests;
- performance budgets;
- unresolved decisions.

A feature list without these properties is a roadmap, not an implementation plan.

## 6. Reuse and code-size policy

Kearne optimizes for **semantic density and one implementation per rule**, not a superficially low line count.

### PLAN-001 — One domain path

GUI, CLI, Python, plugins, AI, replay, and tests MUST invoke the same source/function and typed command/query boundary. Adapters may translate types and presentation, but MUST NOT create a second geometry authority or reimplement engineering rules.

### PLAN-002 — Schema-driven boundaries

Wire messages, command metadata, validation metadata, documentation, Python bindings, AI tool schemas, and conformance cases SHOULD be generated from shared schemas where this removes duplicated facts. Generated output is not reviewed as handwritten product logic.

### PLAN-003 — Reusable policy primitives

Cross-cutting policies—identity, units, diagnostics, cancellation, progress, permissions, revisions, artifact ownership, and schema migration—MUST live in small foundation libraries rather than be reinvented by each subsystem.

### PLAN-004 — No speculative mega-framework

An abstraction requires either:

- two real implementations with a third expected;
- one implementation plus an external compatibility boundary; or
- a cross-cutting invariant that must be enforced centrally.

Code is not generalized merely to reduce apparent duplication. Similar-looking code with different domain rules may remain separate.

### PLAN-005 — Composition over service proliferation

Subsystems SHOULD expose cohesive ports and pure domain operations. Avoid a mutable singleton service for every noun, inheritance trees for graphical operations, and UI-specific facades that duplicate the Engineering API.

### PLAN-006 — Dependencies are liabilities

Every third-party dependency requires an owner, version policy, license review, update strategy, security posture, and removal boundary. A dependency is selected for total maintained code and risk reduction, not only initial implementation speed.

## 7. Test policy

Kearne will not scale through a million lines of one-off example tests. The default order is:

1. Contract suites run against every implementation of a port.
2. Property-based tests generate valid and invalid domain values.
3. Model-based tests generate stateful command sequences.
4. Metamorphic tests verify relationships when no exact geometry oracle exists.
5. Fuzzers attack untrusted parsers and serialized boundaries.
6. Data-driven scenario logs exercise the same behavior through multiple adapters.
7. A small curated regression corpus preserves real bugs and difficult geometry.

Exact BREP bytes, screenshots, wall-clock sleeps, private implementation calls, and tests coupled to class layout are prohibited as primary correctness oracles. See [the test strategy](delivery/03-test-strategy.md).

## 8. AI implementation protocol

An implementation agent MUST:

1. Read this index, the glossary, the owning plan, its dependency plans, and applicable ADRs.
2. Identify requirement IDs being implemented.
3. Stop on an `OPEN` decision that materially changes persisted state or a public boundary.
4. Change geometry through the canonical native source/function path and other product state through typed engineering records.
5. Add or extend reusable test generators and conformance suites before adding isolated examples.
6. Launch, observe, and capture the complete Kearne session before claiming desktop work is visually complete.
7. Report verification against requirement IDs and acceptance scenarios.
8. Avoid introducing a second representation of an existing domain fact.

An agent MUST NOT mark work complete merely because it compiles or produces visually plausible geometry.

## 9. Plan lifecycle

Each plan has one status:

```text
Draft -> Proposed -> Accepted -> Implemented
                    \-> Superseded
```

- **Draft:** incomplete and unsafe to implement independently.
- **Proposed:** coherent, but contains explicitly listed decisions requiring review.
- **Accepted:** approved contract; implementation may proceed.
- **Implemented:** acceptance suite passes on supported platforms.
- **Superseded:** retained for history and linked to its replacement.

The current documents are a proposed baseline. High-risk decisions identified by the prototype plan must be validated before their plans become accepted.
