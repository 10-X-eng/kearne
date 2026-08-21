# Persistence, History, and Recovery

- **Status:** In progress; physical storage acceptance gates remain open
- **Requirement prefix:** `PST`
- **Depends on:** [Document model](01-document-model.md), [commands and revisions](02-commands-transactions-revisions.md)
- **Unblocks:** distributable MVP, versioning, collaboration

## 1. Purpose

Persist the source tree, function graph, typed engineering records, and immutable revision history atomically while keeping reproducible geometry and render products disposable. Saving and crash recovery are defined behavior, not incidental database effects.

## 2. Proposed physical model

The baseline to validate is:

- one SQLite-backed `.kearne` project file for the content-addressed source tree, function contracts and calls, typed records, schemas, revisions, mutation batches, workspace pointers, and chunked artifacts;
- a content-addressed per-user cache outside the portable project for reproducible BREP, meshes, thumbnails, and other derived artifacts;
- optional packaged derived artifacts for fast transfer, each independently validated and discardable;
- SQLite WAL only while the project is open, checkpointed through supported copy/export operations.

This provides transactional durability and a portable primary file without rewriting a ZIP container on every edit. The storage prototype must validate large-project behavior before acceptance.

Implemented: bounded canonical mutation, revision, and project-checkpoint decoders plus a pinned SQLite adapter. One transaction stores source bytes, a content-addressed revision, the verified head checkpoint, and the head pointer. Loading verifies database references, checkpoint integrity, project Merkle roots, revision identity, and source digests before publication. Desktop Save/Open stays disabled until Engineering Service commits can be journaled atomically through this adapter.

## 3. Data classification

### PST-001 — Canonical project data

The project MUST retain source trees, function contracts and calls, typed records, revision parents, normalized mutations, schema identities, durable workspace/branch heads, actor provenance, and migrations needed to reconstruct any retained revision.

### PST-002 — Irreplaceable artifacts

Imported source bytes, native model source, embedded standards/templates, and other irreplaceable inputs are durable artifacts. Native model source participates directly in the canonical content tree. None may be labeled a cache.

### PST-003 — Reproducible caches

Function-output BREP, tessellation, mass properties, parsed-source indices, recognition metadata, thumbnails, and solver outputs are caches only when their inputs and compatible evaluator are available. Removing caches MUST preserve user intent and produce at most recomputation or an unavailable-evaluator diagnostic.

### PST-004 — Retained fallback

Model functions, plugins, imports, and legacy records MAY retain last-known-good BREP as a fallback artifact. It is marked with source revision, evaluator fingerprint, and stale/read-only state; it does not prove that current source evaluated.

## 4. Transactional durability

### PST-005 — Commit acknowledgement

A transaction is acknowledged as durable only after its revision record, mutation batch, referenced new source-artifact chunks, request-id outcome, and head update satisfy the configured durable database commit. UI may optimistically preview pending state but MUST distinguish it from acknowledged state.

### PST-006 — Atomic crash result

After process termination or power-failure simulation, recovery observes either the prior acknowledged head or the complete next acknowledged head. A revision never points to partial mutations or missing required source chunks.

### PST-007 — Save semantics

Because accepted edits are journaled, `Save` marks a user-visible save point and requests a WAL checkpoint/flush; it is not the only durability event. `Save As` creates a transactionally consistent copy with a new project identity only when requested by the copy semantics.

### PST-008 — Single writer

An open project has one coordinator holding the writer lease. Additional local opens are read-only or connect to that coordinator. Filesystem locks are advisory evidence, not the sole corruption defense.

## 5. Revision storage and materialization

The system stores immutable mutation batches plus periodic materialized content-tree and record-table checkpoints.

### PST-009 — Bounded open cost

Opening a project MUST require replay from a bounded recent checkpoint, not the entire lifetime history. Checkpoint frequency is selected by measured size/open-time policy.

### PST-010 — Verify before publish

Loaded records are checksummed/digested and structurally validated before publishing a snapshot. Derived indices are rebuilt when their source revision or digest does not match.

### PST-011 — History-preserving compaction

Compaction may repack tables and deduplicate chunks but MUST NOT change revision IDs, semantic digests, parent relationships, or retained version reachability. Destructive history pruning is an explicit user/admin operation outside MVP.

## 6. Artifact store

Artifacts use digest-addressed immutable chunks with type, byte length, creator/evaluator fingerprint, compression, and integrity metadata.

### PST-012 — Atomic artifact publication

Writers create temporary private content, finalize and verify its digest, then publish atomically. Partial content is never discoverable by the durable artifact index.

### PST-013 — Leases and garbage collection

Running jobs and open snapshots hold leases. Garbage collection removes only unreferenced cache artifacts older than a safety window. Source artifacts are collected only through an explicit semantic mutation and revision-retention policy.

### PST-014 — Untrusted cache

Externally copied cache content is treated as untrusted bytes. Type-specific validators and size limits run before decoding or mapping it into a worker.

## 7. Schema migration

### PST-015 — Forward migration

Opening an older supported project creates a backup/recovery point, migrates through registered monotonic steps inside a database transaction or new-file copy, validates the result, and only then makes it writable.

### PST-016 — No in-place unknown rewrite

Unknown entity payloads are preserved. A migration MUST NOT parse and reserialize data it does not own merely to update the surrounding container.

### PST-017 — Format compatibility manifest

The project header records container version, schema set, minimum reader, feature/plugin requirements, numerical profile, and creation/last-write application builds.

Downgrade export, if offered later, is explicit and reports lost capabilities before writing.

## 8. Recovery and salvage

On abnormal termination Kearne:

1. validates SQLite/WAL state through the supported database recovery path;
2. finds the last complete durable head;
3. validates revision reachability and required source artifacts;
4. quarantines invalid records rather than loading unsafe payloads;
5. offers read-only recovery/export when full restoration is impossible;
6. retains the original file until the user confirms a repaired copy.

Recovery MUST NOT overwrite the only copy of a corrupt project.

## 9. Verification strategy

- Run the command state machine through an instrumented persistence port.
- Inject I/O errors, full disk, permission changes, truncation, checksum mismatch, worker death, application death, and termination at every defined commit stage.
- Use a fault-injecting SQLite VFS in dedicated test builds rather than wall-clock kill scripts alone.
- Generate long revision DAGs, checkpoints, migrations, compaction, save-as, and cache deletion.
- Open every retained schema fixture with the newest reader; use versioned schema builders for every representable fixture.
- Fuzz project headers, message lengths, compressed chunks, and source-artifact decoders under strict resource limits.

Properties assert atomic heads, reachability, idempotent reopen, preservation of unknown data, source/cache classification, and semantic equivalence after checkpoint/compaction.

## 10. Performance budgets

- Acknowledged small semantic transaction: p95 target below 30 ms on recommended local SSD with the durability profile enabled.
- Opening the MVP benchmark project from a valid checkpoint: p95 below 500 ms excluding geometry recomputation.
- Project metadata memory should grow approximately with live paths, functions, calls, records, and the loaded revision window, not total historical payload.

These are provisional until the storage prototype records filesystem-specific results.

## 11. Open decisions

- **PST-OPEN-001:** Accept or reject SQLite single-file baseline after WAL, large BLOB, antivirus, network-folder, and crash tests.
- **PST-OPEN-002:** Chunking/compression algorithms and maximum embedded source size.
- **PST-OPEN-003:** Project encryption and OS key-storage policy.
- **PST-OPEN-004:** Exact checkpoint and history-retention policy.
- **PST-OPEN-005:** Behavior on unsupported network filesystems and cloud-synced folders.

Current evidence covers generated create/commit/retry/head-move/save-point/reopen behavior, read-only enforcement, stale-head rollback, and rejection of altered checkpoints and source bytes. It does not yet cover process/power interruption at each commit stage, disk-full and permission faults, large BLOB and long-history budgets, Windows filesystems, antivirus interference, network/cloud folders, migrations, salvage, or desktop integration. SQLite is therefore not yet accepted by `PST-OPEN-001`.

## 12. Definition of done

Persistence is implemented when fault injection cannot produce a hybrid acknowledged head, generated histories reopen identically, migrations preserve unknown source and payloads, deleting all caches preserves intent, and recovery passes on supported Windows and Linux filesystems.
