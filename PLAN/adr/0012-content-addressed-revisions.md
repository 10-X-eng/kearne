# ADR-0012: Content-Addressed Revisions

- **Status:** Superseded by [ADR-0021](0021-git-project-packages.md)
- **Date:** 2026-08-19
- **Related:** `CMD-005`, `CMD-009`, `CMD-011`, `CMD-OPEN-001`

## Context

Revision identity must support Git-like history, replay, integrity checks, and exchange. Semantic entities already use UUIDv7.

## Decision

A revision ID is an algorithm-qualified 256-bit digest of a versioned canonical revision envelope excluding its own ID. The envelope covers parents, transaction and normalized mutations, provenance, schema set, commit time, and project-root digest. All nondeterministic inputs are supplied by the transaction context.

Protobuf bytes are not the canonical encoding. [ADR-0013](0013-canonical-content-encoding.md) defines the hashed envelope and first algorithm. A store finding the same ID with different canonical bytes stops with a collision or corruption diagnostic. Digest algorithms are versioned.

## Consequences

Revision IDs verify content and naturally identify equal histories. UUIDv7 remains the identity for projects, requests, transactions, records, functions, calls, actors, and jobs.

## Alternatives rejected

Random revision UUIDs avoid canonical encoding but provide no content integrity or equality and weaken the requested Git-like model.
