# Technical Spikes

- **Status:** Proposed
- **Requirement prefix:** `SPIKE`
- **Depends on:** all proposed foundation plans
- **Unblocks:** accepted architecture decisions and Stage 1

## 1. Spike rules

A spike answers a decision with reproducible evidence. It has a hypothesis, representative workload, measurement method, exit threshold, artifacts, and ADR. It does not end with “seems workable.”

Spike code is isolated from product libraries. Reuse requires normal review, tests, and boundary compliance. Failed approaches and data remain recorded.

## 2. SPIKE-001 — Schema and binding pipeline

**Question:** Can one IDL declaration generate compatible C++/Python wire types and bounded JSON Schema/tool metadata without weakening domain types?

Build one command, query, event stream, unknown entity, and worker job in candidate tooling. Measure generated/runtime size, compile time, conversion code, unknown-field preservation, schema evolution, fuzz integration, and Qt/Python ergonomics.

**Exit:** select the toolchain and record API evolution rules, or reject schema generation scope. Demonstrate one semantic scenario through C++, local IPC, Python, CLI, and AI-tool schema with one validator.

## 3. SPIKE-002 — Durable project store

**Question:** Does SQLite in a `.kearne` file meet commit, crash, migration, large-source-artifact, and Windows/Linux filesystem behavior?

Generate revision DAGs and chunked artifacts; inject failures through VFS and process termination; test WAL/checkpoint, antivirus-like contention, copy/save-as, compaction, 1/10/100 GB cache separation, schema migration, and read-only recovery.

**Exit:** meet `PST` atomicity/open/commit thresholds or select a different physical model. Record network/cloud-folder support policy.

## 4. SPIKE-003 — OCCT process and artifact boundary

**Question:** What geometry work can safely share a process, and what is the cost/reliability of exact-shape artifact exchange?

Use pinned OCCT to evaluate chained extrude/boolean/fillet jobs across one process, warm worker pool, and dedicated processes. Measure serialization, worker-local reuse, peak memory, parallelism, cancellation latency, intentional crash containment, BREP compatibility, and topology history availability.

Repeat exchange with the proposed OCP/build123d build.

**Exit:** choose geometry pool policy, exact artifact format/version, worker affinity policy, and incompatible-version behavior.

## 5. SPIKE-004 — Persistent topology v1

**Question:** Can the proposed naming/provenance/resolution ladder meet a declared edit matrix for the first feature chain?

Implement generated sketch -> extrude -> fillet -> hole cases, perturb dimensions/topology/order, force symmetric ambiguity, serialize/reload, and reorder OCCT exploration. Record success by matrix cell and operation-history gaps.

**Exit:** accept a bounded guarantee matrix and algorithm, revise feature order, or stop the product path. One successful hand-modeled bracket does not pass.

## 6. SPIKE-005 — Qt Quick viewport

**Question:** Can OCCT AIS integrate cleanly with Qt Quick and the target graphics/platform matrix, or should Kearne own the initial mesh renderer?

Prototype scene publication, shaded edges, selection mapping, overlays, high-DPI/fractional scale, resize, multi-monitor, device loss, QML controls above viewport, background mesh updates, and 10,000 instanced objects. Capture UI/render thread traces.

**Exit:** select backend and supported Qt graphics APIs; demonstrate frame/pick budgets and no AIS types outside adapter. Reject a backend that forces document or selection ownership leakage.

## 7. SPIKE-006 — Sketch solver

**Question:** Which solver meets constraint coverage, diagnostics, licensing, numerical stability, and interactive latency?

Run the same generated systems and edits through candidates. Measure residuals, DOF/rank results, conflict sets, seed continuity, degeneracies, order sensitivity, cancellation, 100/1,000-entity latency, API integration effort, maintenance activity, and redistribution terms.

**Exit:** choose solver and supported MVP constraint subset with documented limitations, or budget a Kearne solver effort explicitly.

## 8. SPIKE-007 — Numerical envelope

**Question:** What model range and tolerances are reliable across document quantities, solver, OCCT operations, topology matching, tessellation, and STEP exchange?

Generate analytic models across scales, offsets, unit systems, near-degeneracy, and tolerance boundaries. Measure failure modes, topology stability, round trips, and GPU precision with local origins.

**Exit:** publish versioned numerical profile, supported range, conversion boundary, rejection diagnostics, and separate healing/tessellation profiles.

## 9. SPIKE-008 — Python isolation

**Question:** What crash, resource, filesystem, network, and native-extension isolation is enforceable on supported Windows/Linux builds?

Exercise infinite loops, memory/process bombs, file probes, network probes, inherited handles, malicious artifacts, worker reuse contamination, cancellation, and OCP native crashes. Measure startup/warm-pool cost.

**Exit:** define enforceable capability profiles and honest product wording. Security-sandbox claims require threat-model review; otherwise ship crash containment and explicit trust warnings.

## 10. SPIKE-009 — Dependency and license closure

**Question:** Can the selected Qt, OCCT, sketch solver, SQLite, IDL, Python, build123d/OCP, compression, and packaging stack be built, redistributed, updated, and supported together?

Produce a license/SBOM prototype, binary-size/build-time data, patch/update process, supported compiler matrix, source-offer obligations, and replacement boundary for each dependency.

**Exit:** no MVP dependency has unresolved redistribution, ABI, maintenance, or security-update risk.

## 11. Spike completion record

Each completed spike stores:

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

## 12. Definition of done

Stage 0 ends only when SPIKE-001 through SPIKE-009 have decisions and no critical result is replaced by an assumption. A failed threshold changes scope or architecture before feature implementation.
