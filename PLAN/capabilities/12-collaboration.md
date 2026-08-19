# Collaboration and Synchronization

- **Status:** Proposed; final roadmap phase
- **Requirement prefix:** `COL`
- **Depends on:** [Versioning](09-versioning-and-merge.md), [persistence](../foundations/06-persistence-and-recovery.md), [Engineering API](../foundations/08-engineering-api.md), [security](../delivery/06-security-threat-model.md)
- **Unblocks:** cloud projects, teams, comments, presence, concurrent editing

## 1. Purpose

Add optional synchronization and teamwork without weakening local ownership, immutable revision history, semantic conflict handling, or offline editing.

## 2. Synchronization model

Initial collaboration synchronizes immutable objects:

```text
revision records and mutation batches
branch/version/reference updates
required source artifacts
approved derived artifacts
comments/review records
access-control metadata
```

Content is addressed by stable IDs/digests. Upload and download are resumable, idempotent, and integrity-checked.

### COL-001 — Local-first remains valid

Network loss does not prevent opening and editing locally available projects. Offline edits create normal local revisions and synchronize later.

### COL-002 — No implicit upload

A local project becomes synchronized only through explicit project/user/organization configuration. AI provider access and project synchronization are separate permissions.

### COL-003 — Revision synchronization

Synchronization transfers immutable revision DAG nodes and updates mutable branch references through compare-and-swap. It does not repeatedly upload a monolithic project file or replay untrusted commands with new semantics.

## 3. Concurrent editing strategy

The first collaborative editing model is branch/revision based:

- each client commits immutable revisions against an observed remote head;
- fast-forward updates are automatic;
- independent semantic changes may auto-merge through the normal merge engine;
- conflicts produce a merge workspace;
- a server may sequence accepted branch-reference updates but does not use last-writer-wins on source, function contracts, or engineering records.

### COL-004 — No universal CAD CRDT assumption

Kearne does not assume arbitrary source/function changes commute. CRDTs may be used for truly commutative ancillary state such as presence or draft text, but project convergence uses revision identity and source/schema-aware merge unless proven otherwise.

### COL-005 — Same merge semantics

Online conflicts and offline branch merges use the same semantic merge engine and descriptors. Cloud code does not implement a second conflict policy.

## 4. Presence and ephemeral state

Presence, cursors, transient selections, active view, and typing indicators are expiring session messages, not project revisions. They use occurrence/semantic references plus scene generation where meaningful and tolerate loss/reordering.

Comments/review items are durable semantic entities or associated service records with revision-pinned attachments and topology repair behavior.

## 5. Server boundary

The server provides authenticated project membership, immutable object storage, branch/version compare-and-swap, event notification, policy enforcement, quotas, audit, and optional compute dispatch.

### COL-006 — Server does not redefine documents

Shared server libraries validate schemas/digests/permissions, but authoritative engineering semantics remain the headless Kearne core. Server and desktop negotiate evaluator/schema compatibility.

### COL-007 — Zero-trust artifact handling

Uploaded artifacts are untrusted, size-limited, digest-verified, scanned under their artifact-type policy, and decoded only in isolated workers. A valid digest proves identity, not safety.

## 6. Security and privacy

- Encryption in transit is mandatory.
- Encryption at rest, tenant isolation, regional storage, backups, deletion, retention, and enterprise keys require explicit service policies.
- Role/permission changes and released-reference updates are audited.
- Clients do not trust server-supplied actor IDs or branch updates without authenticated authorization context.
- Project content is never used for model training or telemetry without a distinct explicit agreement.

## 7. Conflict and offline UX

Users can inspect local head, remote head, common base, semantic diff, required artifact transfers, and conflicts before merge. Sync errors do not block local save. Deleted remote projects enter a recoverable local state rather than deleting local files automatically.

### COL-008 — Monotonic acknowledgement

A client distinguishes local durable, uploaded, server-accepted, and visible-to-team states. UI never labels an edit “synced” before the relevant remote branch/reference acknowledgement.

## 8. Verification strategy

A deterministic network simulator runs multiple client/server state machines with generated edits, partitions, retries, duplicate messages, reordering, dropped events, expired credentials, permission changes, server failover, and offline compaction.

Properties include:

- immutable objects with the same ID always have the same verified bytes;
- acknowledged branch updates are linearizable per branch reference;
- clients converge after all non-conflicting operations and messages are delivered;
- conflicting changes are preserved in reachable revisions;
- no offline revision disappears due to remote rejection;
- permission revocation prevents future remote mutation without corrupting local state;
- partial artifact transfer is never published;
- presence loss cannot affect canonical history.

The simulator scales client count and history length through parameters, while a bounded real-service integration suite validates transport/auth/storage adapters.

## 9. Open decisions

- **COL-OPEN-001:** Service deployment, tenancy, data-region, and business model.
- **COL-OPEN-002:** Authentication/SSO and organization policy architecture.
- **COL-OPEN-003:** Comment storage in project history versus collaboration service.
- **COL-OPEN-004:** End-to-end encryption feasibility versus server-side compute/search.
- **COL-OPEN-005:** Real-time editing granularity beyond revision commits.

## 10. Definition of done

Collaboration is implemented when deterministic multi-client simulations converge without loss, server faults do not block local work, semantic merge is shared with desktop workflows, and an independently reviewed security/privacy design covers the deployed service.
