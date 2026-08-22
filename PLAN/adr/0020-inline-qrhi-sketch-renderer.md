# ADR-0020: Inline QRhi Sketch Renderer

- **Status:** Proposed
- **Date:** 2026-08-20
- **Related:** `RND-004`, `RND-005`, `RND-010`, `RND-017`, `RND-018`, `PERF-003`, `PERF-006`, `PERF-007`, `PERF-009`

## Context

Sketch publishes base geometry, a base-state overlay, provisional geometry, and markers as one displayed and pickable frame. Overlay restyling must reuse base geometry, and selection must retain the presentation that reached the Qt Quick render pass. Qt documents [`QSGRenderNode`](https://doc.qt.io/qt-6/qsgrendernode.html) as its inline custom-rendering path and documents [`QQuickRhiItem`](https://doc.qt.io/qt-6/qquickrhiitem.html) as a texture-backed path.

This decision covers Sketch presentation; `RND-OPEN-001` still owns the 3D exact-geometry renderer boundary.

## Decision

Each Sketch viewport uses one `QSGRenderNode` that records only QRhi commands inline with the Qt Quick render pass. Base, overlay, provisional geometry, and markers are logical products of one frame, not independent scene-graph layers.

Sketch preparation publishes native vector primitives and NURBS control data. The fragment path evaluates that data directly. Sketch rendering and picking MUST NOT flatten curves into polylines, create stroke meshes, use chord-error LODs, or infer hits from triangles. Unsupported vector data fails publication; no alternate renderer is permitted.

Prepared products reuse immutable CPU resources by exact identity. GPU resources are immutable after upload, shared by logical products where identity permits, and confined to one `QQuickWindow` render epoch; they cannot cross a window, scene-graph reinitialization, or device loss.

All [`QRhi`](https://doc.qt.io/qt-6/qrhi.html) types remain inside `sketch_frame_renderer.*`, and only its owning target links the narrow `Qt6::GuiPrivate` dependency. The renderer targets Qt 6.8 or newer, declares `QSGRenderNode::NoExternalRendering`, and contains no OpenGL- or Vulkan-specific draw path.

The Sketch renderer creates no viewport texture or secondary render pass and does not use `QQuickRhiItem`.

The production render node publishes presentation evidence only after recording the frame with the exact palette, effective inherited opacity, effective clip state, item-to-device transform, device-pixel ratio, and device viewport it used. Test probes cannot publish presented-frame evidence.

A non-RHI or Qt Quick software backend reports an explicit renderer diagnostic, publishes no presented frame, and enables no picking.

Pipeline reuse keys include the QRhi backend, required feature set, and [`QRhiRenderPassDescriptor::serializedFormat()`](https://doc.qt.io/qt-6/qrhirenderpassdescriptor.html). `releaseResources()` and renderer destruction invalidate the render epoch and release its GPU resources on the render thread.

A command may request a shader-family warmup. Presentation is not current until the requested pipeline is ready; warmup does not publish geometry or selection evidence.

## Consequences

- QRhi is semi-public: Qt requires `GuiPrivate` and provides no source or binary compatibility guarantee across minor releases, so each supported Qt line requires compilation and native tests.
- The render adapter owns Qt compatibility changes; render products, picking, and QML cannot include QRhi types.
- Sketch quality is independent of zoom because camera changes do not rebuild geometry.

## Alternatives rejected

- `QQuickRhiItem` adds a texture render target, another render pass, and a textured-quad composition step.
- Separate `QSGGeometryNode` layers cannot express exact overlay restyling with shared arbitrary geometry ranges without duplication or transparent overdraw.
- Direct OpenGL and Vulkan implementations duplicate resource, synchronization, and device-loss policy.
- CPU or QML path flattening creates a second renderer and makes curve quality camera-dependent.

## Evidence required for acceptance

- Compile the renderer against the latest supported Qt 6.8 and Qt 6.11 patch releases without source variants.
- Run the native frame, clipping, palette, multi-window, render-target-change, and device-loss suites on OpenGL and Vulkan under both Qt lines with the production node.
- Generate every supported primitive across scale, rotation, weight, degree, and camera ranges; compare pixels and picks with independent analytic evaluators and reject any polygonal curve payload.
- Measure frame latency, render-thread work, uploads, draw calls, render targets, and GPU bytes for the versioned 1,000/10,000/100,000 Sketch workloads; no run may contain a viewport texture pass.
- Verify the bootstrap installs the matching `GuiPrivate` development package and that packaging resolves its Qt-version coupling.

The ADR remains Proposed until all evidence passes.
