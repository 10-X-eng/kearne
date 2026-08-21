# Technology Decision Gates

- **Status:** Proposed
- **Requirement prefix:** `TECH`
- **Depends on:** all proposed foundation plans
- **Unblocks:** accepted architecture decisions and Stage 1

## 1. Evidence rules

A technology gate answers a decision with reproducible evidence. Prefer measurements from production code. Create an isolated artifact under [`prototype/`](../../prototype/README.md) only when the candidate is destructive, unsafe, mutually exclusive, or otherwise impractical to evaluate in production.

Prototype code is isolated from product libraries and packages. A successful result is reimplemented at the owning production boundary. Failed approaches and data remain recorded.

## 2. TECH-001 — Schema and binding pipeline

**Question:** Can one IDL declaration generate compatible C++/Python wire types and bounded JSON Schema/tool metadata without weakening domain types?

Build one command, query, event stream, unknown entity, and worker job in candidate tooling. Measure generated/runtime size, compile time, conversion code, unknown-field preservation, schema evolution, fuzz integration, and Qt/Python ergonomics.

**Exit:** select the toolchain and record API evolution rules, or reject schema generation scope. Demonstrate one semantic scenario through C++, local IPC, Python, CLI, and AI-tool schema with one validator.

**Result (2026-08-19):** Protobuf 35.1 was selected by [ADR-0011](../adr/0011-protobuf-engineering-api.md). The retained [TECH-001 evidence](../../prototype/001-schema-binding-pipeline/README.md) covers C++, local binary IPC, generated Python, CLI JSON, AI metadata, evolution, limits, and fuzzing. Production schemas and conformance live under `api/schema`; supported-platform closure remains part of `TECH-009`.

## 3. TECH-002 — Git project package

**Question:** Can one local Git repository and one portable `.kearne` package preserve exact project bytes, retained history, safe Save, recovery, and optional remotes on Windows and Linux?

Generate commit DAGs, refs, source trees, typed records, and binary artifacts. Inject failures during object writes, ref compare-and-swap, bundle creation, ZIP64 publication, flush, replacement, reopen, migration, and shared-file conflict. Measure command commits, full and incremental packaging, copy/Save As, Git interoperability, antivirus contention, and 1/10/100 GB cache separation.

**Exit:** meet `PST` integrity, portability, atomicity, open, commit, and package thresholds with no second history graph. Select the embedded Git and package libraries and record shared/cloud-folder behavior before Save/Open is enabled.

## 4. TECH-003 — OCCT process and artifact boundary

**Question:** What geometry work can safely share a process, and what is the cost/reliability of exact-shape artifact exchange?

Use pinned OCCT to evaluate chained extrude/boolean/fillet jobs across one process, warm worker pool, and dedicated processes. Measure serialization, worker-local reuse, peak memory, parallelism, cancellation latency, intentional crash containment, BREP compatibility, and topology history availability.

Repeat exchange with the proposed OCP/build123d build.

**Exit:** choose geometry pool policy, exact artifact format/version, worker affinity policy, and incompatible-version behavior.

## 5. TECH-004 — Persistent topology v1

**Question:** Can function-published labels and the provenance/resolution ladder meet a declared edit matrix for the first model-function chain?

Implement generated `sketch() -> extrude() -> fillet() -> holes()` source, perturb dimensions/topology/declaration order, force symmetric ambiguity, serialize/reload, and reorder OCCT exploration. Record success by matrix cell and operation-history gaps.

**Exit:** accept a bounded topology-capability matrix and algorithm, revise function labeling, or stop the product path. One successful hand-modeled bracket does not pass.

## 6. TECH-005 — Qt Quick viewport

**Question:** Can OCCT AIS integrate cleanly with Qt Quick and the target graphics/platform matrix, or should Kearne own the initial mesh renderer?

Prototype scene publication, shaded edges, selection mapping, overlays, high-DPI/fractional scale, resize, multi-monitor, device loss, QML controls above viewport, background mesh updates, and 10,000 instanced objects. Capture UI/render thread traces.

**Exit:** select backend and supported Qt graphics APIs; demonstrate frame/pick budgets and no AIS types outside adapter. Reject a backend that forces document or selection ownership leakage.

## 7. TECH-006 — Sketch solver

**Question:** Which solver meets constraint coverage, diagnostics, licensing, numerical stability, and interactive latency?

Run the same generated systems and edits through candidates. Measure residuals, DOF/rank results, conflict sets, seed continuity, degeneracies, order sensitivity, cancellation, 100/1,000-entity latency, API integration effort, maintenance activity, and redistribution terms.

**Exit:** choose solver and supported MVP constraint subset with documented limitations, or budget a Kearne solver effort explicitly.

**Provisional result (2026-08-19):** Ceres 2.2.0 with Eigen 3.4.1 implements the production solver port; it is not selected. The generated [`sketch-solver-properties`](../../modules/adapters/sketch_ceres/tests/solver_properties.cpp) suite covers every MVP residual equation, nonlinear circle tangency, scale and declaration-order metamorphisms, rank-deficient fallback, contradiction reporting, drag refusal, concurrent cancellation, and degenerate source/seed/result geometry. Debug passed in 3.04 s, Release in 0.23 s, the Release 100k profile passed in 2.11 s at 9,724 KiB peak RSS, and the adapter/domain ASan+UBSan run passed in 9.74 s. Ceres itself did not inherit Kearne's target-scoped sanitizer flags.

The connected-chain benchmark and measurement boundary are recorded under [performance](04-performance.md#sketch-solver-candidate). Release p95 was 3.260 ms for 100 entities and 37.050 ms for 1,000. Ceres contributes 115 object files; its archive is 7,718,938 bytes in Release and 475,862,536 bytes in Debug, while its build tree is 15,045,430 and 929,875,962 bytes respectively. The Kearne adapter archive is 481,298 bytes in Release. Candidate comparison, full supported-platform evidence, conflict minimality, redistribution closure, and the build/debug footprint remain open; `TECH-006` does not pass.

## 8. TECH-007 — Numerical envelope

**Question:** What model range and tolerances are reliable across document quantities, solver, OCCT operations, topology matching, tessellation, and STEP exchange?

Generate analytic models across scales, offsets, unit systems, near-degeneracy, and tolerance boundaries. Measure failure modes, topology stability, round trips, and GPU precision with local origins.

**Exit:** publish versioned numerical profile, supported range, conversion boundary, rejection diagnostics, and separate healing/tessellation profiles.

## 9. TECH-008 — Python isolation

**Question:** What crash, resource, filesystem, network, and native-extension isolation is enforceable on supported Windows/Linux builds?

Exercise infinite loops, memory/process bombs, file probes, network probes, inherited handles, malicious artifacts, worker reuse contamination, cancellation, and OCP native crashes. Measure startup/warm-pool cost.

**Exit:** define enforceable capability profiles and honest product wording. Security-sandbox claims require threat-model review; otherwise ship crash containment and explicit trust warnings.

## 10. TECH-009 — Dependency and license closure

**Question:** Can the selected Qt, OCCT, sketch solver, Git, IDL, Python, build123d/OCP, compression, and packaging stack be built, redistributed, updated, and supported together?

Produce a license/SBOM prototype, binary-size/build-time data, patch/update process, supported compiler matrix, source-offer obligations, and replacement boundary for each dependency.

**Exit:** no MVP dependency has unresolved redistribution, ABI, maintenance, or security-update risk.

## 11. TECH-010 — Codex app-server integration

**Question:** Can a pinned Codex app-server provide Kearne's thread, turn, approval, tool, image-input, cancellation, and recovery lifecycle through a small replaceable adapter?

Generate protocol schemas from the installed executable; implement initialization, one thread/turn, streamed events, an approval, a local MCP Kearne query/command tool, local-image input, cancellation, crash/restart, and unsupported-version failure. Measure startup, event latency, memory, packaging size, schema churn, auth states, transcript storage, and platform behavior. Verify stdio framing remains clean under logs and failure.

The official documentation currently marks the app-server command experimental and unsupported for production. Evidence must either show a support-status change or explicitly accept and staff Kearne-owned compatibility maintenance.

**Exit:** accept one exact version and packaging policy, client boundary, Agent Bridge mechanism, credential ownership, sandbox/approval split, recovery policy, and upstream-support risk, or revise the harness ADR before AI implementation.

## 12. TECH-011 — Agent-observable desktop

**Question:** Can Kearne deterministically return every visible Kearne-owned surface and a correlated semantic snapshot on supported Windows and Linux display stacks?

Launch a packaged Qt/QML prototype through the observation driver. Exercise multiple windows/displays, native and QML menus, popups, tooltips, modal dialogs, high-DPI/fractional scale, viewport overlays, animation, occlusion, device loss, and software/GPU backends. Capture without sleeps and attach the result to an app-server local-image turn. Probe full-display restrictions under X11, Wayland portals, and Windows secure surfaces.

**Exit:** select surface enumeration/composition and semantic automation paths; prove full Kearne-session capture on release baselines; document OS-owned pixels that cannot be guaranteed; block desktop feature breadth if capture is incomplete.

## 13. TECH-012 — Native function and graphical editing boundary

**Question:** Can unrestricted native build123d functions support stable contracts, source-preserving graphical edits, incremental evaluation, and Git-like merge without a parallel feature model?

Implement the production source/function boundary for one generated sketch/extrude module. Exercise algebra, builder, and mixed-mode refactors; helpers, loops, and classes; parse without execution; manifest/decorator declarations; source moves; invalid syntax; expected-digest edits; dependency invalidation; topology capability changes; and concurrent three-way source merges.

**Exit:** select the contract declaration and concrete-syntax tooling; define recognized-editor capability and honest fallback; prove source/function identity survives moves and revisions; prove unrecognized valid source evaluates unchanged; and fail the gate if any hidden sketch/feature graph becomes geometry authority.

**Partial result (2026-08-19):** [ADR-0017](../adr/0017-python-ast-source-editing.md) selects standard-library AST and token spans for the recognized generated-source editor after LibCST missed the latency budget and Tree-sitter crashed at scale. The parameterized benchmark and generated preservation suite pass. Function contracts, evaluation isolation, topology publication, moves, and merge remain open; `TECH-012` is not closed.

## 14. Prototype completion record

Each completed prototype stores:

```text
hypothesis and date
exact source/build/dependency revisions
hardware/OS/driver
workload generator seed/profile
raw results and analysis
accepted threshold result
ADR link
prototype retention/removal decision
new risks and plan edits
```

## 15. Definition of done

Each decision closes before its first irreversible public, persisted, packaged, or backend-specific dependency. Gates do not block unrelated production UI work. A failed threshold changes scope or architecture before dependent implementation expands.
