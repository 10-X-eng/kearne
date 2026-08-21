# Git Project Storage and Recovery

- **Status:** Accepted architecture; implementation pending
- **Requirement prefix:** `PST`
- **Depends on:** [Document model](01-document-model.md), [commands and revisions](02-commands-transactions-revisions.md)
- **Decision:** [ADR-0021](../adr/0021-git-project-packages.md)
- **Unblocks:** distributable MVP, versioning, collaboration

## 1. Purpose

Make one `.kearne` file carry a complete project and its retained history without requiring a server, sidecar, or database. Git owns durable history. Kearne owns engineering commands, validation, semantic diff, merge, packaging, and recovery.

## 2. Human contract

### PST-001 — One portable file

A saved project is one regular `.kearne` file. Copying it to a compatible Kearne installation MUST retain every branch, version, retained revision, canonical source byte, typed record, embedded input, and explicitly retained result.

### PST-002 — Exact bytes

Native source, imports, templates, and other irreplaceable inputs are stored as exact Git blobs. Package entries record byte length and a cryptographic digest. Open rejects or recovers corruption; it never silently substitutes content.

### PST-003 — No hidden dependency

A saved project MUST NOT require a working directory, recovery directory, cache, remote, or absolute local path. Imported files are embedded by default. An explicitly linked file remains external and makes the portability status visibly incomplete.

### PST-004 — Local first

Creating, editing, saving, branching, merging, and restoring a project require no remote. A remote is optional and may be a local path, shared Git repository, or network service. Credentials remain outside the project.

## 3. Git repository

Kearne maintains an application-controlled local Git repository while a project is open.

### PST-005 — One history graph

Each accepted engineering transaction creates one Git commit. Its tree is the complete materialized project state; its parents are the revision parents. `RevisionId` identifies that Git commit. Kearne MUST NOT persist a parallel revision graph.

### PST-006 — Canonical commit tree

The tree contains native build123d/Python source, function declarations, typed engineering records, project metadata, embedded inputs, and retained project artifacts. A fixed transaction record contains the command types, normalized mutations, actor, origin, schema set, and project-root digest for that commit. Disposable caches and user preferences are excluded.

### PST-007 — Git references

Branches use `refs/heads/*`. Immutable versions and releases use protected tags or Kearne refs. Workspaces, saved heads, redo futures, and retention roots use reserved `refs/kearne/*` names. Every revision promised to the user is reachable from a packaged ref; reflogs alone do not satisfy retention.

### PST-008 — Durable acknowledgement

Kearne acknowledges a transaction only after its blobs, tree, commit, and compare-and-swap reference update meet the durable-write policy. Failed reference updates may leave unreachable objects but MUST NOT move a visible head.

### PST-009 — No history noise

One completed gesture or command transaction creates one commit. Preview samples do not. Save packages the current committed state and moves the saved ref; it does not create an empty commit.

## 4. `.kearne` package

The initial package format is ZIP64 with fixed entries:

```text
manifest.json       bounded format, project, active-ref, and compatibility data
repository.bundle   complete Git bundle with no external prerequisites
preview.png         optional disposable preview
```

The manifest records each package entry's byte length and cryptographic digest.

The Git bundle is stored without redundant archive compression. Canonical and retained project data lives inside the repository, not beside it in the outer package.

### PST-010 — Complete bundle

The bundle includes every retained branch, version, release, workspace recovery root, and object reachable from them. A partial or prerequisite-dependent Git bundle is not a valid `.kearne` project.

### PST-011 — Verify before publish

Before reporting Save complete, Kearne verifies package limits and digests, verifies the Git bundle, imports it into a clean repository, checks object reachability, checks the active ref and project identity, and validates the materialized head through the document model.

### PST-012 — Platform-neutral paths

Repository paths use a canonical separator and reject traversal, absolute paths, case-fold collisions, Windows reserved names, and names that cannot round-trip on a supported platform. Project semantics do not depend on file times, host permissions, or symlinks.

## 5. Save and shared storage

### PST-013 — Safe Save

Save builds a complete package at a private temporary path, verifies it, flushes it under the platform durability policy, confirms the destination has not changed since open or the last Save, and replaces the destination through the supported atomic path. Failure leaves the prior file and local repository recoverable.

### PST-014 — Shared-file conflict

Projects opened from shared, removable, or cloud-synchronized storage edit through a local repository. If the source file changes, Kearne MUST NOT overwrite it. The user receives a merge workflow or Save Copy. A `.kearne` file is a portable package, not a multi-writer database.

### PST-015 — Save As and copies

Save As changes location while preserving project identity and full history. Duplicate/Fork creates a new project identity through an explicit command and retains ancestry according to the chosen workflow. Export Snapshot is the only ordinary operation that discards history.

### PST-016 — Honest completion

The UI distinguishes locally committed, packaged, published to the chosen file, and synchronized to a remote. It MUST NOT display Saved or Synced before the corresponding verification succeeds.

## 6. Recovery and migration

### PST-017 — Continuous local recovery

Accepted transactions are durable in the local repository before package Save. Reserved refs retain unsaved heads and redo futures. After abnormal termination, Kearne compares the packaged saved ref with local recovery refs and offers recovery without modifying the package.

### PST-018 — Non-destructive migration

Opening an older supported format preserves the original file, imports it into a new local repository, applies monotonic migration commits, validates unknown data, and writes a replacement only after explicit Save. Unsupported required features open read-only when possible.

### PST-019 — No execution during inspection

Package and repository validation parse bounded data without executing project Python, loading native plugins, or decoding derived geometry in the UI process.

## 7. Remote Git

### PST-020 — Optional remotes

Remote configuration is optional. Kearne may fetch and push through local filesystem, SSH, HTTPS, or later supported transports. The `.kearne` package itself is not a push target; the local repository exchanges Git objects and refs.

### PST-021 — Engineering merge

Git supplies commit ancestry, merge bases, objects, and reference transport. Kearne performs source-aware and schema-aware three-way merge, validates the staged project, and creates the two-parent merge commit. A clean textual merge does not prove valid geometry.

## 8. Derived data

BREP, tessellation, parsed-source indices, thumbnails, and solver outputs are disposable only when their inputs and compatible evaluator are available. User-retained results and last-known-good fallback geometry are project artifacts and travel in Git history. Per-user caches remain outside `.kearne` and may be deleted without losing intent.

## 9. Verification

One generated state machine drives the reference model and Git implementation through commands, undo, redo, branches, tags, merges, Save, Save As, reopen, remote fetch/push, and recovery. Profiles scale history, tree size, binary size, branch count, fault point, and concurrent readers.

Fault tests interrupt object writes, ref updates, bundle creation, archive writes, flush, rename, shared-file comparison, and reopen. Portability tests move packages between supported Windows and Linux filesystems and retain format fixtures for future macOS validation. External Git verifies repository interoperability.

Properties assert one-to-one transaction/commit correspondence, immutable ancestors, exact blob bytes, complete retained reachability, deterministic semantic state, no silent overwrite, and preservation of the previous valid package after every failed Save stage.

## 10. Performance gates

- A small accepted transaction targets p95 below 30 ms on a recommended local SSD.
- Editing and recovery never wait for shared-storage latency.
- Package creation runs outside the UI thread and reports measured progress.
- Open cost depends on the current tree and required validation, not replay of the full history.
- Package size tracks reachable unique Git objects plus bounded envelope overhead.

Full-bundle rewrite, large binary history, antivirus contention, and network publication require measured Windows/Linux budgets before Save/Open is enabled. If full ZIP rewrite misses the budget, the package envelope may adopt verified incremental bundle segments without changing Git history or the one-file contract.

## 11. Open decisions

- **PST-OPEN-001:** Embedded Git implementation and redistribution choice.
- **PST-OPEN-002:** ZIP64 library, deterministic encoding, and size limits.
- **PST-OPEN-003:** Git object format and remote compatibility policy.
- **PST-OPEN-004:** Retention, pruning, and package compaction policy.
- **PST-OPEN-005:** Embedded native-plugin and cross-platform dependency policy.
- **PST-OPEN-006:** Encryption and signing.

## 12. Definition of done

Save/Open remains disabled until generated history and fault suites prove exact portable reconstruction, full retained reachability, safe shared-file conflict behavior, bounded performance, and recovery on supported Windows and Linux filesystems.
