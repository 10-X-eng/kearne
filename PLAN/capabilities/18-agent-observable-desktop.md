# Agent-Observable Desktop

- **Status:** Proposed; mandatory desktop infrastructure
- **Requirement prefix:** `OBS`
- **Depends on:** [Qt/QML shell](15-qt-qml-application-shell.md), [rendering](01-rendering-and-selection.md), [Codex harness](17-codex-app-server-harness.md)
- **Unblocks:** verifiable desktop implementation and AI visual inspection

## 1. Purpose

An agent that changes the desktop must be able to launch it, determine when a requested state is visible, inspect semantic controls, capture every visible Kearne surface, and receive the lossless image. Code inspection is not visual verification.

The observation plane reports UI state; it does not mutate engineering state or replace Engineering API queries.

## 2. Observation contract

```text
application.start(profile, project?) -> ApplicationSessionId
application.state(session) -> ApplicationState
application.stop(session, policy) -> StopOutcome
ui.snapshot(session) -> SemanticUiSnapshot
ui.await(session, predicate, deadline) -> ObservationPoint
ui.capture(session, target, observation_point?) -> ImageArtifact
ui.action(session, semantic_action) -> ActionOutcome
```

The contract is available through a local developer/test driver and the capability-filtered Agent Bridge. Release builds may disable control and capture endpoints unless the user enables agent access.

### OBS-001 — Deterministic launch

The driver selects executable/build identity, settings profile, locale, theme, scale, graphics backend, project, screen geometry, and clean or retained workspace state. It returns process and session identities without relying on window titles or process-name scans.

### OBS-002 — Semantic snapshot

Every interactive control exposes a stable semantic ID, role, accessible name, enabled/visible/focused state, logical and physical bounds, value/state summary, action set, and parent/child relation. Viewport selections, active command, dialogs, jobs, diagnostics, revision, and render generation appear as typed state, not inferred pixels.

Localized text, QML object addresses, hierarchy indexes, and screen coordinates MUST NOT be durable control identity.

### OBS-003 — Event-based readiness

`ui.await` waits on typed conditions and frame presentation events with a deadline. It never sleeps for an assumed render, job, animation, or dialog duration. Timeout returns the last semantic snapshot and frame/job generations.

## 3. Capture targets

```text
ApplicationSession  every visible Kearne window, dialog, menu, popup, and tooltip
ApplicationWindow   one complete Kearne top-level window
Viewport            one viewport at native pixel size
Display             one physical/virtual OS display when permission allows
```

### OBS-004 — Complete Kearne frame

`ApplicationSession` is mandatory on Windows and supported Linux baselines. The capture composes all Kearne-owned visible surfaces in desktop coordinates so an agent cannot miss a detached panel, modal dialog, popup, or tooltip. It MUST NOT crop to the viewport or return a thumbnail.

A UI surface that internal capture and permitted platform APIs cannot observe MUST NOT carry an agent-verified workflow; Kearne must replace it with a capturable surface or declare that workflow unsupported for agent operation.

### OBS-005 — Lossless artifact and metadata

Capture returns a lossless PNG artifact plus build/session ID, observation point, target IDs, pixel dimensions, device-pixel ratio, color space, theme, locale, revision, UI generation, render generation, capture method, and timestamp. The API returns the artifact handle/path to its caller and can attach it to an app-server turn as local-image input.

### OBS-006 — Frame consistency

A capture requested at an observation point contains frames presented at or after that point. It reports surfaces that changed during composition. A stale, partial, black, transparent, or device-lost frame returns a diagnostic rather than silent success.

### OBS-007 — Full-display limits are explicit

`Display` capture is capability-gated because it can expose other applications. Wayland portals, locked sessions, secure desktops, OS-owned permission prompts, DRM-protected surfaces, and administrator policy may prevent it. Kearne guarantees complete capture of Kearne-rendered surfaces; it MUST NOT claim universal capture of OS-owned pixels.

## 4. Actions and control

Semantic actions target control IDs and declared actions such as invoke, set value, choose item, focus, press shortcut, and pointer/gesture input in a named viewport. Each outcome reports resulting UI generation and any submitted Engineering API request ID.

### OBS-008 — No hidden mutation path

UI actions use the same public input/controller path as a user. The observation driver cannot call private QML functions, write UI models, or bypass command validation to make a scenario pass.

Coordinate actions are allowed for spatial viewport behavior but record viewport ID, camera, projection, scale, and normalized plus physical coordinates for replay.

## 5. Privacy and retention

- Agent access is visible, revocable, and scoped to an application session.
- Application capture excludes unrelated applications by construction; display capture requires a separate grant.
- Captures and semantic snapshots may contain project secrets. They are temporary by default and do not enter project history, telemetry, crash reports, or provider context without policy.
- Temporary artifacts use private permissions, quotas, expiry, and deletion on normal shutdown; abnormal cleanup occurs on next launch.
- Secure text fields report role/state but not secret values and render according to the normal masked UI.

## 6. Verification

The observation conformance suite runs against fake surfaces and real packaged builds. The platform matrix covers high-DPI/fractional scale, multiple windows and displays, modal/modeless dialogs, menus, popups, tooltips, viewport overlays, maximized/minimized/occluded states, software and selected GPU backends, device loss, animations, and concurrent jobs.

Image assertions check target coverage, dimensions, composition, non-empty content, alpha/color validity, and frame metadata. Broad pixel goldens remain prohibited. Human or model visual review consumes the returned image while semantic assertions locate failures.

### OBS-009 — Desktop evidence gate

A desktop work package is incomplete without a captured `ApplicationSession` artifact from the changed build, its matching semantic snapshot, and automated scenario evidence for affected behavior. The artifact proves observability and supports review; it is not the sole correctness oracle.

### OBS-010 — Headless and physical coverage

Per-change UI scenarios run on a controlled virtual display or offscreen backend. Nightly/release suites also run packaged builds on physical Windows and Linux compositor/GPU baselines. Passing headless rendering cannot waive physical-platform failures.

## 7. Open decisions

- **OBS-OPEN-001:** Cross-platform surface enumeration/composition strategy for QML, native menus, and child windows.
- **OBS-OPEN-002:** Stable automation bridge: Qt accessibility, a dedicated observation protocol, or both.
- **OBS-OPEN-003:** Artifact format beyond PNG for HDR and future video/event traces.
- **OBS-OPEN-004:** User-facing agent-access indicator and consent persistence.

## 8. Definition of done

The desktop is agent-observable when a clean driver can launch a packaged build, await the reference workflow without sleeps, control it through semantic actions, return one complete lossless Kearne-session image and matching semantic snapshot, attach that image to an app-server turn, and report platform limitations without false success.
