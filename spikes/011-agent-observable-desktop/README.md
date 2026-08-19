# SPIKE-011 — Agent-Observable Desktop

- **Status:** In progress
- **Question:** Can Kearne return every visible owned Qt surface and a correlated semantic snapshot without sleeps?
- **Requirements:** `OBS-001` through `OBS-007`, `OBS-009`, `OBS-010`, `UI-010`, `UI-011`
- **Decision:** [ADR-0007](../../PLAN/adr/0007-agent-observable-desktop.md)

## Probe

The Qt 6.11 prototype registers two `QQuickWindow` surfaces, waits for each `frameSwapped` event, captures each client surface with `QQuickWindow::grabWindow`, and composes them in desktop coordinates. The same observation point is written into the PNG metadata record and an accessibility-derived semantic snapshot. Any actionable accessible node without a stable `Accessible.id` fails capture.

```sh
cmake -S spikes/011-agent-observable-desktop -B build-spike-011 -G Ninja
cmake --build build-spike-011
xvfb-run -a -s '-screen 0 1600x900x24' \
  build-spike-011/kearne-observation-spike --output /tmp/kearne-observation
```

The output directory is private and contains:

- `application-session.png` — lossless composition of visible registered client surfaces;
- `semantic-ui.json` — stable IDs, roles, names, states, actions, relations, and bounds;
- `capture.json` — correlated observation point, surface/frame coverage, image digest, environment, and limits.

## Current boundary

This probe does not claim completion. `grabWindow` excludes native decorations, shadows, OS-owned secure surfaces, and unrelated applications. Mixed-DPI composition, overlapping surface z-order, QML/native transient surfaces, Wayland, Windows, GPU backends, device loss, semantic actions, packaging, and app-server local-image attachment remain exit work. Physical compositor coverage cannot be replaced by Xvfb evidence.

