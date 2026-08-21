# Kearne Product Definition

- **Status:** Proposed
- **Requirement prefix:** `PROD`
- **Depends on:** [`SPEC.md`](../SPEC.md)
- **Unblocks:** [MVP definition](02-mvp-definition.md), all capability plans

## 1. Product statement

Kearne is a new local-first mechanical CAD system whose GUI, automation, plugins, and AI edit one native build123d function graph and typed engineering model. It is not a FreeCAD rewrite, fork, workbench replacement, or compatibility clone. Its first proof is reliable parametric editing without separate GUI and code geometry authorities.

## 2. Initial users

The first supported user is a mechanically literate designer or engineer creating individual machined, printed, or fabricated solid parts on Windows or Linux. They value editability, transparent history, keyboard access, local files, and automation.

The initial release is not optimized for:

- casual mesh sculpting;
- architecture/BIM;
- industrial styling requiring Class-A surfacing;
- enterprise production deployment;
- safety certification based solely on Kearne output;
- real-time multi-user editing.

These markets may influence architecture but do not expand MVP acceptance.

## 3. Product promises

### PROD-001 — Local ownership

Opening, editing, saving, recovering, importing, and exporting an MVP part MUST work without a network connection. No project data may leave the machine without an explicit user- or administrator-authorized action.

### PROD-002 — One engineering behavior

A supported modeling action MUST change the same source/function graph and use the same transaction and evaluation path whether initiated by QML, CLI, Python, AI, replay, or test.

### PROD-003 — Source editability

Part geometry MUST remain represented by editable native build123d source, typed inputs, named outputs, and references. Kearne MUST NOT replace source with opaque BREP merely to make an operation appear successful.

### PROD-004 — Honest failure

Kearne MUST retain source and typed intent and issue a structured diagnostic when parsing or geometry fails. It MUST NOT silently choose a different face, weaken a constraint, modify a dimension, or accept an ambiguous topology match.

### PROD-005 — Responsive control

Camera interaction, selection feedback, command cancellation, and access to last-known-good visible geometry MUST remain available while expensive work runs.

### PROD-006 — Recoverability

A process crash or terminated worker MUST not corrupt the last durable revision. Normal crash recovery MUST recover acknowledged committed edits.

### PROD-007 — Explainable automation

Every persistent change MUST identify its origin and normalized mutation. AI and scripts operate with explicit permissions and produce ordinary editable source, functions, and engineering records.

### PROD-008 — Compatible evolution

Persisted projects, public API messages, and plugin-owned entities MUST carry schema identity. Changes require forward migration and readable diagnostics when an evaluator is unavailable.

## 4. Reference MVP workflow

The primary workflow used to judge product coherence is:

1. Create a project and component definition.
2. Sketch a dimensioned mounting plate.
3. Extrude the plate and cut a hole pattern.
4. Fillet selected edges using persistent semantic references.
5. Change the plate dimensions and observe incremental recomputation.
6. Undo and redo while jobs are active.
7. Save, terminate the process, reopen, and reproduce the same project state.
8. Export STEP and STL.
9. Have AI edit the native build123d source and perform a parameter change through Python.
10. Inspect provenance and diagnostics for all changes.

This workflow is implemented before broadening graphical operation support.

## 5. Product-wide non-goals for the first release

- Full assemblies, simulation, drawings, CAM, PDM release workflows, and collaboration.
- Binary compatibility with proprietary CAD formats.
- Cloud account requirement.
- Arbitrary native in-process plugins.
- Claiming that AI output is validated merely because geometry evaluated.
- Exact BREP stability across different OCCT versions.

## 6. Supported platform policy

### PROD-009 — Platforms

MVP release gates cover current supported Windows 11 x86-64 and two named Linux distribution/runtime baselines. Exact compiler, Qt, OCCT, driver, and packaging versions are pinned by the build plan before implementation begins.

### PROD-010 — Hardware profile

Performance requirements MUST name a minimum and recommended reference machine. Unsupported hardware may run Kearne, but is not used to waive correctness or corrupt data.

### PROD-011 — Independent modern architecture

Kearne MUST select current supported technology from its own requirements and measured evidence. FreeCAD source structure, workbench model, document object model, property system, command paths, and UX conventions are not compatibility constraints. File interchange and learned product lessons do not require architectural imitation.

### PROD-012 — Agent-observable desktop

An authorized agent MUST be able to launch Kearne deterministically, inspect semantic UI state, await visible state without sleeps, operate public controls, and receive a lossless capture containing every visible Kearne-owned surface. Platform restrictions on capturing unrelated applications or OS-owned secure surfaces MUST be reported, not hidden.

## 7. Success measures

An MVP is successful when:

- the reference workflow passes through GUI, headless API, and replay adapters;
- each desktop slice returns a complete Kearne-session capture correlated with semantic UI state;
- generated source/command sequences can edit, save, reload, and replay thousands of valid projects without invariant violations;
- topology references survive the documented MVP edit classes at the required confidence;
- forced worker and application crashes preserve the durable-revision contract;
- interactive performance meets the benchmark plan on both supported platforms;
- a new command adapter does not reimplement engineering behavior.

Operation count, source line count, and AI demo quality are secondary measures.

## 8. Open product decisions

- **PROD-OPEN-001:** Licensing and business model. This affects Qt distribution, solver choices, plugin policy, and update infrastructure.
- **PROD-OPEN-002:** Exact Linux baselines and package formats.
- **PROD-OPEN-004:** Accessibility and localization release targets.

## 9. Definition of done

This plan becomes accepted when the target user, reference workflow, MVP boundary, supported platforms, and licensing direction have product-owner approval.
