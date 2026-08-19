# Kearne Glossary

- **Status:** Proposed
- **Purpose:** Give product, code, schemas, tests, UI, and AI one vocabulary.

| Term | Meaning |
|---|---|
| Artifact | Immutable bulk output such as BREP, mesh, thumbnail, simulation results, or an imported source file. Addressed by digest and stored outside canonical entity records. |
| Body | A document entity representing a connected solid, sheet, or compound modeling result owned by a component definition. |
| Branch | A named mutable pointer to a revision. Branch UI arrives later; revision identity exists from the first persistent format. |
| Cache | Discardable data that can be reproduced from canonical state plus pinned evaluator dependencies. If it cannot be reproduced, it is an artifact, not a cache. |
| Command request | Validated user or automation intent submitted to the Engineering API. It is not itself canonical document state. |
| Component definition | Reusable authored definition containing bodies, features, datums, properties, and configuration inputs. |
| Component instance | Placement of a component definition in an assembly, with transform and instance-level overrides. |
| Configuration | A named or parameterized set of overrides evaluated against the same component definition. |
| Diagnostic | Structured finding with stable code, severity, message parameters, affected references, provenance, and possible repairs. |
| Document | Kearne's semantic engineering aggregate. It contains entities and revision metadata, not live UI or OCCT objects. |
| Document snapshot | Immutable logical state at one `RevisionId`. Snapshots may use persistent data structures internally; immutability is an observable contract. |
| Entity | Persisted semantic object with a stable typed ID, kind, schema version, lifecycle state, and payload. |
| Evaluation | Pure-with-respect-to-document computation that derives results from a snapshot, evaluator fingerprint, and declared inputs. External solvers may be nondeterministic and must report that property. |
| Evaluator fingerprint | Version identity of code and dependencies affecting a derived result, including OCCT and plugin versions where applicable. |
| Feature | Parameterized semantic operation whose evaluation produces or transforms geometric results. |
| Head | Current revision selected by a workspace or branch. Moving a head does not mutate an older snapshot. |
| Instance path | Ordered sequence of instance IDs locating a definition-owned entity in an assembly occurrence. |
| Job | Scheduled execution record for expensive or asynchronous work. Job state is operational data; durable engineering intent remains in the document. |
| Mutation | Normalized atomic change to semantic entities, produced after command validation. |
| Project | Persisted container containing a document revision graph, artifacts, metadata, and local workspace state. |
| Revision | Immutable node with parent revision IDs and one committed mutation batch. A revision is not necessarily a named version. |
| Semantic reference | Typed persistent reference to an entity, parameter, datum, or topology name; never a pointer, array index, render ID, or raw OCCT subshape index. |
| Topology name | Persistent, feature-relative identity for a produced face, edge, or vertex plus resolution evidence and confidence. |
| Transaction | Atomic validation and commit boundary producing zero or one revision. It may contain multiple command requests and one normalized mutation batch. |
| Version | User-named immutable reference to a revision carrying release or checkpoint intent. |
| Workspace | Local mutable editing context with a head revision, redo choices, transient previews, selection, and unsaved UI state. |

## Naming rules

- `Id` means stable semantic identity; `Handle` means process-local or session-local access.
- `Ref` means a serializable typed reference; it never implies ownership.
- `Snapshot` is immutable after publication.
- `Descriptor` is declarative metadata; it must not conceal mutable engineering state.
- `Result` represents an expected success/failure value. Exceptions are reserved for violated programmer invariants or unrecoverable runtime faults.
- “Part” is a product/UI term. The domain model uses component definitions and bodies so assemblies and multi-body modeling do not require a later identity migration.
