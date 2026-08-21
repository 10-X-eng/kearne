# ADR-0014: Persistent Project State

- **Status:** Accepted
- **Date:** 2026-08-19
- **Related:** `ARCH-004`, `ARCH-OPEN-002`, `DOC-001`, `CMD-006`

## Context

Published snapshots must be immutable and cheap to retain while edits scale beyond whole-table copies. The public document API must not expose a container dependency or storage layout.

## Decision

Use Immer 0.9.1 CHAMP maps for in-memory content, record, function, call, and artifact tables. Each table partitions stable-key hashes into 1,024 structurally shared buckets and maintains a persistent Merkle tree over their canonical digests. A point edit rehashes one bounded bucket and ten parents. Staged mutations create new roots without changing published values. The implementation remains private behind `ProjectState`, whose copies are constant-time immutable handles.

Canonical encoders sort keys and never depend on CHAMP iteration or hash order. Durable Git trees and commits remain a repository-adapter concern. The in-memory representation cannot leak into public types or KCE bytes.

## Consequences

Point edits copy only affected trie paths, readers require no table locks after publication, and divergent revisions share unchanged state. Immer is header-only, hidden from installed headers, and pinned under the dependency policy.

## Alternatives rejected

- Copying standard maps on each revision has linear edit cost and retains duplicate table storage.
- Mutable shared maps require reader synchronization and weaken atomic snapshot publication.
- A Kearne-specific persistent trie duplicates mature low-level memory and concurrency code.

## Evidence

[Immer](https://github.com/arximboldi/immer) supplies immutable CHAMP maps without runtime dependencies. Generated long-history tests and benchmarks verify immutability, structural edit scaling, and canonical-order independence in Kearne.
