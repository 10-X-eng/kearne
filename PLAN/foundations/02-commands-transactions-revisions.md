# Commands, Transactions, and Revisions

- **Status:** Proposed
- **Requirement prefix:** `CMD`
- **Depends on:** [Document model](01-document-model.md), [units and expressions](05-units-expressions-numerics.md)
- **Unblocks:** persistence, automation, versioning, all editing tools

## 1. Purpose

Provide one mutation path for source, function bindings, and typed engineering records while making undo, recovery, replay, branching, audit, and synchronization consequences of one immutable revision model.

Kearne does not use command objects that hold hidden mutable state and implement bespoke inverse methods. Such objects are difficult to serialize, retry, merge, or recover after a crash.

## 2. Command envelope

```text
CommandEnvelope
  request_id: RequestId
  command_type: stable qualified name
  schema_version: integer
  base_revision: RevisionId
  actor: ActorRef
  origin: Human | Python | AI | Plugin | Import | Replay | System
  permission_context: PermissionContextId
  gesture_id: optional GestureId
  payload: typed command payload
```

### CMD-001 — Commands express intent

A command request describes an intended operation such as `model.extrude.create`, `source.module.replace`, or `assembly.joint.create`, not storage edits such as “write this JSON path.” Direct source replacement is a first-class operation with an expected digest; arbitrary record or database patches are not public APIs.

### CMD-002 — Schema and domain validation

Before mutation, the engine validates the envelope schema, permissions, base revision, reference types, dimensional values, and command-specific preconditions. Validation failure creates no revision.

### CMD-003 — Idempotent request identity

A committed `request_id` maps durably to exactly one outcome. Retrying it returns that outcome. Reusing it with different bytes is an invariant violation and security diagnostic.

## 3. Normalized mutations

Validated commands produce a small closed set of internal mutations:

```text
PutContent(path, expected_prior_digest, content_artifact)
MoveContent(old_path, new_path, expected_digest)
DeleteContent(path, expected_prior_digest)
CreateRecord(record)
ReplaceRecord(id, expected_prior_digest, new_record)
DeleteRecord(id, expected_prior_digest)
CreateFunctionContract(contract)
ReplaceFunctionContract(id, expected_prior_digest, new_contract)
DeleteFunctionContract(id, expected_prior_digest)
CreateModelCall(call)
ReplaceModelCall(id, expected_prior_digest, new_call)
DeleteModelCall(id, expected_prior_digest)
AttachArtifact(metadata)
DetachArtifact(id, expected_prior_digest)
```

Source blobs, contracts, calls, and records are replaced as complete versioned values rather than mutated through general path patches. This keeps validation, migration, diff, and replay understandable.

### CMD-004 — Mutation isolation

Only the transaction engine may apply normalized mutations. Mutations are internal persisted facts and are not a general-purpose public editing API.

### CMD-005 — Deterministic normalization

Given the same accepted command bytes, base snapshot, registered schemas, and deterministic policy inputs, normalization MUST produce semantically equivalent mutations. Allocated IDs and timestamps are supplied explicitly through the transaction context so tests and replay can control them.

## 4. Transactions

A transaction contains one or more command envelopes evaluated sequentially against a private staged snapshot:

```text
Open(base revision)
  -> validate command 1 -> stage mutations
  -> validate command 2 against staged state -> stage mutations
  -> validate complete staged snapshot
  -> durable atomic commit
  -> publish one new revision
```

### CMD-006 — Atomic visibility

Readers observe either the base revision or the committed revision, never an intermediate staged state. A failed command rolls back the entire transaction unless the transaction schema explicitly marks independent optional proposals before execution.

### CMD-007 — No kernel work in commit

Transactions MUST NOT perform unbounded OCCT, meshing, simulation, Python, AI, network, or export work. Commands validate semantic preconditions using already published artifacts where required; evaluation happens after commit.

### CMD-008 — Geometry-dependent preconditions

A command that requires evaluated geometry includes the expected evaluation key and topology resolution evidence. If those do not match the base revision, validation returns `StaleEvaluation` instead of applying the command to a guessed target.

## 5. Revisions and history

```text
RevisionRecord
  id: RevisionId
  parents: [RevisionId]       // one normally, two for merge
  transaction_id: TransactionId
  mutations: MutationBatch
  actor and provenance
  schema_set
  committed_at
  project_root_digest
```

### CMD-009 — Immutable revision DAG

Every successful transaction creates one immutable revision node. Existing revision contents and parent links never change.

### CMD-010 — Workspace head

A workspace stores a mutable head pointer separately from revision content. Undo moves the head to a parent. Redo selects a known child. A new edit after undo creates another child; it does not erase the abandoned future.

### CMD-011 — Public history foundation

The revision DAG and identity semantics exist in the first persisted format even though named branches, merge UI, and release workflows arrive later.

### CMD-012 — Audit completeness

Each revision records actor, origin, command descriptions or securely retained request references, normalized mutations, and causal transaction metadata. Secrets, raw AI prompts, and access tokens MUST NOT enter history by default.

## 6. Optimistic concurrency

### CMD-013 — Explicit base revision

Every persistent command names its base revision. If the workspace head has moved, the command fails with `RevisionConflict` unless its descriptor declares and implements a deterministic rebase rule.

Automatic rebase is initially restricted to operations proven independent through declared read/write sets. “Last writer wins” is not a default engineering merge policy.

### CMD-014 — Declared effects

Command descriptors report conservative content paths and function, call, record, and artifact read/write sets after validation. These support diagnostics, preview, permissions, safe rebase analysis, diff, and collaboration.

### CMD-015 — Source transformations are proposals

A specialized GUI tool may ask a source service to generate or structurally transform native build123d code. The service returns replacement bytes, expected prior digest, contract changes, and diagnostics. Only the transaction engine commits them. A stale digest or unsafe transform fails without fallback text rewriting.

## 7. Preview and continuous interaction

### CMD-016 — Preview is ephemeral

Dragging a dimension or editing a feature dialog creates generation-tagged preview snapshots and evaluation requests. Preview revisions are not added to durable history and are invalid after their base or generation changes.

### CMD-017 — One gesture, one durable revision

Continuous UI gestures SHOULD commit one final command. Intermediate values may be sampled for preview and telemetry but MUST NOT flood durable history.

### CMD-018 — Preview equivalence

Accepting a preview submits an ordinary command. The preview implementation MUST NOT have a separate geometry or validation path. Its result may be reused only if its evaluation key exactly matches the committed snapshot.

## 8. Evaluation failure semantics

Project validity and source/geometric evaluability are distinct:

- invalid references, illegal units, or impossible contracts reject the command before commit;
- source with syntax errors or an infeasible fillet may commit and subsequently fail to parse or evaluate;
- failed source remains editable and retains diagnostics;
- downstream evaluation is blocked or uses an explicitly supported partial-output policy;
- undo operates on the committed revision regardless of evaluation status.

### CMD-019 — No silent rollback after publication

Once a revision is durably acknowledged, later evaluation failure MUST NOT remove or rewrite it. Repair is another command or movement of the workspace head.

## 9. Verification strategy

One model-based state machine generates:

- valid and invalid source, contract, binding, and record commands;
- multi-command transactions;
- duplicate request delivery;
- stale base revisions;
- undo, redo, divergent edits, and head movement;
- injected normalization and commit failures;
- serialization and replay at arbitrary steps.

Properties include atomicity, idempotency, immutable ancestors, deterministic replay, no lost divergent revisions, stable function identity across source moves, and correspondence between head snapshot and reference model.

Every command descriptor joins a shared conformance suite that checks schema rejection, permission denial, deterministic normalization, declared effects, readable description, provenance, round-trip, and replay.

## 10. Open decisions

- **CMD-OPEN-001:** Revision ID construction: random UUID versus content-derived ID plus collision-safe envelope.
- **CMD-OPEN-002:** Retention policy for original command request payloads versus only normalized mutations and redacted descriptions.
- **CMD-OPEN-003:** Durable semantics of local uncommitted UI workspace settings versus project revisions.
- **CMD-OPEN-004:** Safe initial set of automatically rebasable commands.

## 11. Definition of done

This plan is implemented when the reference state machine passes with faults and retries, every registered command passes the conformance suite, replay reconstructs identical content trees and typed records, and no adapter can mutate project state outside this path.
