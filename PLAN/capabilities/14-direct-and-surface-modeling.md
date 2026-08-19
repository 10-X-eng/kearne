# Direct and Surface Modeling

- **Status:** Proposed; post-MVP
- **Requirement prefix:** `DSM`
- **Depends on:** [Solid modeling](03-solid-modeling.md), [persistent topology](../foundations/04-persistent-topology.md), [import/export](04-import-export.md)
- **Unblocks:** imported-model editing, professional surfacing, later sheet metal

## 1. Purpose

Extend native feature history with semantic direct edits and sheet/surface bodies while preserving source geometry, topology evidence, and honest failure behavior.

## 2. Body kinds

The body schema distinguishes:

```text
SolidBody
SheetBody
WireBody where explicitly supported
Compound result as a result type, not an ambiguous solid
```

### DSM-001 — Dimensionality is explicit

Feature inputs and output slots declare permitted body kinds. An operation that unexpectedly produces a shell, multiple solids, or a compound cannot publish it under a single-solid contract.

### DSM-002 — Shared feature infrastructure

Direct and surface operations use the same descriptors, commands, evaluation keys, workers, topology publication, body scope, diagnostics, preview, persistence, and generated conformance harness as solid features.

## 3. Direct editing

Initial direct features:

- move/rotate face;
- offset/press-pull face;
- delete face with healing;
- replace face;
- resize recognized cylindrical face group.

Each stores selected topology, transform/distance parameters, explicit propagation/healing policy, and target body.

### DSM-003 — Direct edits remain features

A direct operation appends or inserts a semantic feature. It MUST NOT overwrite an imported/native BREP in place or erase upstream history.

### DSM-004 — Recognition is a proposal

Hole, fillet, pocket, cylindrical group, and pattern recognition returns candidates with evidence. Accepting a candidate creates explicit feature/reference commands. Recognition never silently rewrites the model.

### DSM-005 — Imported source remains recoverable

Direct editing an imported body consumes its normalized geometry artifact while retaining source bytes and import provenance. Reimport/update later creates a preview and topology reconciliation, not an implicit replacement.

## 4. Surface modeling

Planned feature groups:

1. Surface extrude/revolve/sweep/loft, ruled and offset surface.
2. Trim, extend, split, stitch/unstitch, fill, and thicken.
3. Boundary surface and face blend with continuity controls.
4. Curvature, zebra, draft, continuity, radius, and deviation analysis.

### DSM-006 — Boundary intent

Surface features store ordered boundary/guide references, parameterization/orientation choices, continuity target (`G0`/`G1`/`G2`/supported higher level), and weights/tolerances. The evaluator does not infer an alternate ordering silently when the requested construction fails.

### DSM-007 — Stitching disclosure

Stitching records tolerance policy, gaps closed, topology changes, and whether a closed solid resulted. Tolerance expansion beyond policy requires explicit approval.

### DSM-008 — Continuity is measured

A successful kernel operation does not prove requested continuity. Kearne evaluates positional/tangent/curvature deviation using a declared sampling/analytic policy and publishes the measured result.

## 5. Preview and interaction

Face dragging creates ephemeral direct-feature parameters evaluated through the same worker path. UI manipulators operate in explicit local/world frames and commit one command. Preview may lower evaluation/tessellation quality but acceptance re-evaluates at production policy.

## 6. Topology and failure

Direct/surface operations publish modified/generated/deleted ancestry and feature-specific names. Their edit-support matrices must cover source-face splitting, stitching, guide reorder, and continuity changes.

When healing or replacement admits multiple results, evaluation returns ambiguity instead of choosing by OCCT enumeration. Last-known-good geometry remains visible but stale.

## 7. Verification strategy

Descriptor-generated tests cover body-kind contracts, topology matrices, cancellation, transformations, unit scaling, and replay. Surface-specific metamorphic properties include:

- boundary endpoint interpolation within tolerance;
- continuity measurements under reparameterization and rigid transform;
- offset distance sampled against source;
- stitch/unstitch relation and reported gap bounds;
- thicken volume/boundary relationships for generated safe domains;
- trim/split region containment.

Direct-edit generators create analytic boxes/cylinders and imported-like solids, then verify intended face displacement/radius, unchanged protected topology, shape validity, and source-artifact retention. Curated kernel failures supplement generated domains.

## 8. Open decisions

- **DSM-OPEN-001:** Exact direct-edit subset and OCCT Local Operations strategy.
- **DSM-OPEN-002:** Surface parameterization/orientation schema.
- **DSM-OPEN-003:** Continuity measurement algorithms and thresholds.
- **DSM-OPEN-004:** Feature-recognition engine and confidence calibration.
- **DSM-OPEN-005:** Imported-model reimport/update semantics.

## 9. Definition of done

A direct/surface feature is implemented only when it shares native feature infrastructure, declares body/topology contracts, passes generated geometric relations, reports healing/continuity evidence, and preserves imported source provenance.
