# Solid Modeling

- **Status:** Proposed
- **Requirement prefix:** `MOD`
- **Depends on:** [Python/build123d](05-python-and-build123d.md), [persistent topology](../foundations/04-persistent-topology.md), [sketch](02-sketch.md), [evaluation](../foundations/03-evaluation-and-jobs.md)
- **Unblocks:** MVP, assemblies, drawings, simulation

## 1. Purpose

Define how GUI-authored and AI-authored native build123d functions produce editable solid geometry through one evaluation path.

## 2. Operation descriptors

Kearne may register descriptors for operations it can generate and structurally edit:

```text
stable operation ID and version
typed parameters and defaults
allowed inputs and named outputs
source generator and recognizer
structural source transformer
topology-label policy
diagnostic mapper
edit-support matrix
test-domain generator
```

A descriptor describes Kearne's tooling capability. It is not a persisted geometry node and does not limit valid build123d code.

### MOD-001 — One evaluation contract

Generated, hand-written, and AI-written functions use the same source/function evaluator. A recognized operation MUST NOT call a private geometry path.

### MOD-002 — Explicit body scope

Generated functions declare which named outputs they create or consume. “Use the active body” is UI command construction, not persisted meaning.

### MOD-003 — Validation layers

Invalid contracts, references, or dimensional bindings reject a command. Syntax and kernel feasibility errors remain committed source revisions with structured diagnostics and no newly published output.

## 3. Initial generated patterns

Kearne supplies compact source generators and specialized editors for:

- construction planes by offset, midplane, angle, three points, and tangent reference;
- sketch/profile construction;
- extrude and revolve;
- union, subtraction, and intersection;
- constant fillet and equal-distance chamfer;
- simple and standards-backed clearance holes;
- linear and circular patterns.

Generators emit ordinary native build123d. Users and AI may replace or refactor that source without conversion.

### MOD-004 — Recognition is source-digest scoped

Recognition records the source digest, matched structure, editable fields, confidence, and unsupported constructs. A stale or partial match cannot enable a destructive specialized edit.

### MOD-005 — Refusal preserves source

If a structural edit cannot preserve semantics, Kearne refuses it and opens the function source or offers an explicit generated alternative. It never rewrites with regular expressions or substitutes BREP.

### MOD-006 — Versioned support profiles

Graphical support is declared by operation-descriptor version and cumulative profile. A release advertises only rows whose complete descriptor passes the definition of done.

| Profile | Required operation families |
|---|---|
| `MODEL-MVP-B-1` | origin datums; construction planes; sketch/profile; extrude in new/add/subtract modes |
| `MODEL-MVP-C-1` | revolve; union/subtract/intersect; constant fillet; equal-distance chamfer; simple and standards-backed clearance holes; linear/circular patterns |
| `MODEL-PRO-1` | construction points/axes/frames; typed primitives; sweep; loft; helix; shell; draft; rib/web; split body/face; mirror; path pattern; scale/transform; derived body; counterbore/countersink/tapped/tapered holes |

Direct face edits and surface bodies remain owned by the [direct/surface plan](14-direct-and-surface-modeling.md). A row missing its source editor, topology policy, diagnostic mapper, generated domain, or headless parity is unavailable, not partially supported.

### MOD-007 — Extent and target semantics

Material-creating/removing descriptors declare result mode (`NewBody`, `Add`, `Subtract`, or `Intersect`) and explicit targets. Extrude/cut declare direction and termination; revolve declares axis and angular extent; sweep declares path and orientation; loft declares ordered sections and continuity. `MODEL-MVP-B-1` requires blind one-sided and symmetric extrude/cut extents. `MODEL-PRO-1` adds two-sided, through-all, up-to-face/body, offset-from-reference, and supported thin-feature extents. Reversal and offsets are typed inputs; the evaluator never chooses a target or side from mutable UI state.

### MOD-008 — Engineering hole contract

Hole descriptors distinguish simple, counterbore, countersink, tapped, clearance, and tapered forms. They record axis, start reference, termination, diameter/depth, point and countersink angles where applicable, thread/fit standard identity and edition, tolerance class, hand, and thread representation. Unsupported standard rows or incompatible geometry refuse with allowed alternatives; they are never rounded to a nearby catalog value.

## 4. Evaluation and diagnostics

The worker validates finite inputs, output types, dimensionality, body count, shape health, bounds, and declared topology before publication. Safe normalization is part of the evaluator fingerprint. Healing that changes intent, topology, or tolerance beyond policy requires explicit source or parameter change.

Kernel exceptions map to stable diagnostics with function, source location when available, input bindings, and affected output. Cancellation or worker death cannot publish partial output or block the UI.

## 5. History and ordering

Model history is a projection of function calls, named-output bindings, source recognition, and component bindings. Reordering changes explicit dependencies or source, not a hidden feature vector. Suppression is a typed call/binding state with declared pass-through behavior.

## 6. Imported and direct geometry

Imported bodies remain retained artifacts with provenance. Native functions may consume approved imported geometry handles. Direct operations are build123d functions over explicit topology references; they never overwrite imported bytes or an upstream artifact in place.

## 7. Verification

Operation descriptors join one generated suite covering source generation, parse-without-execution, recognition, source-preserving edits, stale-digest rejection, dimensions, output contracts, topology publication, transformations, cancellation, save/reload, and adapter parity.

Geometry oracles combine shape validity, analytic relations, containment/intersection, mass properties, topology ancestry, differential constructions where independent, and a bounded kernel-regression corpus. BREP bytes and a growing list of static part files are diagnostic only.

The suite also generates unrecognized but valid algebra, builder, and mixed-mode functions to prove that evaluation does not depend on recognition.

Each support-profile row supplies valid/invalid parameter generators and relations for every termination and result mode. Hole domains include metric/inch boundaries, through/blind termination, fit incompatibility, thread representation changes, and missing catalog versions.

## 8. Performance and cancellation

### MOD-009 — Interactive modeling budget

Descriptor validation, source transformation, and invalidation planning contain no kernel work and meet the `MODEL-EDIT-100` budget in the performance plan. Preview jobs are generation-tagged; superseding or cancelling one is acknowledged within the shared interactive cancellation budget. Kernel calls run under declared worker time/memory limits, and an uninterruptible call is terminated through the worker supervisor without blocking input or publishing partial geometry.

Evaluation, topology publication, and artifact memory scale with the affected function subgraph. A local edit cannot recompute unrelated functions or retain an unbounded sequence of preview BREPs.

## 9. Acceptance scenarios

- A generated mounting plate is edited graphically, refactored by AI, then remains source- and parameter-editable even if specialized recognition is lost.
- A revolved shaft retains labeled cylindrical and planar selections across supported edits.
- A `MODEL-PRO-1` housing combines loft/sweep, shell, draft, ribs, and path patterns; an infeasible shell preserves the prior accepted body and source-linked failure.
- Changing a standard clearance hole to a tapped/countersunk form resolves one exact fit/thread row and updates semantic thread, drawing, and BOM data without requiring modeled threads.
- Boolean movement transitions success → no intersection → success without losing source.
- An oversized fillet fails with source-linked diagnostics and recovers after source or parameter repair.
- Pattern count changes preserve declared unaffected member labels.

## 10. Open decisions

- **MOD-OPEN-001:** Source shape generated for initial operation functions.
- **MOD-OPEN-002:** Structural recognizer and concrete-syntax technology.
- **MOD-OPEN-003:** Multi-body and variable-output contract.
- **MOD-OPEN-004:** OCCT boolean and tolerance policy.

## 11. Definition of done

An operation is supported graphically only when its generator, recognizer, structural edits, output/topology contract, generated domains, diagnostics, and headless documentation pass the shared suite. Any valid declared build123d function remains evaluable without such support.
