# ADR-0021: Git Project History and Portable Packages

- **Status:** Accepted
- **Date:** 2026-08-20
- **Supersedes:** [ADR-0012](0012-content-addressed-revisions.md) for revision identity and durable graph storage
- **Related:** `PROD-OPEN-003`, `CMD-009`, `PST`, `VER`, `COL`

## Context

Users need one `.kearne` file that moves without sidecars, retains full history, works offline, and can later synchronize with a local or hosted Git repository. A custom SQLite revision store duplicates Git's object graph, references, branches, merges, rollback, deduplication, and transport while making those histories harder to inspect or exchange.

## Decision

Git is Kearne's only durable project-history graph. Each accepted engineering transaction creates one Git commit whose tree contains the complete canonical project state and whose parents encode history. `RevisionId` is an algorithm-qualified Git commit object ID. Kearne's project-root digest remains independent validation, not a second revision identity.

The open project uses an application-controlled local repository and requires no remote. Kearne maps workspaces, branches, versions, releases, saved heads, and retention roots to Git refs. Semantic commands, diff, and merge remain Kearne behavior; raw Git file merging cannot publish engineering state.

A `.kearne` file is a ZIP64 package containing a bounded integrity manifest, a complete prerequisite-free Git bundle, and an optional preview. Save packages all retained refs and verifies the result before replacement. It does not commit the package into itself or create an empty commit.

SQLite is removed. Adding a database later requires a measured need and a separate non-authoritative boundary.

## Consequences

- One graph drives undo, redo, branching, merge, rollback, AI alternatives, releases, and synchronization.
- Full project history moves with one file and remains usable without a server.
- Local paths and hosted services use the same Git transport model.
- Kearne must ship or link a supported Git implementation; it cannot assume Git is installed.
- Full-package publication and large binary history require explicit performance gates.
- Credentials, user preferences, jobs, and disposable caches remain outside project history.
- Shared-file editing uses local repositories, destination-change detection, and safe replacement; the package is not a live multi-writer repository.

## Alternatives rejected

- SQLite as project history: duplicates Git and is unsuitable as the direct multi-writer interface to arbitrary shared storage.
- Custom revision DAG plus Git export: creates two identities and synchronization paths.
- Commit `.kearne` files to an internal repository: recursively embeds history and causes pathological growth.
- Directory-only projects: violate the one-file portability contract.

## Evidence required

The Git/package technology gate must prove transaction latency, branch/ref correctness, semantic merge integration, complete bundle recovery, exact binary preservation, fault-safe Save, large-history behavior, shared-storage conflicts, and Windows/Linux portability before desktop Save/Open is enabled.

[Git bundle format](https://git-scm.com/docs/bundle-format) defines a file containing refs and pack data. [Git clone](https://git-scm.com/docs/git-clone) supports local repository paths and complete bundles for clone/fetch. A bundle is package content, not a push endpoint.
