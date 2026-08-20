# Computer-Aided Manufacturing

- **Status:** Proposed; post-modeling
- **Requirement prefix:** `CAM`
- **Depends on:** [Modeling](03-solid-modeling.md), [topology](../foundations/04-persistent-topology.md), [materials](19-materials-and-standard-components.md), [jobs](../foundations/03-evaluation-and-jobs.md), [interchange](04-import-export.md)
- **Unblocks:** verified toolpaths and machine-code export

## 1. Purpose

Turn revisioned design intent into inspectable, simulated, and postprocessed manufacturing plans without making generated toolpaths or machine code canonical project state.

Kearne CAM is planning assistance. A user remains responsible for machine setup, tooling, workholding, limits, and safe execution.

## 2. Canonical records

```text
ManufacturingPlan
Setup
Stock
Fixture
MachineProfileRef
ToolAssemblyRef
MachiningOperation
PostProfileRef
```

Records reference immutable design revisions/configurations and semantic topology. Toolpaths, removal meshes/BREPs, simulation traces, setup sheets, and NC files are derived artifacts.

### CAM-001 — Revision-pinned setup

A setup fixes design revision/configuration, work coordinate system, stock, fixtures, machine limits, tool library versions, tolerances, and post profile. A changed dependency marks affected operations stale; it never mixes results from two revisions.

### CAM-002 — Explicit coordinate chain

Part, stock, fixture, setup, machine, and work-offset transforms are named right-handed frames. Export reports the complete resolved transform chain and units.

### CAM-003 — Semantic geometry selection

Operations target faces, edges, holes, pockets, contours, or bounded regions by persistent semantic references. Broken or ambiguous resolution blocks regeneration instead of machining a guessed entity.

## 3. Initial milling scope

The first complete workspace implements `CAM-3X-1`. Each operation declares tool, geometry, heights/depths, stepdown/stepover, entry/exit, direction, allowance, coolant, spindle, feed, and linking policy.

### CAM-004 — Capability negotiation

Machines, tools, strategies, simulation, and posts declare supported axes, units, limits, cycles, compensation, and output capabilities. Unsupported combinations fail before path generation.

### CAM-005 — No hidden machining defaults

Feeds, speeds, clearances, tolerances, stock allowances, and retract behavior resolve through visible versioned policy. Missing safety-critical values block generation; AI suggestions remain proposals with provenance.

### CAM-010 — Versioned strategy profiles

Graphical support is advertised by strategy-descriptor version and cumulative profile:

| Profile | Required workflow |
|---|---|
| `CAM-3X-1` | setup/stock/fixture/WCS; machine and tool assemblies; facing; 2D contour with holding tabs; pocket; drilling; chamfer; adaptive clearing; operation reorder/suppress; explicit lead, ramp, retract, and linking; removal/collision simulation; setup sheet; whole-plan post |
| `CAM-3X-2` | slot and helical bore; thread milling; engraving/deburr; probing; rest machining; operation templates; selected-operation post |
| `CAM-3D-1` | bounded 3-axis surface, waterline, and finishing strategies with holder-aware simulation |

Each strategy row declares supported geometry, stock assumptions, machine axes, tool types, compensation, entry/linking policies, safety findings, approximation level, post capabilities, and generated test domain. Rotary and simultaneous multi-axis work require later profiles and are unavailable until declared.

### CAM-011 — Path modification remains semantic

Tabs, leads, ramps, boundaries, transforms, arrays, and rest-machining inputs are fields or typed operations with source-operation mapping. They cannot be anonymous edits to generated move lists. Copying or patterning an operation allocates new identity and explicit dependencies.

### CAM-012 — Program controls

Comments and optional/program stops are typed plan records interpreted through post capability negotiation. Raw controller text is disabled by default; an enabled escape records provenance, bypassed validations, and makes the affected export unverified unless a controller-specific validator proves it.

## 4. Generation and simulation

Strategy workers consume neutral bounded manifests and immutable geometry artifacts. Published paths contain ordered moves with coordinate frame, motion kind, feed/spindle state, tool identity, and source-operation mapping.

### CAM-006 — Safety invariants

Rapid motion below declared clearance, tool/holder/fixture/machine collision, limit violation, invalid tool engagement, and uncut or gouged regions are structured findings. A failed or incomplete simulation cannot be labeled verified.

### CAM-007 — Material-removal evidence

Simulation records stock state, path digest, tool geometry, tolerance, collision completeness, remaining stock, gouges, and comparison to the target. Approximate voxel/mesh results declare resolution; exact claims require an exact-capable backend.

### CAM-008 — Ordered dependencies

Operations consume the stock result of declared predecessors. Reordering is a command that invalidates downstream paths and simulations. Independent setups may evaluate concurrently without sharing mutable backend state.

## 5. Postprocessing and export

Posts execute in an isolated, capability-limited worker against a versioned neutral path schema. NC output is written atomically, then parsed or linted against the selected controller profile where supported. The export report includes source/path/post/machine/tool digests, units, warnings, line count, and output digest.

### CAM-009 — Postprocessor isolation

A post cannot access arbitrary project files, network, credentials, or the live document. Failure retains the prior export and complete diagnostics.

## 6. Verification

Generated analytic pockets, holes, islands, contours, stocks, fixtures, and transform variants feed every strategy adapter. Properties verify unit and rigid-transform invariance, containment, tolerance/allowance bounds, monotonic stock removal, safe linking, operation-order invalidation, and deterministic path identity.

Simulation uses deliberately colliding fixtures, holders, limits, gouges, and unreachable regions. Post conformance parses emitted programs and compares motion/state semantics, not text formatting. Fuzzers cover path manifests, tool libraries, and post output parsers; fault injection covers cancellation, crashes, and atomic export.

Every strategy-profile row runs the same setup → generate → simulate → post state machine. It varies operation suppression/reordering, work offsets, tabs, entry/linking, tool/holder dimensions, fixtures, stale topology, missing policy, and unsupported machine/post combinations.

## 7. Performance and cancellation

### CAM-013 — Bounded manufacturing jobs

`CAM-REF-1` measures queue, path generation, first simulation update, complete removal simulation, and post latency separately. It records geometry size, operation and move counts, stock representation/resolution, collision pairs, worker peak memory, artifact bytes, and cancellation time. The release targets live in the performance plan.

Path and removal artifacts scale with declared moves and stock cells, not operation-history snapshots. Independent setups may run concurrently within the shared CPU/memory budget. Generation, simulation, and post jobs acknowledge cancellation within the interactive budget; uninterruptible adapters run in terminable dedicated workers. No streamed preview or partial NC file becomes current.

## 8. Acceptance

From a modeled component, create stock and fixture, choose a versioned machine/tool assembly, generate facing/pocket/drilling and a lead/ramp-controlled contour with holding tabs, reorder/suppress an operation, simulate removal and collisions, inspect remaining stock, and post one controller program. The same plan and findings are queryable from UI, headless API, Python, and AI.

## 9. Open decisions

- **CAM-OPEN-001:** Strategy kernel and OpenCAMLib or replacement boundary.
- **CAM-OPEN-002:** Neutral toolpath/removal artifact schemas.
- **CAM-OPEN-003:** First supported machine/controller/post profiles and qualified review.
- **CAM-OPEN-004:** Tool/material feeds-and-speeds sources, licenses, and liability wording.
- **CAM-OPEN-005:** Exact versus approximate removal/collision profiles and thresholds.

## 10. Definition of done

A CAM strategy is supported only when generation, stale invalidation, simulation, machine-limit/collision checks, post conformance, atomic export, cancellation/fault behavior, and stated approximation profile pass on supported platforms.
