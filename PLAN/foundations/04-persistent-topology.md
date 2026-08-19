# Persistent Topology Identity

- **Status:** Proposed; high-risk prototype required
- **Requirement prefix:** `TOP`
- **Depends on:** [Document model](01-document-model.md), [evaluation](03-evaluation-and-jobs.md), [numerics](05-units-expressions-numerics.md)
- **Unblocks:** downstream functions, drawings, comments, simulation, semantic selection

## 1. Purpose

Allow a user reference such as “the end cap of this extrusion” to survive documented upstream edits without persisting OCCT subshape indices or silently guessing when design intent is ambiguous.

Persistent topology is a provenance and resolution system, not one clever geometric hash.

## 2. Public reference

```text
TopologyRef
  producer: NamedOutputRef
  name: TopologyName
  expected_kind: Face | Edge | Vertex
  authored_revision: RevisionId
  authored_signature: optional GeometricSignature
```

`TopologyName` is a versioned label or expression published by the producing function:

```text
Cap(Start)
Cap(End)
Generated(ProfileEdge(<SketchEntityId>))
Modified(<InputTopologyRef>, role)
Intersection(<InputTopologyRef>, <InputTopologyRef>, disambiguator)
PatternMember(<source name>, <member key>)
```

The serialized representation is structured data, not a localized string.

### TOP-001 — Producer-defined naming

Every function claiming labeled topology MUST declare how it names public output subshapes and how upstream labels propagate through generated, modified, split, merged, and deleted results. Functions may instead declare body-only or dumb topology.

### TOP-002 — No kernel index persistence

OCCT explorer order, `HashCode`, memory address, triangulation order, and transient selection IDs MUST NOT enter a persisted topology reference.

### TOP-003 — Stable member keys

Patterns and other collections use stable member keys derived from semantic inputs, not current list positions. Suppressing or inserting one member does not renumber unrelated references.

## 3. Evaluation publication

Each geometry result publishes a topology table:

```text
TopologyRecord
  name
  shape_kind
  ephemeral kernel handle (worker-local)
  geometric signature
  adjacency signature
  provenance edges
  resolution evidence
```

The worker also publishes a compact mapping from tessellated primitive ranges to topology names so rendering and picking never infer persistent identity from triangle numbers alone.

## 4. Resolution ladder

Resolution uses ordered evidence:

1. **SemanticExact:** the evaluator emitted the requested function-published label exactly once.
2. **OperationHistory:** OCCT operation history maps the prior named subshape to exactly one compatible result.
3. **StructuredReconciliation:** producer-specific split/merge rules and adjacency produce one result.
4. **GeometricMatch:** numerical signature matching produces one candidate separated from the next candidate by configured confidence margins.
5. **Ambiguous:** multiple candidates remain plausible.
6. **Broken:** no compatible candidate remains.

### TOP-004 — Honest ambiguity

Only levels 1–4 return a resolved subshape. Ambiguous and broken references block dependent evaluation and expose candidates/evidence for repair. Kearne MUST NOT select the first OCCT result or nearest candidate merely to continue.

### TOP-005 — Confidence is evidence-based

Resolution returns method, score components, threshold profile, candidate count, and source/target names. UI labels such as “Exact” or “Geometric match” are projections of this evidence.

### TOP-006 — Producer rules precede similarity

Generic geometric matching is fallback behavior. A function claiming labeled topology MUST publish construction semantics rather than rely only on area or centroid comparison.

## 5. Geometric and adjacency signatures

Signatures may include, with normalized tolerances:

- topological kind;
- analytic surface/curve kind and parameters;
- measure: area, length;
- centroid or representative point in producer-local coordinates;
- orientation relative to the producer frame;
- bounding box;
- closed/open and periodic properties;
- adjacent named topology and valence;
- source ancestry and operation role.

### TOP-007 — Signatures are not identity

A signature supports reconciliation and diagnostics but is never sufficient identity by itself. Symmetric geometry is expected and must become ambiguous when semantics cannot distinguish it.

### TOP-008 — Scale-aware tolerances

Comparisons use the project numerical profile and local characteristic size. Fixed global epsilons and exact floating-point equality are prohibited.

## 6. Splits, merges, and disappearance

- A one-to-one modified subshape retains its semantic ancestry.
- A split emits child labels containing stable split keys. Producer-specific geometric ordering is allowed only when invariant under documented transformations and tolerance perturbations.
- A merge records all source ancestry but has one new output label; references to either source may resolve historically only if the function contract allows it.
- A deleted subshape resolves `Broken` with deletion provenance.
- Reappearance after parameter reversal may recover the prior label only from the same function's published semantics, not global nearest matching.

## 7. MVP edit-support matrix

Every labeled function family defines a matrix with upstream mutation classes:

```text
parameter magnitude change
profile dimension change
profile entity insertion/deletion
operation direction reversal
body transform
pattern count/spacing change
boolean tool movement
function dependency reorder where legal
suppression/unsuppression
```

Each cell is classified:

```text
Guaranteed | BestEffort | ExpectedAmbiguous | ExpectedBroken | Unsupported
```

### TOP-009 — Published compatibility contract

MVP acceptance is based on this matrix, not an undefined claim of “robust topology.” A release may improve classifications but MUST NOT quietly regress a guaranteed cell.

## 8. Selection and repair

### TOP-010 — Selection captures semantics

Selecting a rendered primitive returns the topology name, owning result, occurrence path, evaluation key, and resolution evidence. Commands validate that evidence against their base revision.

### TOP-011 — Explicit rebind

When a reference is ambiguous or broken, the user may select a replacement. Rebinding is an ordinary command that preserves the old reference and repair provenance in history.

### TOP-012 — Semantic query results

Natural-language and programmatic selection return sets of `TopologyRef` values with revision and evaluation keys. They do not return raw shapes to persistent command payloads.

## 9. Verification strategy

The topology suite is producer-contract driven. Every labeled function family registers:

- its name constructors;
- valid topology invariants;
- generated parameter domains;
- supported edit matrix;
- transformations under which naming should be invariant.

Tests generate source models and edit sequences, then verify:

- guaranteed names resolve to topologically/geometrically compatible results;
- rigid transforms do not change producer-relative identity;
- serialization and worker-process round trips preserve names;
- symmetric cases become ambiguous rather than nondeterministic;
- reordering OCCT exploration results does not change published names;
- small perturbations inside the numerical profile do not cause random rebinding;
- known kernel regressions remain in a bounded corpus.

Exact BREP bytes and face counts alone are not valid topology oracles.

## 10. Technical prototype exit criteria

Before broad modeling work, implement sketch -> extrude -> fillet -> hole and exercise at least these mutations:

- dimension changes that preserve profile entities;
- adding and deleting unrelated profile entities;
- reversing extrusion direction;
- fillet success, failure, and recovery;
- symmetric duplicate faces;
- save/reload and worker restart.

The prototype must publish measured success by edit-matrix cell and record unresolved ambiguities. A demo that works for one hand-built plate is insufficient.

## 11. Open decisions

- **TOP-OPEN-001:** Internal representation of structured `TopologyName` and its schema evolution.
- **TOP-OPEN-002:** OCCT history coverage and defects for each pinned operation.
- **TOP-OPEN-003:** Generic matching algorithm, score normalization, and confidence margins.
- **TOP-OPEN-004:** Stable split-key rules for boolean and fillet outputs.
- **TOP-OPEN-005:** Storage of topology tables as artifacts versus compact result metadata.

## 12. Definition of done

Persistent topology v1 is implemented when each labeled MVP function family publishes a complete contract, generated edit-matrix tests pass on both platforms, body-only functions are not overpromised, ambiguity is deterministic and repairable, and no persisted reference relies on transient OCCT ordering.
