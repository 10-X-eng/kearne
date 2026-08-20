# Project and Function Model

- **Status:** Proposed
- **Requirement prefix:** `DOC`
- **Depends on:** [system architecture](../01-system-architecture.md), [ADR-0009](../adr/0009-native-build123d-function-graph.md), [glossary](../GLOSSARY.md)
- **Unblocks:** commands, evaluation, persistence, modeling, product structure

## 1. Purpose

Define one immutable project model in which native build123d functions are part geometry and typed records carry engineering semantics build123d does not define.

## 2. Project snapshot

```text
ProjectSnapshot
  project_id: ProjectId
  revision_id: RevisionId
  root_tree: ContentTreeId
  records: PersistentMap<RecordId, EngineeringRecord>
  function_contracts: PersistentMap<ModelFunctionId, ModelFunctionContract>
  calls: PersistentMap<ModelCallId, ModelCall>
  artifacts: PersistentMap<ArtifactId, ArtifactMetadata>
  schema_set: SchemaSetId
```

The content tree maps stable project paths to content-addressed source blobs. Native Python/build123d source in that tree is canonical part intent, not a cache.

Derived state includes parsed syntax trees, recognition metadata, reverse-reference indices, dependency adjacency, UI trees and timelines, dirty flags, BREP, meshes, mass properties, solver results, selections, and presentation state. A persisted derived value is discardable and names its source revision and evaluator digest.

### DOC-001 — Immutable publication

Published snapshots, records, contracts, calls, and tree entries never mutate. A transaction creates replacements in a new revision. Older revisions retain their prior bytes.

### DOC-002 — Stable typed identity

Record, function, call, output, and occurrence IDs are globally collision-resistant, independent of names, paths, source spans, and collection positions, and never reused within a project. Public code uses typed ID wrappers.

### DOC-003 — Names and paths are not identity

Display names and module paths may change. Moving a module or renaming a function preserves identity through its contract mutation. Source spans, line numbers, tree indices, and Python object addresses are never durable identity.

### DOC-004 — Unknown data preservation

Unknown record schemas, source types, contracts, and bindings remain byte-preserved and receive an unavailable-evaluator diagnostic. Load and migration MUST NOT rewrite payloads they do not own.

## 3. Model functions

```text
ModelFunctionContract
  id: ModelFunctionId
  module: ProjectPath
  qualified_name: string
  inputs: map<InputKey, InputDeclaration>
  outputs: map<OutputKey, OutputDeclaration>
  environment: EnvironmentRef
  capability_profile: CapabilityProfileRef
  topology_mode: Labeled | BodyOnly | Dumb

ModelCall
  id: ModelCallId
  function: ModelFunctionId
  input_bindings: map<InputKey, Value | ParameterRef | NamedOutputRef>
  configuration_scope: ConfigurationScope

NamedOutputRef
  call_id: ModelCallId
  output_key: OutputKey
```

### DOC-005 — Source and contract separation

Source contains implementation. The contract contains stable identity and integration metadata. It may be declared by manifest or optional decorator, but Kearne can inspect it without running project code. A contract can outlive a temporarily invalid source edit so the failed revision remains repairable.

### DOC-006 — Unrestricted implementation

Kearne assigns semantics only to the function boundary. Helpers, control flow, classes, and build123d algebra or builder constructs inside the body are source implementation details.

### DOC-007 — Explicit inputs and outputs

Every call input is bound to a typed literal, parameter, or named upstream output. Every published result occupies a declared named slot. Positional return order, module globals, and display names are not output identity.

### DOC-008 — Function graph

Calls and bindings form the explicit geometry dependency graph. Internal calls within one module are covered by source digests but are not separate graph nodes unless declared as model functions. Cycles in the published graph are invalid.

### DOC-009 — Granularity is not prescribed

A GUI may generate one function per sketch or operation. AI or a user may define a whole component in one function. Both use the same contract, evaluation, history, and output-reference rules.

### DOC-010 — Recognition is derived

Classification such as sketch, extrude, fillet, or generated pattern is a source-digest-tagged projection. It may enable a specialized editor but never becomes a parallel geometry definition.

### DOC-011 — Topology capability is explicit

Named subshape references require published labels and ancestry that pass validation. Body-only or dumb outputs remain usable, but Kearne does not invent stable face or edge identity for them.

## 4. Engineering records

```text
EngineeringRecord
  id: RecordId<T>
  kind: stable schema-qualified name
  schema_version: positive integer
  owner: optional RecordId
  lifecycle: Active | Suppressed
  payload: typed value or preserved opaque bytes
  provenance: CreationProvenance
```

Initial record kinds are:

```text
ProjectRoot | ComponentDefinition
DatumFrame | DatumPlane | DatumAxis | DatumPoint
Parameter | MaterialAssignment | Requirement | ImportedSource
```

Later schemas add:

```text
Assembly | ComponentInstance | Joint
ConfigurationDefinition | ConfigurationInput
SimulationStudy | Load | Fixture | Contact
Drawing | Sheet | DrawingView | Annotation
BOMDefinition | Comment | ReleaseRecord
```

### DOC-012 — No duplicate geometry model

Component records reference named model-function outputs. They MUST NOT contain a second sketch/feature/body parameter graph for the same source-defined geometry.

### DOC-013 — Ownership and dependency differ

Every owned record has one owner. Ownership controls lifetime and navigation. Cross-owner computation uses explicit typed references. Deleting an owner removes only fields declared as owned; other downstream records remain with broken-reference diagnostics.

### DOC-014 — Component and occurrence separation

A component definition owns product semantics and binds model outputs. An assembly occurrence references a component and configuration; it is not a copy of geometry or source.

## 5. References and coordinate context

```text
ProjectRef =
    RecordRef<T>
  | ParameterRef
  | NamedOutputRef
  | TopologyRef
  | ExternalRevisionRef
```

### DOC-015 — No positional references

Canonical state contains no raw pointers, collection offsets, render IDs, OCCT subshape indices, database row IDs, line numbers, or user-visible names as sole identity.

### DOC-016 — Declared reference policy

Every reference field declares allowed targets, cardinality, ownership or dependency meaning, missing/suppressed behavior, external-reference policy, and visible rebinding behavior.

### DOC-017 — Coordinate and instance context

Geometric values name a right-handed coordinate context and documented transform convention. Geometry in an occurrence is addressed by `InstancePath` plus definition-owned output or topology reference, never by transform or tree position.

## 6. Parameters and configurations

Parameters use the shared dimensional value model. Accepted expressions retain editable source and resolved ID dependencies. Evaluation and queries carry an explicit `ConfigurationContext`; no process-global active configuration affects durable results.

## 7. Validation

Validation is layered:

1. tree paths, content digests, encoding, and size;
2. record and contract schemas;
3. typed references, ownership, and function-call bindings;
4. acyclic published dependencies;
5. environment and capability declarations;
6. domain rules.

Source parsing and evaluation are separate. Invalid Python can be retained as a failed revision, but it cannot publish new function outputs. Corrupt or structurally unsafe project data is quarantined before live use.

## 8. Queries and projections

Queries consume an immutable snapshot and return the observed revision. Reusable projections include content tree, function graph, recognized feature history, ownership tree, incoming references, searchable properties, and evaluation health. Every projection is reproducible from canonical state plus declared evaluator versions.

## 9. Verification

One generated state machine creates, replaces, moves, renames, binds, suppresses, deletes, branches, merges, saves, and reloads source and records. Properties include immutable ancestors, stable identity across moves, no dangling silent cascades, deterministic dependency invalidation, lossless unknown-data round-trip, parse-without-execution, and cache deletion without loss of intent.

The same generators run at small pull-request and large nightly sizes. Adding a function or record schema registers generators and invariants; it does not require a fixed example per entity count.

## 10. Open decisions

- Content-tree encoding is defined by [ADR-0013](../adr/0013-canonical-content-encoding.md). External semantic IDs are defined by [ADR-0010](../adr/0010-uuidv7-semantic-identities.md).
- **DOC-OPEN-002:** Manifest/decorator precedence and conflict diagnostics.
- **DOC-OPEN-003:** Model-call representation for multi-body and variable-cardinality outputs.
- Opaque payloads and persistent maps are defined by [ADR-0013](../adr/0013-canonical-content-encoding.md) and [ADR-0014](../adr/0014-persistent-project-state.md).

## 11. Definition of done

The model is implemented when source, contracts, calls, and records round-trip; generated histories preserve identity and unknown data; invalid source cannot publish outputs; cache deletion preserves intent; and assemblies, configurations, studies, and drawings reference geometry without duplicating its definition.
