# Assemblies and Mechanical Relationships

- **Status:** Proposed; post-MVP
- **Requirement prefix:** `ASM`
- **Depends on:** [Document model](../foundations/01-document-model.md), [persistent topology](../foundations/04-persistent-topology.md), [rendering](01-rendering-and-selection.md), [versioning](09-versioning-and-merge.md)
- **Unblocks:** BOM, motion, assembly simulation, exploded drawings

## 1. Purpose

Represent repeated component definitions as occurrences, solve mechanical relationships independently from geometry evaluation, and support lightweight rendering without duplicating definition geometry.

## 2. Canonical entities

```text
Assembly
ComponentInstance
Joint
AssemblyPattern
ExplodedState
BOMOverride
```

`ComponentInstance` contains stable instance ID, definition reference, version/configuration reference, initial transform, visibility/suppression, allowed overrides, and metadata.

### ASM-001 — Definition reuse

Instances reference component definitions and immutable source revisions; they do not copy source or named outputs. Repeated compatible occurrences share exact and render artifacts.

### ASM-002 — Occurrence addressing

All assembly geometry references use `InstancePath + definition-owned SemanticRef`. Nested occurrence identity survives tree display reordering and transform changes.

### ASM-003 — Explicit grounding

Fixed/grounded state is an explicit assembly constraint or instance property with defined solver semantics. The first component is not silently fixed merely because it appears first.

## 3. Reference policy

Component references may be:

- pinned to an immutable version/revision;
- floating to an explicitly selected branch/workspace under update policy;
- local within the same project revision graph.

Floating references resolve to a recorded effective revision during each assembly evaluation. Released assemblies use pinned references.

### ASM-004 — No mixed hidden updates

An assembly solve and render snapshot records the exact effective revision/configuration of every definition. Background link updates produce a new assembly revision/proposal, never mutate an already evaluated assembly silently.

## 4. Joint model

Primary semantic joints:

```text
Fastened, Revolute, Slider, Cylindrical, Planar, Ball, PinSlot
```

Each joint contains two connector frames derived from datums or topology references, allowed degrees of freedom, offsets, limits, initial coordinate, and optional motion parameters.

Primitive constraints are supported through the same normalized equation model for advanced use.

### ASM-005 — Connector frames are semantic

Joints reference stable connector frames, not transient closest points. Automatic connector inference is a proposal whose accepted result is stored explicitly.

### ASM-006 — Limits and state separation

Joint definition/limits are canonical; a current interactive pose may be workspace state or an explicitly saved assembly state. Dragging does not rewrite every instance transform as independent unconstrained truth.

## 5. Solver port

```text
AssemblySolveInput
  occurrence graph
  connector frames
  joint/constraint equations and limits
  grounded conditions
  initial pose
  numerical profile

AssemblySolveResult
  transforms by InstancePath
  joint coordinates
  remaining DOF/modes
  residuals
  conflicts/redundancies
  conditioning diagnostics
```

### ASM-007 — Solver/kernel separation

The assembly solver consumes frames and equations, not mutable OCCT shapes. Exact geometry is queried separately for connector evaluation, interference, and contact.

### ASM-008 — No silent constraint removal

As with sketches, over-constraint recovery never drops a relationship silently. Repair suggestions are explicit commands.

### ASM-009 — Flexible subassembly policy

Subassemblies declare rigid or flexible evaluation. Rigid instances reuse a solved internal representation; flexible instances expand required internal degrees of freedom with scoped instance paths.

## 6. Lightweight representations

Each definition may publish:

```text
bounds
coarse mesh
production mesh
exact BREP artifact
connector/datum summary
mass-property summary
```

Assembly navigation and broad-phase selection/interference use the cheapest sufficient representation. Operations requiring exact topology request BREP asynchronously.

### ASM-010 — Representation honesty

Approximate LODs may support navigation and broad-phase analysis but MUST NOT produce exact clearance, mass, or manufacturing claims without labeled approximation.

### ASM-011 — Demand loading

Assembly evaluation publishes bounds and available representation manifests before loading exact BREP. View, selection, and analysis demand schedule higher representations by priority and memory budget; background loading cannot evict visible or leased data.

## 7. Motion, interference, exploded states, and BOM

- Dragging applies a temporary target and solves the joint system interactively.
- Interference uses broad-phase bounds/meshes then exact narrow-phase geometry, reporting revision/configuration.
- Exploded states store semantic instance displacement rules, not baked duplicate geometry.
- BOM is a query/projection over effective occurrences, configurations, suppression, and overrides; quantities are generated rather than hand-maintained.

## 8. Verification strategy

Generate kinematic graphs from known mechanisms and equation systems. Properties include:

- rigid-world-transform invariance;
- instance/tree order independence;
- residual satisfaction and correct DOF count;
- conflict detection without dropped joints;
- repeated definitions share artifact identities;
- save/reload and branch update preserve occurrence identity;
- solver result never mixes component revisions;
- broad-phase never reports “no interference” when an exact candidate was omitted incorrectly.

Mechanism generators cover chains, loops, symmetric linkages, under/over-constrained graphs, nested rigid/flexible subassemblies, and limit boundaries. Curated real assemblies supplement them.

## 9. Open decisions

- **ASM-OPEN-001:** Assembly solver/library and license.
- **ASM-OPEN-002:** External-link storage and offline resolution UX.
- **ASM-OPEN-003:** Connector-frame schema and topology fallbacks.
- **ASM-OPEN-004:** Rigid/flexible subassembly evaluation strategy.
- **ASM-OPEN-005:** Exact interference backend and tolerance semantics.

## 10. Definition of done

Assembly v1 is implemented when generated mechanisms pass solver conformance, occurrence identity is stable through nested/repeated structures, lightweight navigation does not load unnecessary BREP, and BOM/interference/motion results state exact effective revisions and approximation levels.
