# ADR-0011: Protobuf Engineering API

- **Status:** Accepted
- **Date:** 2026-08-19
- **Related:** `TECH-001`, `API-004`, `API-005`, `API-011` through `API-014`

## Context

Kearne needs one bounded schema to generate C++ and Python wire types, compatibility checks, and AI metadata. The semantic core must retain strong types.

## Decision

Use Protobuf Editions 2024. Pin compiler, C++ runtime, and Python runtime from [`dependencies.lock.json`](../../dependencies.lock.json); C++ generator and runtime versions match exactly. Binary Protobuf is the compatibility format. ProtoJSON is limited to validated CLI and tool edges. Generated wire values cross one handwritten strong-domain conversion boundary.

Evolution rules:

- Never reuse field numbers, field names, enum numbers, or stable descriptor names; reserve removals.
- Minor versions add optional fields or open-enum values only. A changed semantic default or required value creates a new schema version.
- Preserve unknown binary fields. Do not route preservation through JSON or field-by-field copies.
- Adding a oneof member is wire-safe, but an older receiver treats an unknown member as unsupported rather than as an empty request.
- Protobuf serialization is not canonical and is never hashed as document or revision identity.
- Regenerate all bindings and metadata on every dependency update; compatibility and fuzz suites must pass before changing the lock.

## Consequences

Descriptors are the source for names, versions, bounds, permissions, and AI schemas. Protobuf remains behind `Kearne::ApiWire`; document and geometry modules do not expose generated types.

## Alternatives rejected

Cap'n Proto lacks a maintainer-reviewed Python implementation. FlatBuffers supports C++ and Python but its Python runtime cannot parse schemas or JSON without the C++ parser. Both increase Kearne-owned binding work for this control API.

## Evidence

[`prototype/001-schema-binding-pipeline`](../../prototype/001-schema-binding-pipeline) exercises C++, Qt, binary IPC, generated Python, CLI JSON, AI metadata, evolution, bounds, and fuzzing. The production [`api/schema`](../../api/schema) replaces that code with a locked host compiler, generated bindings/registry, shared validator, descriptor conformance, and auto-enrolled fuzz surface.

Upstream contracts: [version support](https://protobuf.dev/support/version-support/), [wire evolution and unknown fields](https://protobuf.dev/programming-guides/proto3/), [non-canonical serialization](https://protobuf.dev/programming-guides/serialization-not-canonical/).
