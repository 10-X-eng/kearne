# Sheet Metal

- **Status:** Proposed; post-solid-modeling
- **Requirement prefix:** `SMT`
- **Depends on:** [Native source](05-python-and-build123d.md), [solid modeling](03-solid-modeling.md), [topology](../foundations/04-persistent-topology.md), [materials](19-materials-and-standard-components.md)
- **Unblocks:** flat patterns, bend documentation, and manufacturing export

## 1. Purpose

Create folded parts and reproducible flat patterns from native source while applying explicit material, thickness, bend, relief, and manufacturing rules.

## 2. Authority

Native Python/build123d functions remain geometry authority. Graphical sheet-metal tools transform that source or call versioned Kearne sheet helpers that return build123d objects. A `SheetMetalDefinition` record binds the function output to material, thickness, rule set, grain direction, and manufacturing metadata; it does not duplicate the bend/feature graph.

Unrecognized valid source still evaluates as geometry. It receives sheet-metal editing and unfolding only when it publishes the required thickness, bend, and neutral-surface evidence or an explicit flat-pattern output.

### SMT-001 — Uniform-thickness contract

A sheet body has one positive nominal thickness within a declared tolerance unless a capability explicitly supports a transition. Shells, self-intersections, zero-width regions, and ambiguous thickness block flat-pattern claims.

### SMT-002 — One effective bend rule

Each bend resolves exactly one allowance method from bend override, part rule, material/gauge table, or project default. Supported methods are K-factor, direct bend allowance, and bend deduction. The result records the source and version used.

For thickness `t`, inside radius `R`, bend rotation `θ` in radians, and neutral-axis factor `K` measured from the inside face:

```text
BA = θ (R + Kt)
OSSB = (R + t) tan(θ / 2)
BD = 2 OSSB - BA
```

`θ` is the change in flange direction, with `0 < θ < π`. Hem and nonstandard bend types use their declared evaluator, not this symmetric two-leg deduction formula.

### SMT-003 — Rule bounds

Thickness, radius, angle, K-factor, table domain, relief, and minimum web/flange values are finite dimensional inputs. `0 < K < 1`; rule tables interpolate only within their declared axes and policy. Extrapolation requires an explicit warning policy and is recorded.

### SMT-004 — Bend conventions are explicit

Every bend records its stationary region, material side, signed fold direction, inside radius, included/rotation angle convention, and dimension reference: tangent, inside virtual sharp, or outside virtual sharp. Flange length and setback calculations use that stored convention; the UI and drawing cannot reinterpret it.

### SMT-005 — Bend-table resolution

A bend table declares material/condition, gauge or thickness axis, radius/thickness axis, angle domain, grain/process applicability, value kind, interpolation, and edition. Interpolation is deterministic within the declared domain. Extrapolation is refused unless the selected rule explicitly permits and records it.

## 3. Operations

The workspace supports base and contour flanges, edge flange, bend, hem, jog, rip/seam, corner and bend relief, unfold, refold, and flat pattern. Each operation publishes stable bend/relief labels and uses preview generations before one source transaction.

### SMT-006 — Folded/flat correspondence

The evaluator publishes folded body, flat body, bend lines with direction/angle/radius, fixed region, and a bidirectional semantic map. A flat entity without an unambiguous folded source is marked unresolved rather than guessed.

### SMT-007 — Local unfold/refold

Unfold/refold selects a fixed region and bend set. Edits made while unfolded commit ordinary source changes and must either refold through the same mapping or report the precise lost correspondence.

### SMT-008 — Corners and reliefs

Multi-edge flanges store miter, gap, overlap, setback, and corner treatment rather than deriving intent from the resulting solid. Bend/corner relief records type, dimensions, orientation, and applicable bend ends. Invalid or self-intersecting combinations fail with the responsible feature IDs.

### SMT-009 — Manufacturability diagnostics

Evaluation reports bend collisions, overlapping flat regions, relief failures, impossible inside radius, tool-access limits supplied by a manufacturing profile, and grain/rule conflicts. A warning never silently changes geometry or allowance.

### SMT-010 — Compensation is separate

Springback, overbend, tooling, and press-brake sequence data are manufacturing-process records. They do not alter nominal folded design or neutral-axis calculation unless an explicit versioned compensation rule is selected. Flat-pattern and tool-motion outputs state which compensation, if any, they include.

## 4. Derived outputs

Flat BREP, render mesh, bend map, extents, blank area/mass, and DXF are revision-, configuration-, rule-, material-, and evaluator-pinned artifacts. DXF export separates cut, bend-up, bend-down, etch, and annotation layers and reports units and unresolved bends.

## 5. Concurrency and failure

Recognition, folding, unfolding, collision checks, and export run as generation-tagged jobs. The viewport retains the last accepted folded/flat result while current evaluation is pending or failed. Cancellation publishes no partial flat pattern as current.

## 6. Verification

Generate bend chains, boxes, transitions, hems, relief variants, and boundary cases over units, thicknesses, radii, angles, and rule sources. Verify:

- analytic allowance/deduction relations and table precedence;
- rigid-transform and unit invariance;
- fold → unfold → refold correspondence within the numerical profile;
- flat regions do not overlap when classified valid;
- bend order does not change an equivalent final definition;
- edits preserve or honestly break semantic bend labels;
- DXF parsed lengths/layers agree with the flat artifact;
- cancellation, worker crash, and stale results cannot replace last-known-good output.

## 7. Acceptance

Create a material-bound enclosure with flanges, reliefs, hem, and hole; change thickness and bend table; inspect updated folded and flat outputs; locally unfold/edit/refold; export a validated layered DXF and bend table through UI, headless API, Python, and AI using the same commands.

## 8. Open decisions

- **SMT-OPEN-001:** Recognition/unfold backend and licensing.
- **SMT-OPEN-002:** Supported bend-table formats and interpolation policy.
- **SMT-OPEN-003:** First-release corner, hem, lofted/transition, and forming-tool profiles.
- **SMT-OPEN-004:** Qualified manufacturability limits and machine/tooling data sources.

## 9. Definition of done

Sheet metal is implemented when the operation matrix passes analytic and metamorphic suites, folded/flat semantic correspondence survives its edit matrix, rule provenance is complete, invalid geometry cannot produce a manufacturing-ready flat pattern, and the acceptance workflow passes on Windows and Linux.
