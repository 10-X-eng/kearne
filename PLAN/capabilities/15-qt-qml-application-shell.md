# Qt/QML Application Shell

- **Status:** Proposed
- **Requirement prefix:** `UI`
- **Depends on:** [Engineering API](../foundations/08-engineering-api.md), [rendering](01-rendering-and-selection.md), [evaluation](../foundations/03-evaluation-and-jobs.md)
- **Unblocks:** desktop MVP, [agent observation](18-agent-observable-desktop.md), and professional workflows

## 1. Purpose

Provide a modern native Windows/Linux shell that projects Kearne's engineering state, supports dense keyboard/mouse workflows, and remains responsive while computation fails, blocks, or restarts.

## 2. UI ownership

QML owns presentation and ephemeral interaction state:

- panel layout, focus, hover, menus, dialogs;
- current view/camera and transient selection;
- draft field text and validation display;
- command palette/search presentation;
- operation/progress notification state.

The Engineering API owns source/function state, typed engineering records, command validation, revisions, jobs, and diagnostics.

### UI-001 — Read-only projections

QML receives read-only revision/generation-tagged models. It cannot own canonical source/contracts, mutable engineering records, `TopoDS_Shape`, repository handles, or normalized mutations.

### UI-002 — Controller boundary

Controllers translate interaction into source/function or typed command/query requests and adapt results into projections. They contain no model-function evaluation or duplicated domain validation.

### UI-003 — Generated generic surfaces

Command, function, and parameter schemas drive command-palette entries, property editors, units, enum choices, help, permission visibility, and basic forms. Specialized sketch/viewport interactions submit structural source-function requests against expected digests.

## 3. Shell layout

[`ScreenShot.png`](../../rendering/ScreenShot.png) is non-binding design inspiration. It defines no requirement, acceptance criterion, layout, style, component, or behavior. Its reusable visual intent and conflicts are documented in the [visual design system plan](16-visual-design-system.md).

The initial shell contains:

- application/project/revision bar;
- contextual command strip;
- structure/history panel;
- viewport;
- property/parameter panel;
- native source/function inspector and editor for the selected model scope;
- command palette and search;
- jobs/diagnostics/AI area;
- status and selection feedback.

Dock/layout implementation must support persistence and restoration without serializing arbitrary QML object state into the project.

## 4. Interaction model

### UI-004 — Command identity

Every action has a stable command identifier, availability predicate, display metadata, default shortcut where applicable, and permission requirement. Menus, toolbars, context actions, command palette, and shortcut binding reference that identity.

### UI-005 — Context is explicit

Tool activation captures selection, revision, active component/body/configuration, and view context as typed values. It cannot read changing global context halfway through command construction.

### UI-006 — Preview generations

Dialogs and manipulators issue cancellable preview generations. Only the newest generation displays as current. Apply submits an ordinary command; Cancel removes preview without history mutation.

### UI-007 — No modal computation

Dialogs may block local interaction flow but never wait synchronously for geometry, import, export, Python, AI, meshing, or persistence I/O. They observe operation handles and remain cancellable.

## 5. Structure, history, properties, and diagnostics

- Structure and model-history views project the same function calls, output bindings, and typed records.
- Property fields show function inputs, base/effective values, units, expression source, validation, revision, recognition capability, and editability.
- Diagnostics link to source locations or typed references and repair commands.
- Jobs expose stage, progress confidence, cancellation, resource contention, and revision.
- Last-known-good geometry and stale analyses have persistent visual/status markers, not color alone.

### UI-008 — No swallowed failure

Command rejection, evaluation failure, cancellation, worker loss, stale preview, and permission denial have distinct UI states. A failed operation cannot disappear because a toast timed out.

## 6. Input and accessibility

- Keyboard focus order and shortcuts are deterministic and remappable.
- All commands remain searchable even when not on a toolbar.
- Selection cycling and filters have keyboard access.
- Icons have text alternatives/tooltips; status is not encoded by color alone.
- High-DPI and fractional scaling are tested on both platforms.
- Screen-reader semantics and contrast targets are set before public beta.
- Text input and unit parsing respect locale while commands remain canonical.

## 7. UI state persistence

User-level settings store theme, shortcuts, layout presets, and device preferences. Workspace-local state may store open panels, camera, transient selections, and active branch outside canonical engineering revisions. Project-shared presentation entities, such as saved views, require explicit semantic schemas and commands.

### UI-009 — State classification

Every persisted UI value is classified as user setting, local workspace state, or shared project record. UI code cannot write an unversioned miscellaneous project-settings blob.

The default unit profile is a user setting; the project display/input profile is a typed project presentation record; grid visibility and snap preference are local workspace state. Snap-derived geometry is stored only through the resulting engineering command.

### UI-010 — Observable controls

Every interactive control and transient surface MUST expose stable semantic identity, accessibility role/name/state, bounds, and supported actions through the Observation API. QML object addresses, visual hierarchy indexes, and localized labels are not identity.

### UI-011 — Capturable surface ownership

Kearne-owned windows, dialogs, menus, popups, tooltips, and viewport overlays MUST register with the application session so complete capture can include them. A new windowing mechanism is incomplete until it passes observation conformance.

### UI-012 — No decorative production workspace

A release build exposes a workspace or action only when its owning acceptance workflow reaches real Engineering API commands, jobs, results, diagnostics, history, and recovery through production adapters. Development providers may exercise unfinished states only in identified development builds; packaging checks reject them. Registry-generated traversal verifies every visible action's route and availability.

## 8. Verification strategy

UI assurance favors semantic and model-level checks:

- Controller conformance tests run command/query scenarios without QML.
- Generated descriptors instantiate generic property forms and verify field type, units, permission, validation, and round-trip command payload.
- Model-based UI tests generate command activation, focus, selection, preview, apply/cancel, undo/redo, job completion, and failure events against fake Engine/Renderer ports.
- Accessibility-tree tests verify roles, names, focus order, and non-color status.
- A bounded end-to-end suite runs critical workflows on real Windows/Linux builds.
- Pixel snapshots are limited to design-system primitives and a small viewport/shell smoke set; they are not the primary interaction oracle.
- Responsiveness instrumentation fails tests when forbidden blocking work runs on the UI thread.
- The observation driver launches the packaged application, awaits typed UI/frame state, captures the complete Kearne session, and correlates the image with its semantic snapshot.

## 9. Performance budgets

- Input dispatch and local state update: p95 below 8 ms.
- UI-thread frame work: p95 below 8 ms in the reference workspace, leaving render budget.
- Opening command palette: p95 below 100 ms with the full registered command set.
- Incremental source/tree/property updates scale with changed projection nodes, not total project size.
- No synchronous UI wait above 50 ms on a worker/repository/network operation.

## 10. Open decisions

- **UI-OPEN-001:** Qt Quick Controls styling versus a controlled Kearne component layer.
- **UI-OPEN-002:** Dock/layout library or custom split-panel model.
- **UI-OPEN-003:** Accessibility baseline and localization milestone.
- **UI-OPEN-004:** Multi-window/multi-document MVP scope.
- **UI-OPEN-005:** Search indexing backend and command/source/project result ranking.

## 11. Definition of done

The shell is implemented when the reference workflow is keyboard and pointer accessible, direct source and recognized graphical edits reach the same function graph, failure/job states remain inspectable, the complete session is capturable and semantically inspectable, UI-thread guards pass, and supported-platform end-to-end tests meet responsiveness budgets.
