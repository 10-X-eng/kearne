# Linux X11 Evidence

- **Source:** `acc6596958b178a5be774c13ddfcf84a18545e3d`
- **Executable SHA-256:** `7086824697576ef95b1a8c33ca682fa777d53debfd31252adb0a4431a65b7fa1`
- **System:** KDE neon 24.04, Linux 6.17.0-35-generic, x86-64, Intel Xeon Silver 4112, 16 logical CPUs
- **Toolchain:** Qt 6.11.1, CMake 3.30.5, GCC 13.3.0, Xvfb 21.1.12

## Retained run

Artifacts: [PNG](linux-x86_64-qt-6.11.1-x11-xvfb/application-session.png), [semantic snapshot](linux-x86_64-qt-6.11.1-x11-xvfb/semantic-ui.json), [capture metadata](linux-x86_64-qt-6.11.1-x11-xvfb/capture.json).

`linux-x86_64-qt-6.11.1-x11-xvfb/` was captured on a 1600×900 Xvfb display with `QT_QUICK_BACKEND=software`. Both registered surfaces emitted `frameSwapped` twice before capture. The PNG is 1408×700, covers both surfaces, contains 88 sampled colors, and matches its recorded SHA-256. The correlated snapshot contains 23 unique semantic nodes. Artifact directories are mode `0700`; files are `0600`.

The image was visually inspected at original resolution. Both surfaces are present, legible, unclipped, and nonblank. This is review evidence, not a pixel oracle.

## Active-session run

The same executable passed on the active X11 session with the default Qt graphics backend at `2026-08-19T16:18:31.436Z`. It returned two surfaces, 23 semantic nodes, render generation 2, a 1408×700 sRGB PNG, and image SHA-256 `db80abffe056dd7d7fec5f553d3358361c59be0412ba75bd43dc989a9911265c`.

## Result

The Qt accessibility plus registered-`QQuickWindow` path is viable for the tested Linux/X11 client surfaces. It does not close SPIKE-011: Wayland, Windows, native transients, mixed DPI, overlap/z-order, GPU/device loss, semantic actions, packaging, and app-server image attachment remain unproved.
