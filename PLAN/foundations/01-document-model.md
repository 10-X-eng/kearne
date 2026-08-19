# Semantic Document Model

- **Status:** Proposed
- **Requirement prefix:** `DOC`
- **Depends on:** [System architecture](../01-system-architecture.md), [glossary](../GLOSSARY.md)
- **Unblocks:** commands, evaluation, persistence, every modeling capability

## 1. Purpose

Define the smallest durable semantic model capable of supporting parametric parts now and assemblies, configurations, simulation, drawings, branching, and collaboration later without changing identity or ownership rules.

The document stores engineering intent. It does not store GUI models, worker state, live kernel objects, or implicit relationships discoverable only by running code.

## 2. Canonical aggregate

An immutable snapshot contains:

```text
DocumentSnapshot
  document_id: DocumentId
  revision_id: RevisionId
  root_id: EntityId<DocumentRoot>
  entities: PersistentMap<EntityId, EntityRecord>
  source_artifacts: PersistentMap<ArtifactId, ArtifactMetadata>
  schema_set: SchemaSetId
```

The following are derived and MUST NOT be treated as canonical fields:

- reverse-reference index;
- dependency adjacency lists;
- tree/timeline projections;
- evaluation state and dirty flags;
- BREP and render meshes produced by native features;
- selection and expanded/collapsed UI state;
- search index and mass-property index.

Persisting a derived index for load performance is allowed only when it carries a source revision/digest and can be discarded after validation failure.

## 3. Entity record

Every canonical entity has an envelope:

```text
EntityRecord
  id: EntityId<T>
  kind: stable schema-qualified name
  schema_version: positive integer
  owner: optional EntityId
  lifecycle: Active | Suppressed
  payload: typed versioned value or preserved opaque bytes
  provenance: CreationProvenance
```

### DOC-001 — Stable typed identity

Entity IDs MUST be globally collision-resistant, independent of display names and container position, and never reused within a project. Code MUST use typed ID wrappers so a `BodyId` cannot be passed as a `SketchId` without explicit checked conversion.

UUIDv7 is the proposed external representation because it is portable, sortable by creation time, and branch-safe. Persistence MUST NOT rely on timestamp ordering for correctness.

### DOC-002 — Immutable record publication

An entity record published in a snapshot MUST NOT be modified. Editing replaces the record for the same ID in a new snapshot. Older revisions continue to observe the old value.

### DOC-003 — Explicit ownership

Every owned entity has exactly one canonical owner. Ownership controls lifetime and navigation, not computation order. Cross-owner dependencies are explicit semantic references.

### DOC-004 — Names are not identity

Display names may be duplicated and changed. Expressions that expose symbolic names MUST resolve them to IDs during validation and preserve enough source text for editing.

### DOC-005 — Unknown entity preservation

If an entity kind or schema version cannot be evaluated, its envelope and payload MUST be preserved byte-for-byte where the serialization format permits. Known references to it remain intact and receive `UnavailableEvaluator` diagnostics.

## 4. Core entity kinds

The initial schema reserves these stable concepts:

```text
DocumentRoot
ComponentDefinition
DatumFrame | DatumPlane | DatumAxis | DatumPoint
Parameter
Sketch
Body
Feature
ImportedSource
MaterialAssignment
Requirement
```

Later schemas add without replacing the identity model:

```text
Assembly | ComponentInstance | Joint
ConfigurationDefinition | ConfigurationInput
SimulationStudy | Load | Fixture | Contact
Drawing | Sheet | DrawingView | Annotation
Comment | ReleaseRecord
```

### DOC-006 — Component and body separation

A component definition owns zero or more bodies, datums, sketches, and features. A body is not a component and an assembly occurrence is not a copy of either. The MVP UI may expose one component, but persistence uses this full distinction from version 1.

### DOC-007 — Feature output slots

A feature descriptor declares stable, named output slots. A result reference is:

```text
ResultRef
  feature_id
  output_key
```

Output keys are schema-defined identifiers such as `body`, `surface`, or `profile`, never vector offsets. A feature changing the number of dynamic outputs uses persistent element keys defined by that feature type.

### DOC-008 — Bodies represent design continuity

A body entity provides stable product identity and references the feature result currently defining its tip. Features may create, consume, split, or combine bodies through explicit inputs and output bindings. The feature-history display order is a projection, not the source of dependency truth.

## 5. Semantic references

All persisted relationships use a tagged union:

```text
SemanticRef =
    EntityRef<T>
  | ParameterRef
  | ResultRef
  | TopologyRef
  | ExternalRef
```

### DOC-009 — No positional references

Canonical state MUST NOT contain raw pointers, collection offsets, render IDs, OCCT subshape indices, database row IDs, or user-visible names as the sole reference identity.

### DOC-010 — Reference policy declaration

Every schema field containing a reference declares:

- allowed target kinds;
- required or optional;
- ownership versus dependency semantics;
- behavior when missing or suppressed;
- whether an external document is allowed;
- whether reference rebinding is user-visible.

### DOC-011 — Deletion preserves evidence

Deleting an entity removes it from the new snapshot but records a typed `DeleteEntity` mutation containing its prior digest. References are not silently cascaded unless the command schema explicitly owns dependent children. Non-owned downstream entities remain and report broken references.

## 6. Datums and coordinate spaces

### DOC-012 — Explicit coordinate context

All geometric values are interpreted in a declared coordinate context. Every component definition owns an immutable origin frame and standard datum planes/axes with stable IDs.

### DOC-013 — Transform convention

Kearne uses right-handed coordinate systems and one documented matrix/vector convention at all public boundaries. Units are not embedded ambiguously in transforms; translation components are dimensional quantities on semantic APIs and normalized SI values on the canonical wire form.

### DOC-014 — Instance paths

Definition-owned geometry in an assembly occurrence is addressed by an `InstancePath` plus the definition-owned semantic reference. Occurrence identity MUST NOT be synthesized from transforms or tree indices.

## 7. Parameters and configurations

Parameters are entities or feature-owned typed fields according to their need for independent reference. Both use the shared dimensional value model.

### DOC-015 — Resolved dependencies

An accepted expression stores editable source plus a resolved, typed dependency representation. Evaluation never searches by display name.

### DOC-016 — Configuration context is explicit

Evaluation and query APIs carry a `ConfigurationContext`. The MVP uses a default context; it MUST NOT rely on process-global “active configuration” state.

## 8. Validation

Document validation has reusable levels:

1. **Structural:** envelope, schema, typed ID, required fields.
2. **Referential:** targets exist and have allowed kinds.
3. **Ownership:** no ownership cycles, one owner, permitted containment.
4. **Dependency:** declared graph is acyclic where required.
5. **Domain:** feature- or entity-specific semantic rules.

### DOC-017 — Central invariant registry

Schema-independent invariants MUST be implemented once in the document library. Entity descriptors contribute domain validators without bypassing structural validation.

### DOC-018 — Load quarantine

A structurally unsafe or corrupt record MUST NOT become a live mutable entity. The loader reports it through a recoverable quarantine representation and retains original project bytes until the user chooses recovery or export.

## 9. Queries and projections

Queries consume an explicit immutable snapshot and return values tagged with its revision. Common reusable projections include:

- ownership tree;
- feature dependency graph;
- incoming-reference index;
- searchable property index;
- structure view and feature-history view;
- evaluation-health summary.

Projections may be incrementally maintained from mutation batches but MUST be reproducible from the snapshot.

## 10. Verification strategy

### Contract properties

- Serialize/deserialize preserves semantic equality for every registered entity schema.
- Replacing one record cannot modify any older snapshot.
- Generated valid ownership forests pass validation; generated cycles fail with stable diagnostics.
- Generated deletion leaves non-owned downstream records present and discoverably broken.
- Random display-name changes do not change resolved identity.
- Unknown payload round-trips without loss.

### Model-based state machine

Generate create, replace, suppress, unsuppress, delete, rename, reference, transaction rollback, and snapshot/reload actions. Compare the production document against a deliberately simple reference model.

### DOC-019 — Scale independence

The same generator and invariant suite MUST run at small PR sizes and larger nightly sizes. Test logic MUST NOT enumerate a separate case per entity count or feature type.

## 11. Open decisions

- **DOC-OPEN-001:** UUIDv7 library and byte-order representation.
- **DOC-OPEN-002:** Persistent map implementation and memory-sharing measurements.
- **DOC-OPEN-003:** Exact feature/body binding schema for split and merge operations; resolve before boolean implementation.
- **DOC-OPEN-004:** Opaque unknown-payload encoding in the selected file format.

## 12. Definition of done

The model is implemented when registered schemas round-trip, the generated state machine passes at configured scales, architecture tests prove domain independence, and a future assembly/configuration schema can be expressed without changing identity or ownership fundamentals.
