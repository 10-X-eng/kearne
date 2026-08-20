# ADR-0010: Stable Semantic Identities Use UUIDv7

- **Status:** Accepted
- **Date:** 2026-08-19
- **Related:** `DOC-002`, `DOC-003`, `CMD-003`, `DOC-OPEN-001`

## Context

Records, model functions, calls, outputs, requests, and other semantic objects need compact collision-resistant identities created without coordination. Their byte and text forms cross APIs, revisions, branches, processes, and platforms.

## Decision

Stable semantic IDs use RFC 9562 UUIDv7 in network byte order and canonical lowercase `8-4-4-4-12` text form. The project coordinator supplies the millisecond timestamp and 74 random bits; tests and replay supply both explicitly. Typed wrappers prevent interchange between ID domains.

Revision IDs, content digests, artifact digests, and cache keys are separate types and do not use this decision. UUID ordering is an indexing optimization, not engineering chronology, authorization, provenance, or a source of creation metadata.

## Consequences

- IDs are 16-byte values with a standard cross-platform representation.
- Time ordering improves locality without a central allocator.
- Generated IDs reveal an approximate creation time; IDs are public identifiers, never secrets.
- Clock rollback and same-millisecond generation require a coordinator policy before runtime allocation is implemented.
- Parsing rejects other UUID versions and variants at typed semantic-ID boundaries.

## Alternatives rejected

- UUIDv4: standardized and private, but gives up time locality while retaining the same size.
- ULID: sortable, but introduces a separate 128-bit text standard when UUIDv7 now covers the requirement.
- Database integers: require central allocation and do not survive offline branches or merge cleanly.
- Content-derived IDs: identity would change with content and cannot represent stable mutable semantic objects.

## Evidence

[RFC 9562](https://www.rfc-editor.org/rfc/rfc9562.html) defines UUIDv7 as a 48-bit Unix-millisecond timestamp plus 74 random bits and recommends it over UUIDv1 and UUIDv6 where possible. Generated round-trip, version/variant rejection, and time-order properties run under change, nightly, release, seed, and shard profiles.
