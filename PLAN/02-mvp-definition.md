# Kearne MVP Definition

- **Status:** Proposed
- **Requirement prefix:** `MVP`
- **Depends on:** [Product definition](00-product-definition.md), [system architecture](01-system-architecture.md)
- **Unblocks:** [Implementation sequence](delivery/01-implementation-sequence.md)

## 1. Meaning of MVP

The Kearne MVP is a usable local parametric-part alpha and a proof of the permanent architecture. It is not a broad but shallow imitation of an established CAD suite.

The original `SPEC.md` MVP feature list remains the target envelope. This plan imposes ordered release gates so hard foundation problems are proven before feature breadth creates migration pressure.

## 2. Permanent domain scope

The persisted model from its first version supports typed entities for:

- project and document metadata;
- component definitions;
- datum frames, planes, axes, and points;
- sketches and sketch entities;
- bodies and features;
- parameters and dimensional expressions;
- source and derived artifacts;
- provenance and structured diagnostics;
- immutable revisions and workspace heads.

The MVP UI may expose only one active component definition and a limited set of datum tools. The schema MUST NOT encode a “single part forever” shortcut.

## 3. Gate A — Architecture executable skeleton

### Required behavior

- Headless creation, mutation, query, save, reopen, and replay of an empty semantic document.
- Immutable revisions and atomic transactions.
- Stable typed IDs and schema versions.
- Command registry exposed through an in-process API and CLI/replay adapter.
- Background job scheduler with deterministic fake executor.
- Structured diagnostics and logging correlation IDs.
- Cross-platform build, test, and package smoke jobs.

### Acceptance

`MVP-A-001`: A generated sequence of at least 10,000 valid and invalid metadata/entity commands can be applied, undone/redone, saved/reloaded at generated checkpoints, and replayed to an equivalent semantic snapshot without an invariant violation.

`MVP-A-002`: Killing the application at injected persistence boundaries yields either the previous durable revision or the next complete durable revision, never a hybrid.

## 4. Gate B — First parametric solid vertical slice

### Sketch scope

- datum-plane attachment;
- point, line, polyline, circle, arc, rectangle, and construction geometry;
- coincident, horizontal, vertical, parallel, perpendicular, tangent, equal, fixed, distance, horizontal/vertical distance, radius/diameter, and angle constraints;
- under-, fully-, over-, conflicting-, and redundant-constraint diagnostics;
- direct manipulation with preview and commit separation.

### Modeling scope

- extrude as new body, additive, and subtractive operation;
- one-sided and symmetric distance extents;
- body, face, edge, and feature selection;
- shaded-with-edges viewport;
- incremental recomputation;
- last-known-good display when current evaluation fails.

### Acceptance

`MVP-B-001`: The reference mounting-plate workflow through sketch and extrusion passes through headless commands and GUI.

`MVP-B-002`: Randomly generated solvable sketches remain solver-valid after rigid transformation and unit-preserving serialization round trips.

`MVP-B-003`: Camera interaction remains within its frame budget during an intentionally blocked geometry job.

`MVP-B-004`: The observation driver launches the packaged desktop, completes the sketch/extrude workflow through public controls, awaits state without sleeps, and returns a lossless image containing every visible Kearne surface plus its correlated semantic snapshot.

## 5. Gate C — Downstream references and useful modeling

### Required features

- revolve;
- boolean union, subtract, and intersection;
- fillet and chamfer;
- engineering-aware simple and clearance hole subset;
- linear and circular feature patterns;
- parameter expressions and named variables;
- persistent topology v1 for documented edit classes.

### Acceptance

`MVP-C-001`: A downstream hole, pattern, and fillet retains or honestly reports its reference after each mutation in the topology edit matrix.

`MVP-C-002`: Failed fillet evaluation retains semantic parameters, last-known-good upstream geometry, and an actionable structured diagnostic.

`MVP-C-003`: A generated feature graph is recomputed in dependency order; unrelated subgraphs are not evaluated.

## 6. Gate D — Interchange and automation

### Required behavior

- STEP import as retained source artifact plus imported body;
- STEP export and STL export;
- isolated Python worker;
- pinned build123d environment and procedural feature;
- typed Python SDK for the supported command/query subset;
- AI read/query tools and the supported modeling commands;
- pinned Codex app-server harness and generated protocol conformance;
- preview/confirm for multi-command AI transactions;
- provenance for human, Python, replay, plugin, and AI actors.

### Acceptance

`MVP-D-001`: The reference part can be constructed through GUI, Python, and typed AI commands and results in semantically equivalent entities modulo allocated IDs and provenance.

`MVP-D-002`: Terminating a Python or geometry worker cannot terminate the main process or corrupt the project.

`MVP-D-003`: Imported source bytes remain recoverable even when the derived BREP cache is removed.

`MVP-D-004`: A Codex app-server turn receives the current Kearne-session capture, inspects and modifies the reference part only through granted Kearne tools, and verifies the committed revision through typed queries.

## 7. Explicitly excluded

- assemblies and joints;
- configurations beyond schema reservation;
- direct and surface modeling;
- sheet metal and weldments;
- simulation;
- drawings and release workflows;
- cloud synchronization and collaboration;
- arbitrary third-party native plugins;
- autonomous AI optimization;
- renderer capabilities whose only MVP use is large-assembly scale.

## 8. Quality gates

### MVP-001 — No temporary architecture

MVP stages MAY omit capabilities but MUST NOT introduce alternate document, command, identity, units, or persistence models intended to be replaced later.

### MVP-002 — Feature registration

Every supported native feature uses the same descriptor, validation, evaluation, diagnostics, serialization, and topology-publication contracts intended for later feature types.

### MVP-003 — Test scale

Each gate extends generators, state machines, and conformance suites. A feature is not complete if it can only be verified by a fixed hand-authored example.

### MVP-004 — No hidden synchronous fallback

GUI behavior MUST NOT invoke a synchronous kernel operation when background evaluation is unavailable or slow.

### MVP-005 — Migration from first persisted release

Every persisted schema version after the first distributable alpha includes a tested forward migration or an explicit incompatibility diagnostic. Test-only pre-alpha fixtures may be reset before the format freeze milestone.

### MVP-006 — No blind desktop completion

A desktop capability is incomplete until automated semantic assertions pass and the observation driver returns the complete Kearne-session image from the changed build. A screenshot alone is not a correctness oracle.

## 9. Definition of done

The MVP is complete when Gates A–D pass their scenario, property, fault-injection, performance, and supported-platform suites; documentation describes known modeling limitations; and no critical invariant in the risk register remains unmitigated.
