# ADR-0013: Canonical Content Encoding

- **Status:** Accepted; revision-identity clauses superseded by [ADR-0021](0021-git-project-packages.md)
- **Date:** 2026-08-19
- **Related:** `DOC-001`, `DOC-004`, `DOC-OPEN-001`, `CMD-005`, [ADR-0012](0012-content-addressed-revisions.md)

## Context

Content trees, entity values, project roots, and revision envelopes need stable 256-bit identities. Protobuf serialization is not canonical.

## Decision

Use Kearne Canonical Encoding v1 (`KCE1`), a closed binary encoding owned by each domain type:

- a domain and version prefix precedes every hashed object;
- field order is fixed by its versioned encoder;
- unsigned integers use minimal base-128 encoding;
- byte and UTF-8 strings use a length prefix and exact bytes;
- optional values carry an explicit presence byte;
- IDs use network-order bytes and digests include algorithm plus bytes;
- maps and sets sort by canonical key bytes and reject duplicate keys;
- floating values reject non-finite values and normalize negative zero before big-endian IEEE-754 encoding.

Opaque payloads are length-prefixed exact bytes. A process may inspect a known payload through its registered schema, but loading or hashing never rewrites an unknown payload.

Use the official portable BLAKE3 1.8.5 C implementation in derive-key mode with a hard-coded context per object kind. Digests are 256 bits and named `blake3`. Content blobs hash exact bytes; trees, entities, project roots, and revisions hash their KCE bytes. Persist KCE version and digest algorithm so a later algorithm can coexist without rewriting existing IDs.

## Consequences

Revision identity is independent of map iteration, host endianness, locale, Protobuf output, and process layout. Encoders are small explicit functions; changing meaning requires a new encoding version. Decode limits and structural validation run before publication.

## Alternatives rejected

- Deterministic Protobuf is not canonical across builds or languages.
- Deterministic CBOR adds a second general schema and parser without removing domain encoders.
- Handwritten SHA-256 or BLAKE3 would make cryptographic maintenance Kearne's responsibility.

## Evidence

The official [BLAKE3 implementation](https://github.com/BLAKE3-team/BLAKE3) provides the pinned portable C API and domain-separation mode. Generated ordering, round-trip, mutation, and corruption properties gate the production encoder.
