# Kearne Glossary

- **Status:** Proposed
- **Purpose:** Give product, code, schemas, tests, UI, and AI one vocabulary.

| Term | Meaning |
|---|---|
| Agent Bridge | Kearne-owned local MCP adapter that exposes capability-filtered Engineering and Observation API tools to Codex app-server. It contains no engineering rules. |
| Application session | One driver-managed Kearne process lifetime and its owned UI surfaces, identified independently of window titles or OS process scans. |
| Artifact | Immutable content such as source, import bytes, BREP, mesh, thumbnail, or simulation results. Source/import artifacts may be canonical or irreplaceable; evaluated artifacts are derived. |
| Body | A named function output representing a solid, sheet, wire, or compound and bound to a component definition. |
| Branch | A named mutable pointer to a revision. Branch UI arrives later; revision identity exists from the first persistent format. |
| Cache | Discardable data that can be reproduced from canonical state plus pinned evaluator dependencies. If it cannot be reproduced, it is an artifact, not a cache. |
| Command request | Validated user or automation intent submitted to the Engineering API. It is not itself canonical project state. |
| Codex thread | App-server conversational history and turn context. It is operational AI state, not a Kearne project, branch, revision, or audit authority. |
| Component definition | Reusable product definition that binds named model outputs to datums, properties, and configuration inputs. |
| Component instance | Placement of a component definition in an assembly, with transform and instance-level overrides. |
| Configuration | A named or parameterized set of overrides evaluated against the same component definition. |
| Diagnostic | Structured finding with stable code, severity, message parameters, affected references, provenance, and possible repairs. |
| Content tree | Content-addressed mapping from project paths to immutable source and data blobs at one revision. |
| Engineering record | Typed persistent nongeometry product state with stable identity, schema, lifecycle, and payload. |
| Evaluation | Pure-with-respect-to-project computation that derives results from a snapshot, evaluator fingerprint, and declared inputs. External solvers may be nondeterministic and must report that property. |
| Evaluator fingerprint | Version identity of code and dependencies affecting a derived result, including OCCT and plugin versions where applicable. |
| Graphical operation | Source pattern Kearne can generate, recognize, and structurally edit. It is tooling capability, not a second geometry node. |
| Head | Current revision selected by a workspace or branch. Moving a head does not mutate an older snapshot. |
| Instance path | Ordered sequence of instance IDs locating a definition-owned entity in an assembly occurrence. |
| Job | Scheduled execution record for expensive or asynchronous work. Job state is operational data; durable intent remains in project revisions. |
| Model call | Invocation of a declared model function with typed literal, parameter, or named-output bindings. |
| Model function | Native Python/build123d entry point with stable identity, typed inputs, named outputs, environment, and topology capability. Its body is canonical part geometry. |
| Mutation | Normalized atomic change to source, function contracts/calls, records, or artifacts after command validation. |
| Named output | Stable output slot of a model call; downstream geometry references use call identity plus output key. |
| Observation point | Correlated semantic-UI, frame, render, revision, and job generations used to await and capture a known desktop state without sleeping. |
| Project | Persisted revision graph containing a content tree, model functions/calls, typed engineering records, artifacts, metadata, and workspace state. |
| Project snapshot | Immutable content tree and typed graph at one `RevisionId`. |
| Revision | Immutable node with parent revision IDs and one committed mutation batch. A revision is not necessarily a named version. |
| Semantic reference | Typed persistent reference to a record, parameter, named output, datum, or topology label; never a pointer, array index, render ID, source line, or raw OCCT subshape index. |
| Semantic UI snapshot | Immutable observation of control identities, roles, states, bounds, actions, and application generations; it contains no engineering mutation authority. |
| Topology label | Producer-published identity for a face, edge, or vertex plus ancestry, resolution evidence, and confidence. |
| Transaction | Atomic validation and commit boundary producing zero or one revision. It may contain multiple command requests and one normalized mutation batch. |
| Version | User-named immutable reference to a revision carrying release or checkpoint intent. |
| Workspace | Local mutable editing context with a head revision, redo choices, transient previews, selection, and unsaved UI state. |

## Naming rules

- `Id` means stable semantic identity; `Handle` means process-local or session-local access.
- `Ref` means a serializable typed reference; it never implies ownership.
- `Snapshot` is immutable after publication.
- `Descriptor` is declarative metadata; it must not conceal mutable engineering state.
- `Result` represents an expected success/failure value. Exceptions are reserved for violated programmer invariants or unrecoverable runtime faults.
- “Part” is a product/UI term. The domain model uses component definitions and named outputs so assemblies and multi-body modeling do not require an identity migration.
