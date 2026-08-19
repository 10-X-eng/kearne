# ADR-0005: Worker and Immutable Artifact Boundary

- **Status:** Proposed
- **Date:** 2026-08-19
- **Related:** [Processes and IPC](../foundations/07-processes-and-ipc.md), [Evaluation](../foundations/03-evaluation-and-jobs.md)

## Context

OCCT operations, import parsers, Python/OCP, and solvers can block, consume unbounded resources, or crash. Their live objects cannot be shared safely across processes or persisted as identity.

## Decision

The project coordinator owns semantic state. Risky runtimes execute in supervised workers and exchange bounded control messages plus immutable artifact handles. Live third-party objects remain worker-local. Correctness cannot depend on worker-local caches or affinity.

## Consequences

Exact geometry needs a pinned artifact interchange. Serialization adds cost, mitigated by affinity and worker-local cache. Workers cannot commit document changes. Shared supervisor, lease, cancellation, and fault semantics apply to all worker roles.

## Evidence required

SPIKE-003 and SPIKE-008 must establish artifact compatibility, process topology, cancellation, performance, and enforceable isolation before acceptance.
