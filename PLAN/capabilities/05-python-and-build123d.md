# Python SDK and build123d

- **Status:** Proposed; sandbox and compatibility spikes required
- **Requirement prefix:** `PY`
- **Depends on:** [Engineering API](../foundations/08-engineering-api.md), [processes](../foundations/07-processes-and-ipc.md), [persistence](../foundations/06-persistence-and-recovery.md)
- **Unblocks:** procedural features, engineering automation, AI-generated scripts

## 1. Purpose

Offer ergonomic Python automation and build123d procedural geometry without embedding Python in the application process or creating a second document model.

## 2. Two Python modes

### Automation session

A user/script client submits normal Engineering API commands and queries. It may create transactions and wait on operation handles subject to permissions.

### Procedural evaluator

A document feature evaluates pinned source code against declared immutable inputs and produces declared artifacts/results. It cannot submit persistent commands, inspect the mutable workspace, or recursively invoke itself.

### PY-001 — Mode separation

Automation and procedural evaluation use distinct capability profiles and SDK entry points. A procedural feature MUST NOT mutate the document as a side effect of evaluation.

## 3. Python SDK

The SDK provides typed wrappers such as:

```python
with project.transaction(base_revision=project.head) as tx:
    sketch = tx.create_sketch(component=component, plane=xy_plane)
    tx.add_rectangle(sketch, width=100 * mm, height=60 * mm)
    feature = tx.create_extrude(profile=sketch.default_profile(), distance=8 * mm)
```

These calls create versioned command envelopes. Python proxy objects hold IDs and client/session context; they are not mutable mirrors of C++ entity objects.

### PY-002 — Revision clarity

Queries and proxies expose the observed revision. Mutating through a stale proxy either submits against its explicit revision and reports conflict or requires an explicit refresh/rebase policy.

### PY-003 — Dimensional values

The SDK exposes quantity types and rejects ambiguous bare numbers where a public command requires a dimension, except APIs with a documented default unit convenience layer.

### PY-004 — Async-native operations

Long operations expose awaitable handles plus synchronous convenience methods usable only off the UI thread. Cancellation maps to the common operation contract.

## 4. Procedural feature schema

```text
ProceduralFeature
  language: python
  source artifact/ref and digest
  entry point
  declared input schema and bindings
  declared output slots
  environment lock/fingerprint
  capability profile
  topology publication mode
```

### PY-005 — Reproducible environment

The evaluator fingerprint includes Python, build123d, OCP/OCCT, Kearne SDK, package lock, platform compatibility class, and source digest. Unpinned imports make a result explicitly non-reproducible and are disabled for durable procedural features by default.

### PY-006 — Declared dependencies

Scripts receive only declared parameter values, semantic geometry artifacts, and approved read-only helper services. Reading arbitrary project state, environment variables, current time, filesystem, or network is prohibited unless declared and capability-approved; such access participates in reproducibility classification.

### PY-007 — Explicit outputs

The entry point returns outputs matching declared slots and types. Printing a shape, mutating a module global, or leaving an object in interpreter memory is not publication.

## 5. build123d exchange

The worker's OCP build must be compatible with Kearne's pinned exact-geometry artifact format. Results pass through artifact validation before publication.

### PY-008 — Procedural topology contract

A procedural feature may:

- publish explicit stable topology labels through a Kearne helper API;
- publish only body-level output identity; or
- be treated as imported/dumb topology.

Kearne MUST NOT promise robust subshape persistence for arbitrary build123d code without labels/provenance. The UI and API expose the chosen capability.

### PY-009 — Native translation is optional and explicit

Recognition or translation of a script result into native features creates a previewable command proposal with confidence. It is never required to execute a procedural feature and never silently replaces its source.

## 6. Runtime and sandbox

- Each untrusted execution uses a fresh or sanitizably pooled worker identity.
- Filesystem starts denied except brokered read-only inputs and private scratch.
- Network starts denied.
- CPU time, wall time, process count, output bytes, memory, and artifact sizes are bounded.
- Native-extension loading is restricted to the pinned environment.
- Standard output/error is bounded, structured as logs, and treated as potentially sensitive.
- Cancellation escalates to process termination.

### PY-010 — Honest sandbox claim

Until OS-level isolation and escape testing meet the threat model on a platform, Kearne labels Python as isolated/crash-contained rather than security-sandboxed. UI copy must not overstate protection.

## 7. Package management

MVP ships one signed, pinned environment. Later custom environments are immutable lockfile-addressed assets built outside project evaluation and scanned/approved before use. `pip install` during feature evaluation is prohibited.

Projects store environment requirements, not an uncontrolled copy of the user's global Python environment.

## 8. Verification strategy

- Generate API command scenarios and run them through C++ and Python adapters, comparing semantic results.
- Run SDK version compatibility against a matrix of supported server/API versions.
- Generate procedural functions over parameter domains and verify output schema, determinism class, cancellation, quotas, and cache keys.
- Fault-inject worker crash, infinite loop, memory growth, stdout flood, malformed artifact, forbidden file/network/process access, and incompatible OCP handshake.
- Fuzz Python-to-wire conversion and quantity parsing.
- Maintain a small malicious-script corpus for known sandbox escapes in addition to generative capability tests.

## 9. Open decisions

- **PY-OPEN-001:** Environment distribution/locking technology.
- **PY-OPEN-002:** OS isolation mechanisms and product wording on each platform.
- **PY-OPEN-003:** build123d/OCP/OCCT compatible version matrix.
- **PY-OPEN-004:** Source editing, debugging, and package UX after MVP.
- **PY-OPEN-005:** Stable explicit topology-label API for procedural features.

## 10. Definition of done

Python MVP is implemented when SDK parity scenarios pass, procedural evaluation is side-effect-free with respect to documents, workers obey tested resource/capability limits, environment fingerprints invalidate correctly, and incompatible or malicious output cannot corrupt the coordinator.
