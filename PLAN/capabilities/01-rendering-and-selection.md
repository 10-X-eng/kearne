# Rendering and Selection

- **Status:** In progress; native backend gates remain open
- **Requirement prefix:** `RND`
- **Depends on:** [Evaluation](../foundations/03-evaluation-and-jobs.md), [persistent topology](../foundations/04-persistent-topology.md), [processes](../foundations/07-processes-and-ipc.md)
- **Unblocks:** interactive MVP, large assemblies, visual analysis

## 1. Purpose

Render immutable engineering results responsively and translate visual picks back into revision-correct semantic references. Rendering is a projection; it never owns document or topology identity.

## 2. Render projection

```text
RenderSnapshot
  projection_revision
  generation
  objects[]

RenderObject
  transient RenderObjectId
  SemanticOccurrenceRef
  mesh artifact + LODs
  local-to-world transform
  style/material key
  visibility/selectability flags
  topology primitive map
  staleness/evaluation state
```

### RND-001 — Immutable scene input

The render backend consumes immutable snapshots or ordered generation-tagged deltas. It cannot query mutable document services during a frame.

### RND-002 — Semantic ownership

Render IDs, GPU buffer offsets, draw order, AIS owners, and triangle indices are session-local. Every selectable primitive resolves through a published map to `SemanticOccurrenceRef` and `TopologyRef` carrying revision/evaluation identity.

### RND-003 — No AIS leakage

OCCT AIS classes, if used by the initial backend, remain inside that adapter. Selection, styles, clipping, camera, and scene contracts are Kearne-owned values so another backend can implement them.

## 3. Backend port

The renderer port supports:

```text
publish snapshot/delta
set camera and viewport
set interaction quality policy
set selection/preselection
set clipping/section state
request pick region
request frame capture
report frame/GPU/resource metrics
release generation/resources
```

The port does not contain modeling commands or expose raw GPU objects to QML.

## 4. Qt Quick integration

### RND-004 — Thread contract

QML state is owned by the UI thread. GPU resources and submission are owned by the Qt Quick render thread/backend. Projection, tessellation, spatial-index construction, and upload-packet preparation run on bounded workers. Synchronization swaps immutable pointers and fixed-size view state; neither thread waits for geometry.

### RND-005 — Graphics backend policy

The Sketch backend candidate is one inline `QSGRenderNode`/QRhi renderer under [ADR-0020](../adr/0020-inline-qrhi-sketch-renderer.md). Acceptance requires the same source to pass native Qt 6.8 and 6.11 suites with both OpenGL and Vulkan. QRhi and `Qt6::GuiPrivate` remain confined to the renderer adapter; the public render port stays backend-neutral. A non-RHI or Qt Quick software backend reports an explicit renderer diagnostic and cannot present or pick. The 3D exact-geometry backend remains `RND-OPEN-001`.

### RND-006 — Frame continuity

Camera manipulation uses the most recent published scene and never waits for newer tessellation. Placeholder, coarse LOD, or last-known-good geometry is visibly classified rather than blocking input.

## 5. Mesh and edge artifacts

A mesh artifact contains versioned format metadata, positions/normals, indices, bounds, topology-range mapping, and optional feature-edge/polyline data. It records source exact-geometry digest, tessellation profile, coordinate origin, and deterministic classification.

### RND-007 — Correct cache key

Mesh cache identity includes the exact geometry artifact digest, tessellation algorithm/fingerprint, linear/angular deflection, requested representations, and format version. A process-local shape hash is insufficient.

### RND-008 — Relative coordinates

Large scenes may use per-object/local origins and camera-relative transforms to preserve GPU precision. This optimization MUST NOT alter semantic world coordinates or measurement results.

### RND-009 — Reuse by definition

Component occurrences with identical mesh/style geometry share immutable GPU buffers. Instance transforms and permitted appearance overrides remain per occurrence.

## 6. Picking and selection

Picking is two-stage when necessary:

1. fast render-backend candidate detection;
2. semantic validation/refinement against the candidate's published topology mapping and evaluation key.

### RND-010 — Stale-pick rejection

A pick carries the exact displayed `SceneStamp`. A persistent command accepts it only while that stamp, revision, evaluation key, and tool preconditions remain current. Target equality without generation and digest equality is insufficient.

Pick eligibility uses the exact resident tessellation, stroke pattern, and presentation products of one displayed frame. Analytic indexing may find candidates but cannot make hidden, clipped, provisional, or absent geometry selectable. Node, candidate, resident-span, triangle, and pattern work have explicit limits; exhaustion returns no partial pick.

### RND-011 — Stable selection sets

Persistent/user-pinned selection sets store semantic references. Hover and transient box/lasso candidates may store generation-local IDs only.

### RND-012 — Selection behavior

The selection engine centrally implements filters, occurrence scope, cycling, select-similar predicates, box/lasso policy, and preselection. Individual tools contribute required target predicates rather than custom pick code.

### RND-013 — Engineering grid

`GridProjection` carries a stable plane reference, canonical origin and basis, minor spacing quantity, major interval, snap state, and camera generation. The renderer selects a 1–2–5 spacing progression that keeps lines legible as the camera changes and reports the exact displayed spacing. Sketch uses its attachment plane; model view uses the selected reference plane or component XY plane. Snapping resolves canonical plane coordinates, never screen pixels. A shell fallback may show orientation while the renderer is unavailable; the connected renderer owns the depth-aware grid.

### RND-014 — Unified camera input

Mouse, keyboard, automation, and six-axis controllers submit normalized orbit, pan, zoom, roll, fit, and standard-view input to one bounded camera controller. User-selectable mouse profiles preserve Fusion, SolidWorks, and Onshape mappings. A profile changes bindings, not camera or renderer behavior.

Linux uses a spacenav adapter. Windows uses the reviewed 3Dconnexion transport. Missing services or devices leave mouse navigation available and report their state without blocking startup.

The Linux adapter applies at most 256 source events per UI turn, preserves ordering across device/button events, coalesces motion, and queues remaining work.

### RND-015 — Orientation control

The viewport provides an orientation cube for top, bottom, front, back, left, right, and isometric views plus fit. Cube state follows the camera. Face actions use the camera controller and remain keyboard- and automation-accessible.

### RND-016 — Navigation calibration

A permanent deterministic calibration solid verifies projection, standard views, mouse profiles, wheel direction, and six-axis motion without claiming evaluated project geometry. Generated camera sequences assert finite bounded state; UI scenarios assert controller-to-frame wiring.

### RND-017 — Presentation overlays

Presentation has three distinct products. A base-state overlay restyles existing evaluated entities for hover, selection, diagnostics, or preview emphasis. A marker packet draws constraint, inference, DOF, dimension, and snapped-cursor annotations. A provisional-geometry packet draws incomplete active-tool geometry. None changes the evaluated base digest, pick index, bounds, or mesh.

Base-state entries reference semantic entities in one exact `SceneStamp`. Markers use canonical SI positions or exact base/provisional references. Provisional identity contains the base stamp, edit session, tool instance, monotonic generation, and payload digest; its tool-local references never enter source commands or persistent selection. A completed command preview is an evaluated base scene with its own stamp, not provisional geometry.

The base Sketch scene contains geometry plus regular/construction classification. An independently generated overlay references its exact base `SceneStamp`, normalizes semantic IDs, and applies `Diagnostic > Preview > Selected > Hovered > Construction > Regular`. Overlay generation is monotonic and independent of scene generation. Retargeting clears overlays for other stamps; style and overlay changes preserve the base digest, pick index, mesh chunks, and evaluation key.

Undo-last-input publishes a newer provisional generation. Stop-tool, finish, cancel, or any base/edit-session/tool retarget clears provisional state. Camera changes preserve canonical markers but invalidate snapped-cursor evidence created from another camera generation. Sketch evaluation requests exclude all presentation products; their desktop ports remain open.

### RND-018 — Bounded scene publication

Worker output is partitioned into spatially indexed immutable chunks with finite byte and primitive ceilings. Each render frame scans only bounded metadata and publishes a bounded visible byte slice. The last coherent scene remains visible and pickable until required visible coverage for the next exact stamp is complete.

One immutable presented frame retains the exact base, overlay, provisional geometry, markers, camera, viewport, and resident coverage used by rendering and picking. It becomes current atomically only after every required component is ready. Visibility, upload, coverage construction, commit, and retirement remain bounded per frame; final sealing cannot copy, sort, or scan the complete scene.

Only the production render node can publish a presented frame. Its evidence records the exact palette, effective inherited opacity, effective clip state, item-to-device transform, device-pixel ratio, device viewport, render epoch, and render-target format used for that draw; test-only renderers and software probes cannot mint this evidence.

Retirement destroys QSG resources on the render thread, then moves CPU-only state through one application-lifetime fixed-capacity reclaimer. Saturation retains at most two retired layers for later frames; render code never joins the reclamation worker. After all Qt Quick windows are destroyed, application shutdown drains the queue and joins the worker.

## 7. Styles and analysis overlays

Style resolution composes document appearance, occurrence override, interaction state, evaluation health, and analysis overlay through a deterministic precedence policy. Feature tools do not mutate base material to display preview colors.

MVP requires shaded, shaded-with-edges, wireframe, transparent preview, selection/preselection, and one section plane. Later backends add hidden-line, curvature, simulation fields, occlusion, and advanced LOD.

## 8. Device loss and memory

- GPU resources are recreatable from mesh artifacts.
- Device loss invalidates backend resources, not semantic selections.
- Memory budgets apply separately to CPU mesh mappings and GPU allocations.
- LRU eviction honors visible/pinned resources and can fall back to coarse LOD.
- Resource destruction occurs on the owning render context/thread.

## 9. Verification strategy

One render-port conformance suite runs against a deterministic null backend, the initial interactive backend, and future custom backends. A reference scene model generates add/update/remove/reorder/LOD/device-loss operations and checks backend-observable scene equivalence.

Selection properties generate meshes and primitive mappings to verify:

- every selectable primitive maps to the correct semantic reference;
- transform and instancing preserve occurrence identity;
- stale generations are rejected;
- filtering and cycling are deterministic;
- render-resource eviction does not alter selections.

Image snapshots are limited to a small portable visual-smoke corpus with tolerant perceptual comparison. Correctness is primarily asserted through scene state, exact-stamp pick identity, bounds, bounded work counters, and GPU-independent invariants.

## 10. Performance budgets

- Camera input to visible frame: p95 under 16.7 ms at 60 Hz on the reference scene and recommended hardware.
- Preselection feedback: p95 under 50 ms.
- No UI-thread operation attributable to scene publication exceeds 4 ms p95.
- MVP scene publication scales with changed objects, not total scene size.
- Later assembly fixture: 10,000 occurrences with documented triangle count, unique mesh count, viewport size, and style distribution.

## 11. Open decisions

- **RND-OPEN-001:** 3D exact-geometry interaction behind the Kearne-owned mesh renderer; Sketch uses the native mesh path.
- **RND-OPEN-002:** Qt graphics backend matrix by platform.
- **RND-OPEN-003:** GPU versus CPU first-stage picking for MVP.
- **RND-OPEN-004:** Versioned mesh artifact format and compression.

## 12. Definition of done

Rendering v1 is implemented when the same scene/selection conformance suite passes the null and production backend, device loss is recoverable, semantic picks survive instancing and scene updates, and interaction budgets hold while geometry jobs are blocked.
