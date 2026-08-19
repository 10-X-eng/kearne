# Rendering and Selection

- **Status:** Proposed; viewport spike required
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

QML state is owned by the UI thread. GPU resources and submission are owned by the Qt Quick render thread/backend. Communication uses Qt's supported scene graph synchronization phase and immutable values; neither thread blocks waiting for geometry.

### RND-005 — Graphics backend policy

The initial release pins supported Qt graphics backends per platform. If AIS requires an OpenGL-specific integration, the spike must prove window embedding, high-DPI, resize, multi-window, device loss, and QML overlay behavior. The public render port remains backend-neutral.

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

A pick produced for an obsolete scene generation cannot be used directly in a persistent command. The adapter revalidates it or reports that geometry changed.

### RND-011 — Stable selection sets

Persistent/user-pinned selection sets store semantic references. Hover and transient box/lasso candidates may store generation-local IDs only.

### RND-012 — Selection behavior

The selection engine centrally implements filters, occurrence scope, cycling, select-similar predicates, box/lasso policy, and preselection. Individual tools contribute required target predicates rather than custom pick code.

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

Image snapshots are limited to a small portable visual-smoke corpus with tolerant perceptual comparison. Correctness is primarily asserted through scene state, pick identity, bounds, and GPU-independent invariants.

## 10. Performance budgets

- Camera input to visible frame: p95 under 16.7 ms at 60 Hz on the reference scene and recommended hardware.
- Preselection feedback: p95 under 50 ms.
- No UI-thread operation attributable to scene publication exceeds 4 ms p95.
- MVP scene publication scales with changed objects, not total scene size.
- Later assembly fixture: 10,000 occurrences with documented triangle count, unique mesh count, viewport size, and style distribution.

## 11. Open decisions

- **RND-OPEN-001:** AIS-in-Qt-Quick adapter versus early Kearne mesh renderer after the viewport spike.
- **RND-OPEN-002:** Qt graphics backend matrix by platform.
- **RND-OPEN-003:** GPU versus CPU first-stage picking for MVP.
- **RND-OPEN-004:** Versioned mesh artifact format and compression.

## 12. Definition of done

Rendering v1 is implemented when the same scene/selection conformance suite passes the null and production backend, device loss is recoverable, semantic picks survive instancing and scene updates, and interaction budgets hold while geometry jobs are blocked.
