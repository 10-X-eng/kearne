# Sketcher

- **Status:** Proposed; solver spike required
- **Requirement prefix:** `SKH`
- **Depends on:** [Document model](../foundations/01-document-model.md), [evaluation](../foundations/03-evaluation-and-jobs.md), [units](../foundations/05-units-expressions-numerics.md), [rendering](01-rendering-and-selection.md)
- **Unblocks:** solid and surface features

## 1. Purpose

Provide an editable two-dimensional constraint system with stable entity identity, transparent degrees of freedom, responsive manipulation, and solver-independent semantic storage.

## 2. Canonical sketch model

```text
Sketch
  attachment: DatumPlaneRef or supported planar TopologyRef
  local frame
  geometry: map<SketchEntityId, SketchGeometry>
  constraints: map<ConstraintId, SketchConstraint>
  display/order metadata
  solve policy ID
```

Geometry records contain dimensional parameters or references to document parameters. Solver coordinates and equation rows are derived.

### SKH-001 — Semantic sketch entities

Point, line, circle, arc, and later curve entities have stable IDs independent of array order, solver variable order, trimming, and draw order.

### SKH-002 — Declarative constraints

Constraints store typed semantic references, dimensional values/expressions, driving/reference mode, and schema version. Solver-specific equation indices and internal handles are not persisted.

### SKH-003 — Stable sub-elements

Entity endpoints, centers, axes, and parameters use schema-defined sub-element keys such as `start`, `end`, and `center`; never numeric implementation offsets.

## 3. MVP geometry and constraints

MVP geometry:

- point, line, polyline;
- circle and circular arc;
- rectangle command producing ordinary lines/constraints;
- construction flag.

MVP constraints:

- coincident, horizontal, vertical;
- parallel, perpendicular, tangent;
- concentric, equal, midpoint, fixed, collinear;
- distance, horizontal/vertical distance;
- radius, diameter, angle.

Higher-order splines, ellipses, slots, projected curves, symmetry, and automatic image tracing follow only after the solver contract is stable. Commands such as rectangle and slot are reusable command compositions, not new solver primitives unless required mathematically.

## 4. Solver port

```text
SketchSolveInput
  normalized geometry and constraint equations
  prior solution seed
  drag target/temporary constraints
  numerical profile
  solve mode

SketchSolveResult
  coordinates/parameters by semantic key
  status
  degrees of freedom and modes
  residuals
  conflicting/redundant constraint sets
  diagnostics
```

### SKH-004 — Solver independence

Canonical sketch records do not expose PlaneGCS, SolveSpace, or another solver's types. Selection requires a license, determinism, diagnostic, supported-constraint, and interactive-latency spike.

### SKH-005 — Prior solution continuity

Interactive solving may use the prior valid solution as a seed to preserve local continuity. The seed is tagged with input digest and is never accepted as proof of constraint satisfaction.

### SKH-006 — Residual validation

Kearne independently validates returned coordinates against normalized constraint residuals and finiteness/range rules before publishing a solved profile.

## 5. Constraint health

Sketch health is not one enum. The result reports:

- solve success/failure;
- remaining degrees of freedom;
- conflicting constraint candidates;
- redundant constraints;
- rank/conditioning warnings;
- invalid geometry such as zero-length entities;
- closed-profile analysis separately from constraint status.

### SKH-007 — Conflict explanation

When practical, conflict diagnosis returns a small conflicting set expressed in constraint IDs. If a mathematically minimal set is too expensive, the result is explicitly labeled an approximate irreducible set.

### SKH-008 — No silent weakening

The solver MUST NOT drop, demote, or modify a user constraint to find a solution. Suggested repairs are typed commands requiring confirmation.

## 6. Interactive editing

Dragging creates a temporary target constraint in an ephemeral solve request. It does not persist coordinate updates per frame.

### SKH-009 — Preview/commit separation

During drag the UI displays generation-tagged solved coordinates. Release submits one semantic command reflecting the intended move or parameter change. If the base revision changed, commit revalidates or reports a conflict.

### SKH-010 — Explicit auto-constraints

Inference produces ranked constraint proposals. Confirmed proposals become normal constraints with provenance. MVP preference-controlled inference may accept high-confidence proposals during creation, but must provide immediate visible feedback and ordinary undo.

### SKH-011 — DOF visualization

The solver projection exposes movable entities and representative remaining modes without encoding solver variable indices into UI state.

## 7. Profiles and projected geometry

Closed-wire/profile extraction is a separate deterministic evaluator over solved sketch geometry. It publishes stable profile-loop names based on entity ancestry and reports self-intersections, gaps, duplicates, and ambiguous nesting.

Projected external geometry stores semantic source references and projection policy. It becomes stale/broken through normal dependency and topology handling; it is not copied into unrelated unconstrained lines silently.

## 8. Verification strategy

The solver port has a reusable conformance suite with generated equation systems. It checks:

- solution residuals and declared degrees of freedom;
- rigid translation/rotation invariance for unconstrained global frames;
- unit-equivalent inputs;
- constraint insertion-order independence within tolerance;
- serialization and entity-order independence;
- contradiction detection and no silent constraint dropping;
- continuous drag behavior under bounded perturbations;
- invalid/non-finite input rejection.

A model-based sketch editor generates entity/constraint addition, deletion, parameter edits, drag previews, undo/redo, save/reload, and solver switching where supported. Curated cases retain only solver defects not captured by generators.

## 9. Performance budgets

- Typical MVP sketch of 100 entities/constraints: preview solve p95 below 16 ms.
- 1,000-entity benchmark: explicit solve/cancellation remains responsive; target set after solver spike.
- Conflict analysis may be asynchronous but returns initial actionable diagnostics within 250 ms for the reference conflict fixture.

## 10. Open decisions

- **SKH-OPEN-001:** Solver library and license.
- **SKH-OPEN-002:** Parameterization and continuity representation for splines/ellipses.
- **SKH-OPEN-003:** Exact guarantee for minimal conflicting sets.
- **SKH-OPEN-004:** Stable loop naming when topology of a sketch profile changes.
- **SKH-OPEN-005:** Whether solved coordinates are cached artifacts or compact result metadata.

## 11. Definition of done

Sketcher v1 is implemented when the selected solver passes the generated port suite, the editor state machine survives long command/reload sequences, no solver-specific state is persisted, and the reference sketches meet interaction and diagnostic budgets.
