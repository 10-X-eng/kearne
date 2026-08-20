# ADR-0016: In-Process Engineering API

- **Status:** Accepted
- **Date:** 2026-08-19
- **Related:** `API-002`, `API-005`, `API-OPEN-002`, [ADR-0004](0004-one-engineering-api.md)

## Context

The desktop, CLI, Python, AI bridge, plugins, and replay need identical command/query behavior. A separate C++ editing facade would create another contract and validation path.

## Decision

In-process adapters submit generated Protobuf command/query envelopes to the same service used by transported clients. Adapters construct boundary values; QML never receives mutable wire objects. The service performs generic wire validation, permission checks, strong domain conversion, normalization, and commit once.

Immutable domain snapshots and projections remain direct typed C++ values inside the core. Protobuf is an API boundary, not the document model or canonical encoding.

## Consequences

Adapter parity is structural and every registered operation shares one conformance surface. Internal hot-path projections avoid serialization. Schema changes regenerate adapters and metadata instead of changing a handwritten facade in parallel.

## Alternatives rejected

- A direct mutable C++ project facade bypasses the command and compatibility model.
- Serializing every internal read wastes work and leaks transport types into domain code.
