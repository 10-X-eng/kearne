# Direct and Surface Modeling

- **Status:** Proposed; post-MVP
- **Requirement prefix:** `DSM`
- **Depends on:** [Solid modeling](03-solid-modeling.md), [persistent topology](../foundations/04-persistent-topology.md), [import/export](04-import-export.md)
- **Unblocks:** imported-model editing, professional surfacing, later sheet metal

## 1. Purpose

Add source-defined direct edits and sheet/surface bodies while preserving imported bytes, topology evidence, and honest failure behavior.

## 2. Body kinds

The body schema distinguishes:

```text
SolidBody
SheetBody
WireBody where explicitly supported
Compound result as a result type, not an ambiguous solid
```

### DSM-001 — Dimensionality is explicit

Function inputs and named output slots declare permitted body kinds. A result that unexpectedly contains a shell, multiple solids, or a compound cannot publish under a single-solid contract.

### DSM-002 — Shared function infrastructure

Direct and surface functions use the same source transactions, contracts, evaluation keys, workers, topology publication, diagnostics, preview, persistence, and generated conformance harness as other model functions.

## 3. Direct editing

Initial direct-operation generators:

- move/rotate face;
- offset/press-pull face;
- delete face with healing;
- replace face;
- resize recognized cylindrical face group.

Each stores selected topology, transform/distance parameters, explicit propagation/healing policy, and target body.

### DSM-003 — Direct edits remain source

A direct operation creates or edits a native build123d function over explicit inputs. It MUST NOT overwrite an imported or evaluated BREP in place or erase upstream source.

### DSM-004 — Recognition is a proposal

Hole, fillet, pocket, cylindrical-group, and pattern recognition returns candidates with evidence. Accepting a candidate proposes an explicit source transformation and reference changes. Recognition never silently rewrites source.

### DSM-005 — Imported source remains recoverable

Direct editing an imported body consumes its normalized geometry artifact while retaining source bytes and import provenance. Reimport/update later creates a preview and topology reconciliation, not an implicit replacement.

## 4. Surface modeling

Planned source generators and graphical editors:

1. Surface extrude/revolve/sweep/loft, ruled and offset surface.
2. Trim, extend, split, stitch/unstitch, fill, and thicken.
3. Boundary surface and face blend with continuity controls.
4. Curvature, zebra, draft, continuity, radius, and deviation analysis.

### DSM-006 — Boundary intent

Surface-function contracts store ordered boundary/guide references, parameterization/orientation choices, continuity target (`G0`/`G1`/`G2`/supported higher level), and weights/tolerances. The evaluator does not infer an alternate ordering silently when the requested construction fails.

### DSM-007 — Stitching disclosure

Stitching records tolerance policy, gaps closed, topology changes, and whether a closed solid resulted. Tolerance expansion beyond policy requires explicit approval.

### DSM-008 — Continuity is measured

A successful kernel operation does not prove requested continuity. Kearne evaluates positional/tangent/curvature deviation using a declared sampling/analytic policy and publishes the measured result.

### DSM-009 — Versioned support profiles

Graphical support is declared by descriptor version and profile. `SURFACE-2` includes `SURFACE-1`; `SURFACE-ANALYSIS-1` requires `SURFACE-1`.

| Profile | Required operation families |
|---|---|
| `DIRECT-1` | move/rotate, offset/press-pull, delete, and replace face; resize cylindrical group; hole/fillet/pocket/pattern recognition proposals |
| `SURFACE-1` | surface extrude/revolve/ruled/offset; trim/extend/split; stitch/unstitch; fill; thicken |
| `SURFACE-2` | sweep, loft, boundary surface, and face blend with declared boundary order and continuity |
| `SURFACE-ANALYSIS-1` | curvature, zebra, draft, continuity, minimum-radius, and deviation analysis |

Every row declares supported body kinds, selection cardinality, propagation/healing policy, topology edit matrix, approximation profile, and refusal cases. A release exposes only rows passing the common descriptor suite.

## 5. Preview and interaction

Face dragging creates ephemeral function inputs or source transformations evaluated through the same worker path. UI manipulators use explicit local/world frames and commit one transaction. Preview may lower evaluation or tessellation quality; acceptance re-evaluates at production policy.

## 6. Topology and failure

Direct/surface functions publish modified/generated/deleted ancestry and stable labels. Their edit-support matrices cover source-face splitting, stitching, guide reorder, and continuity changes.

When healing or replacement admits multiple results, evaluation returns ambiguity instead of choosing by OCCT enumeration. Last-known-good geometry remains visible and stale.

## 7. Verification strategy

Descriptor-generated tests cover source generation/recognition, body-kind contracts, topology matrices, cancellation, transformations, unit scaling, and replay. Surface-specific metamorphic properties include:

- boundary endpoint interpolation within tolerance;
- continuity measurements under reparameterization and rigid transform;
- offset distance sampled against source;
- stitch/unstitch relation and reported gap bounds;
- thicken volume/boundary relationships for generated safe domains;
- trim/split region containment.

Direct-edit generators create analytic boxes/cylinders and imported-like solids, then verify intended face displacement/radius, unchanged protected topology, shape validity, and source-artifact retention. Curated kernel failures supplement generated domains.

## 8. Performance and cancellation

### DSM-010 — Bounded interaction

Face manipulators acknowledge each input generation within the shared interactive budget and never wait for exact geometry. `DIRECT-100` measures analytic face edits; `SURFACE-100` measures boundary/guide count, surface degree, sampling density, and topology growth. Both report first-preview and accepted-result latency, worker peak memory, artifact bytes, and cancellation time.

Preview artifacts are replaceable and byte-bounded. Exact acceptance cannot reuse a lower-quality preview as proof. Superseded work stops cooperatively or through worker termination and cannot replace the newest generation.

## 9. Acceptance

- Direct-edit an imported body through move, resize, and delete/repair; retain import bytes, publish topology evidence, and refuse an ambiguous heal.
- Build and edit a boundary/guide surface with continuity targets; inspect measured continuity/deviation, stitch to the declared body kind, and cancel a stale preview without changing the accepted result.

## 10. Open decisions

- **DSM-OPEN-001:** Exact direct-edit subset and OCCT Local Operations strategy.
- **DSM-OPEN-002:** Surface parameterization/orientation schema.
- **DSM-OPEN-003:** Continuity measurement algorithms and thresholds.
- **DSM-OPEN-004:** Feature-recognition engine and confidence calibration.
- **DSM-OPEN-005:** Imported-model reimport/update semantics.

## 11. Definition of done

A direct/surface graphical operation is implemented only when it uses the common function infrastructure, declares output/topology contracts, passes generated geometric relations, reports healing/continuity evidence, and preserves source provenance. Equivalent hand-written build123d remains valid without graphical recognition.
