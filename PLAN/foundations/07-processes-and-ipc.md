# Process Ownership and IPC

- **Status:** Proposed; process-boundary spike required
- **Requirement prefix:** `IPC`
- **Depends on:** [System architecture](../01-system-architecture.md), [evaluation](03-evaluation-and-jobs.md), [persistence](06-persistence-and-recovery.md)
- **Unblocks:** geometry isolation, Python, import/export, simulation, Codex harness

## 1. Purpose

Keep UI and project state alive when third-party geometry, parsing, Python, or solver code blocks or crashes. Define one reusable worker protocol so every subsystem does not invent its own lifecycle, progress, cancellation, artifact transfer, and failure semantics.

## 2. Ownership

### IPC-001 — Project coordinator ownership

Exactly one project coordinator owns the writable workspace heads, transaction engine, durable store connection, scheduler, and artifact index. Workers cannot write canonical project state.

### IPC-002 — Worker isolation

A worker owns all live objects created by its third-party runtime, including `TopoDS_Shape`, Python interpreter objects, solver meshes, and parser state. Such objects never cross the process boundary.

### IPC-003 — Immutable transfer

Processes exchange versioned messages and immutable artifact bytes/handles. A process-local handle is scoped by worker instance and cannot be persisted or sent to another worker as identity.

## 3. Worker classes

```text
Geometry worker  OCCT feature evaluation, healing, exact queries
Import worker    untrusted/complex format parsing, possibly OCCT-backed
Python worker    SDK/build123d/user code under capabilities
Solver worker    future assembly, meshing, and simulation backends
Utility worker   thumbnails or isolated conversions when justified
Agent runtime    Codex app-server under a protocol-specific adapter
```

A worker executable may host multiple compatible roles, but protocol roles and resource policies remain explicit.

### IPC-004 — Reusable supervisor

All workers use one supervisor implementation for launch, handshake, health, job dispatch, cancellation, progress, logging correlation, quotas, crash classification, restart limits, and shutdown.

The agent runtime reuses process launch, environment, health, logging, quota, restart, and shutdown policy. Its native app-server thread/turn protocol remains inside the Codex adapter and is not translated into fake geometry-worker messages.

## 4. Control and bulk planes

### Control plane

The control protocol carries bounded messages:

```text
Hello / capabilities / protocol versions
StartJob / accepted / rejected
Progress
CancelJob
Complete / fail
Heartbeat
ReleaseLease
Shutdown
```

### Bulk plane

BREP, meshes, source imports, and solver results use immutable artifact transport:

- content-addressed files or mapped chunks for durable/reusable artifacts;
- broker-created shared memory for low-latency transient transfer;
- bounded inline bytes only below a configured threshold.

### IPC-005 — No giant control messages

The protocol rejects bulk payloads above the negotiated inline limit and requires an artifact descriptor. Message decoding has explicit depth, count, and byte limits.

### IPC-006 — Brokered handles

Only the coordinator/artifact broker creates shared-memory regions and durable artifact publication targets. Workers receive least-privilege handles and cannot choose arbitrary host paths.

## 5. Geometry artifact flow

```text
evaluation key + parameters + upstream artifact handles
    -> geometry worker
    -> load/reuse worker-local OCCT shapes
    -> evaluate
    -> serialize exact BREP artifact privately
    -> tessellate or publish topology/mesh artifacts
    -> broker verifies and atomically publishes
    -> completion references immutable artifact digests
```

### IPC-007 — Serialization boundary

Chained geometry is transferable through a versioned exact-geometry artifact format compatible with the pinned OCCT evaluator. Worker affinity and process-local caches may avoid repeated deserialization but cannot be required for correctness.

### IPC-008 — Python compatibility

The build123d/OCP environment is pinned to a geometry artifact interchange version proven compatible with the C++ OCCT adapter. Mismatched environments fail during handshake rather than attempting undefined BREP exchange.

## 6. Protocol compatibility

### IPC-009 — Handshake before work

Worker and coordinator negotiate protocol major/minor version, role capabilities, schema set, evaluator fingerprint, artifact formats, resource limits, and build identity before accepting jobs.

Major incompatibility prevents dispatch. Minor evolution follows additive-field and unknown-field rules in the Engineering API plan.

### IPC-010 — Correlation

Every message includes worker-instance ID, job ID, request/revision/generation context as applicable, and a monotonically increasing per-job sequence for progress/log ordering.

## 7. Cancellation and faults

### IPC-011 — Cancellation escalation

The supervisor sends cooperative cancellation, waits the role-specific grace period without blocking UI, then may terminate a dedicated worker. Shared workers are terminated only when collateral job policy permits; risky uninterruptible jobs should receive dedicated processes.

### IPC-012 — Bounded restart

Crash restart uses exponential backoff and a per-evaluation-key/process-role circuit breaker. Repeated deterministic crashes quarantine the key and emit a diagnostic rather than loop.

### IPC-013 — Lease recovery

Worker-instance-scoped leases expire on confirmed process death. The broker cleans unpublished temporary artifacts and shared memory without affecting published immutable artifacts.

### IPC-014 — No synchronous process dependency

The UI thread never waits for worker launch, heartbeat, response, shutdown, or pipe drain. Loss of a worker updates asynchronous operation state.

### IPC-015 — Protocol-native adapters

An external runtime with a maintained versioned protocol retains that protocol behind its adapter. The supervisor shares lifecycle policy, not a lowest-common-denominator wire schema. Canonical Kearne commands and artifact contracts remain unchanged.

## 8. Security

- Workers are spawned from verified Kearne installations or approved plugin packages.
- Local endpoints use OS access control and per-launch unguessable authentication material.
- Incoming messages are treated as untrusted even from child processes.
- Python and import workers receive stronger filesystem/network restrictions than trusted native geometry workers.
- Environment variables, inherited handles, current directory, and search paths are explicitly constructed.
- Logs and crash reports redact project content unless opted in.
- Codex receives an explicit working directory, configuration root, tool set, sandbox, approval policy, network policy, and brokered image paths.

## 9. Verification strategy

One transport-independent supervisor conformance suite runs against in-memory loopback, OS pipes/sockets, and fault-injecting transports. It generates fragmentation, duplication where transport permits, delay, reordered progress, malformed lengths, cancellation races, worker hangs, worker crashes, coordinator crashes, quota exhaustion, and protocol skew.

Required properties:

- at most one terminal outcome is published per job;
- stale worker instances cannot publish after restart;
- no partial artifact is reachable;
- UI-facing operations remain asynchronous;
- leases release within the configured post-mortem safety window;
- malformed messages cannot allocate unbounded memory or crash the coordinator;
- a fake worker can exercise every job lifecycle without OCCT or Python.

## 10. Open decisions

- **IPC-OPEN-001:** Local transport/IDL selection; Protobuf over local sockets/pipes is the proposed baseline.
- **IPC-OPEN-002:** Exact BREP serialization and compatibility guarantees for pinned OCCT/OCP builds.
- **IPC-OPEN-003:** Geometry pool topology: one job per process, warm shared workers, or risk-based hybrid.
- **IPC-OPEN-004:** Cross-platform sandbox primitives and degraded guarantees on Windows/Linux.
- **IPC-OPEN-005:** Shared-memory implementation and maximum mapped sizes.

## 11. Definition of done

The boundary is implemented when fake and real workers pass the same supervisor suite, process termination cannot damage canonical state, large artifacts do not traverse the control plane, and protocol/version mismatch fails safely on both platforms.
