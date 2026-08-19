# Sketch

- **Status:** Proposed; solver and source-pattern gates required
- **Requirement prefix:** `SKH`
- **Depends on:** [project/function model](../foundations/01-document-model.md), [Python/build123d](05-python-and-build123d.md), [evaluation](../foundations/03-evaluation-and-jobs.md), [rendering](01-rendering-and-selection.md)
- **Unblocks:** solid and surface modeling

## 1. Purpose

Provide a responsive constrained sketch editor whose durable result is native Python/build123d source, not a parallel sketch document.

## 2. Source representation

Kearne-generated sketches are model functions returning a build123d `Sketch`. They use a small pinned `kearne.sketch` helper API for stable element IDs, declarative constraints, solver diagnostics, and structural editing. The helper emits build123d geometry and may be called from ordinary Python.

AI and users may instead write any native build123d sketch construction. Such functions evaluate normally. Rich constraint editing is available only when the source recognizer can recover the helper contract without changing semantics.

### SKH-001 — Source is canonical

Sketch geometry, constraints, dimensions, attachment, and stable IDs are expressed in the function source and contract. Parsed sketch records, solver rows, solved coordinates, profiles, and canvas state are derived.

### SKH-002 — Stable source IDs

Generated point, line, circle, arc, and constraint declarations carry collision-resistant IDs independent of source order, variable names, solver order, trimming, and draw order. Endpoints and centers use keys such as `start`, `end`, and `center`, never numeric offsets.

### SKH-003 — Structural edits only

The graphical editor transforms a recognized concrete syntax structure against an expected source digest. It preserves unrelated source or refuses. It does not maintain a hidden sketch entity table or rewrite arbitrary Python with regular expressions.

## 3. MVP geometry and constraints

MVP geometry:

- point, line, polyline;
- circle and circular arc;
- rectangle as ordinary lines and constraints;
- construction geometry.

MVP constraints:

- coincident, horizontal, vertical;
- parallel, perpendicular, tangent;
- concentric, equal, midpoint, fixed, collinear;
- distance, horizontal/vertical distance;
- radius, diameter, angle.

Higher-order curves and constraint types follow after the solver and source-transform contracts pass their gates.

## 4. Solver port

```text
SketchSolveInput
  source-digest-scoped recognized geometry and constraints
  typed parameter bindings
  prior solution seed
  drag target/temporary constraints
  numerical profile

SketchSolveResult
  coordinates by stable source ID/sub-element key
  status, degrees of freedom, and modes
  residuals
  conflicting/redundant constraint sets
  diagnostics
```

### SKH-004 — Solver independence

Canonical source exposes no solver implementation types. The helper contract maps to a selected solver through a port chosen by license, determinism, diagnostics, supported constraints, and latency evidence.

### SKH-005 — Independent validation

Kearne validates returned coordinates against normalized residuals and finite/range rules. Prior solutions may seed continuity but never prove satisfaction.

### SKH-006 — No silent weakening

The solver cannot drop, demote, or change a declared constraint. Suggested repairs are source transformations requiring confirmation.

### SKH-007 — Explicit attachment

Every sketch references a stable component origin plane, named construction-plane output, or planar topology reference plus orientation. GUI selection writes that reference into source/function state; no transient active plane becomes durable intent.

## 5. Health and interaction

Health reports solve status, remaining degrees of freedom, conflicts, redundancy, conditioning, invalid geometry, and closed-profile analysis separately. Conflict sets refer to stable source IDs and state whether minimality is exact or approximate.

Dragging adds an ephemeral target constraint to a generation-tagged solve. Release proposes one structural source edit or parameter change. If source or base revision changed, commit fails stale and must be regenerated.

Constraint inference produces ranked proposals. Accepted proposals become ordinary helper calls in source with provenance. DOF visualization consumes stable IDs and solver modes, not solver variable offsets.

## 6. Profiles and projections

Closed-profile extraction is a deterministic evaluator over the solved sketch. It publishes labels based on source element ancestry and reports gaps, intersections, duplicates, and ambiguous nesting.

Projected external geometry uses explicit named-output/topology references in the function contract or helper calls. It becomes stale or broken through normal dependency handling; Kearne never copies it into unrelated lines silently.

## 7. Verification

One generated suite covers helper-source generation, parse-without-execution, recognition, source-preserving transformations, stale-digest rejection, solver residuals, DOF, rigid transforms, unit equivalence, declaration reordering, contradiction detection, drag continuity, save/reload, and invalid inputs.

The suite also generates valid native build123d sketch functions outside the helper pattern and verifies they evaluate without being mislabeled graphically editable. A model-based editor composes add/delete/constrain/drag/refactor/undo/redo/branch/merge actions and shrinks failures by source construct and stable ID.

## 8. Performance budgets

- Recognized 100-entity sketch: preview solve p95 below 16 ms on recommended hardware.
- Structural edit and reparse of the same function: p95 below 16 ms, excluding solve/evaluation.
- A 1,000-entity benchmark remains cancellable and interactive; final target follows the solver gate.
- Conflict analysis returns initial actionable evidence within 250 ms for the reference profile.

## 9. Open decisions

- **SKH-OPEN-001:** Solver library and license.
- **SKH-OPEN-002:** `kearne.sketch` helper API and generated source shape.
- **SKH-OPEN-003:** Concrete-syntax parser/transformer and formatting guarantees.
- **SKH-OPEN-004:** Stable profile labels across topology-changing sketch edits.
- **SKH-OPEN-005:** Exact minimal-conflict guarantees and large-sketch target.

## 10. Definition of done

Sketch v1 is implemented when generated graphical edits produce inspectable native source, unrecognized native build123d sketches still evaluate, the selected solver passes generated conformance, long source/editor histories preserve stable IDs, and no canonical sketch state exists outside source and function contracts.
