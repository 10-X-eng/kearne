# Visual Design System

- **Status:** Proposed; reference image is non-binding
- **Requirement prefix:** `VIS`
- **Depends on:** [Qt/QML application shell](15-qt-qml-application-shell.md), [rendering and selection](01-rendering-and-selection.md)
- **Unblocks:** consistent production UI without per-screen styling

## 1. Purpose

Translate the clean, dense intent of [`rendering/ScreenShot.png`](../../rendering/ScreenShot.png) into reusable Qt/QML rules. The image is evidence of visual direction, not a specification. Requirements come from this plan and later user validation.

## 2. Observed composition

At 1682×1027 pixels, the reference shows:

- a project bar with product identity, project, branch/version state, search, people, and sharing;
- workspace tabs below the project bar;
- a contextual command ribbon with icon-and-label tools;
- a three-pane body: structure/history, viewport, and command/properties/AI;
- viewport overlays for breadcrumb, orientation, selection, and camera actions;
- a bottom status strip for model health, sketch state, jobs, frame rate, and snapping.

Approximate reference proportions are 16% left panel, 64% viewport, and 20% right panel. They explain the hierarchy but are not target dimensions.

## 3. Visual intent to retain

### VIS-001 — Viewport dominance

The viewport receives remaining space after bounded side panels. Panels are resizable, collapsible, and persist as user/workspace layout state. They cannot reduce the viewport below its usable minimum without collapsing or overlaying.

### VIS-002 — Quiet chrome

Chrome uses neutral surfaces, one primary accent, thin separators, limited elevation, and no decorative gradients. Color communicates selection, state, or action priority; it does not compensate for inconsistent spacing.

### VIS-003 — Dense alignment

Controls, labels, values, icons, and separators align to shared spacing and baseline tokens. Numerical/property columns use tabular alignment. Compact density remains readable at supported scale factors.

### VIS-004 — Progressive disclosure

The shell shows workspace and context-relevant commands. Unsupported, irrelevant, or low-frequency commands move to search/overflow instead of remaining as disabled clutter.

### VIS-005 — One local primary action

A command editor or proposal card has at most one visually primary action. Destructive, cancel, secondary, and navigation actions use distinct semantic styles.

### VIS-006 — Persistent engineering state

Revision, configuration, units, material, mass, evaluation health, job activity, AI authority, and stale state use live typed projections. Placeholder numbers or decorative status indicators cannot ship.

## 4. Normalized layout

Starting tokens for prototype measurement at 100% scale:

| Region | Initial token | Rule |
|---|---:|---|
| Project bar | 44 dp | May integrate with native title bar only after window-management spike |
| Workspace navigation | 36 dp | Horizontal overflow or compact switcher at narrow widths |
| Context command strip | 72 dp | Grouped commands; overflow retains keyboard/search access |
| Status strip | 28 dp | Summaries only; details open inspectable panels |
| Left panel | 256 dp default | 200–420 dp, collapsible |
| Right panel | 336 dp default | 300–480 dp, collapsible or overlay |
| Spacing grid | 4 dp base | Primary spacing steps: 4, 8, 12, 16, 24, 32 dp |
| Standard field/action | 28–32 dp compact | Comfortable/accessibility density increases target size |

These values are design-system inputs, not constants scattered through QML. User testing, font metrics, localization, accessibility, and platform behavior may change them.

### VIS-007 — Density-independent geometry

Layout uses Qt logical units and device-pixel-aligned separators. Fractional scaling must not blur one-device-pixel rules or truncate glyphs.

### VIS-008 — Responsive panels

The three-pane layout is not fixed. At reduced width, secondary panels collapse, overlay, or become tabs. Command search and keyboard access remain available when ribbon groups overflow.

### VIS-009 — Native window behavior first

Custom title-bar integration cannot weaken system menus, drag/resize, window snapping, accessibility, input methods, or desktop integration. Kearne accepts platform-specific outer chrome when exact custom framing would break those behaviors.

## 5. Tokens

One versioned theme object supplies:

```text
surface, border, text, muted text, accent, focus, selection
success, warning, error, stale, AI/provenance colors
spacing, radius, separator width, elevation
UI/data font families, sizes, weights, line heights
icon sizes/strokes
control heights and focus-ring geometry
animation durations and reduced-motion policy
```

### VIS-010 — Semantic colors

Components request roles such as `selection`, `warning`, or `stale`; they do not embed color literals. Every state also has text, icon, shape, or accessible-description evidence.

### VIS-011 — Typography roles

Use a UI family for navigation/forms and a data/monospaced role only for expressions, identifiers, aligned numbers, and diagnostic detail. Text is never converted to images or manually spaced glyphs.

### VIS-012 — Contrast and scaling

Text, controls, focus, and state indicators meet the accepted accessibility contrast profile in light and dark themes. The exact low-contrast gray text in the reference may be raised to pass. Text scaling and localization may expand controls instead of clipping.

## 6. Reusable component set

The shell should be composed from a small catalog:

```text
application/project bar
workspace tabs and command groups
button, icon button, segmented action, menu trigger
quantity/expression/reference/enum fields
structure tree and revision-aware list
panel, splitter, section, empty/loading/error state
status chip and diagnostic/job summary
command editor and AI proposal card
viewport HUD, breadcrumb, orientation and navigation controls
tooltip, shortcut hint, focus ring, context menu
```

### VIS-013 — State-complete components

Each interactive primitive defines normal, hover, pressed, focused, selected, disabled, pending, stale, warning, and error states that apply. Feature screens compose these primitives rather than restyle Qt controls independently.

### VIS-014 — Descriptor-driven forms

Quantity, expression, reference, enum, boolean, collection, and validation fields render from Engineering API descriptors. Extrude, hole, simulation, and plugin panels reuse those editors; specialized interactions wrap them without duplicating value/state logic.

### VIS-015 — Icon system

Icons share a vector grid, stroke/fill policy, optical sizing, semantic naming, and light/dark treatment. Text labels and tooltips remain available; icon shape alone is not the command contract.

## 7. Reference elements requiring correction

| Reference element | Assessment | Normalized behavior |
|---|---|---|
| Three-pane shell and viewport overlays | Achievable in Qt Quick | Responsive, resizable, keyboard accessible; no fixed screenshot widths |
| Sparse blue/gray visual language | Achievable | Tokenized themes with measured contrast; exact colors remain non-binding |
| Dense tree, property form, ribbon | Achievable | Compact and comfortable density modes; text/localization may change metrics |
| Hairline separators and grid | Achievable with care | Align to device pixels under fractional DPI; accept platform raster differences |
| Pixel-identical Windows/Linux rendering | Not achievable or desirable | Preserve hierarchy and tokens; font hinting, native chrome, and GPU output may differ |
| `⌘K` shortcut | Incompatible with target platforms as shown | Display the platform binding, initially `Ctrl+K` on Windows/Linux |
| Avatars and `Share` | Not available in local MVP | Show only when collaboration/account capability is configured |
| `main`, `v3`, relative edit time, rollback | Partly available by phase | Bind to revision/branch contracts; distinguish revision undo from feature/body-tip editing |
| All workspaces and advanced tools visible | Misstates phased scope | Registry shows available capabilities; search may label planned/documentation items outside release builds |
| `Join/Cut/Intersect` Extrude operations | Incomplete semantic scope | Add `New Body`; `Join` maps to semantic `Add`; non-new operations expose explicit target bodies |
| Draft field in MVP Extrude | Outside current MVP feature contract | Hide until the draft evaluator/schema is supported; never retain a nonfunctional field |
| Profile displayed only as `Sketch003` | Insufficient for multiple profiles | Show semantic profile-loop references, selection count, missing/stale state, and inspect action |
| Expression plus resolved `25 mm` | Correct direction | Use the shared expression/quantity editor with dimension validation and effective value |
| `M5_clearance` resolved as `5.30` | Unit is visually ambiguous | Display/inherit the unit explicitly and retain standards database identity/version |
| “Sketch003 fully constrained” plus “4 unconstrained holes” | Semantically contradictory as written | AI names the exact unconstrained sketch placements or says hole positions lack driving relations; model-health projection is the authority |
| AI state `local · can modify` | Achievable only with policy truth | Display provider/privacy/capability from the current permission context; actions still use preview/confirmation policy |
| `58 fps`, face count, mass, and job count | Achievable as asynchronous telemetry | Mark pending/stale/approximate values; do not use decorative fixed numbers |
| Fixed AI card at lower right | Achievable but space-sensitive | Collapsible panel/section with focus, history, resize, and small-window behavior |
| Frameless integrated project bar | Platform-dependent | Retain native frame initially unless the viewport spike proves custom chrome behavior |

## 8. Cleanliness constraints

### VIS-016 — No decorative duplication

One state has one primary home. The project bar summarizes project/revision; the status strip summarizes health/jobs; panels provide detail. The same status is not repeated in ribbon, tree, viewport, and footer without a distinct interaction need.

### VIS-017 — Stable motion

Animations explain state change and do not move primary controls during pointer interaction. Geometry preview and job completion cannot cause panel width, command position, or focus to jump. Reduced-motion policy disables nonessential transitions.

### VIS-018 — Empty, loading, stale, and failed are designed states

The striped empty viewport in the reference is replaced by explicit states tied to evaluation: no model, loading, current, stale last-known-good, failed, and unavailable evaluator. Each state exposes the next valid action or diagnostic.

### VIS-019 — Information has provenance

AI badges, edit badges, mass, material, face counts, constraint status, branch/version, and job summaries link to their source entity, revision, evaluation, or actor. Decorative labels cannot imply unsupported engineering certainty.

## 9. Verification

- A component catalog renders every component/state in light/dark, compact/comfortable, supported scale factors, long localized text, keyboard focus, and reduced motion.
- Layout property tests vary window dimensions, panel sizes, text expansion, and DPI; controls cannot overlap, become unreachable, or reduce viewport below policy without responsive transition.
- Accessibility tests inspect roles, names, focus order, contrast, state alternatives, and keyboard completion.
- Architecture checks reject color/spacing/font literals outside the token/design-system layer, with named exceptions for viewport data visualization.
- Controller tests bind live revision, job, diagnostic, unit, AI capability, and stale states; no placeholder status survives release builds.
- A small perceptual smoke set catches catastrophic styling/rendering changes. The reference screenshot is never a golden image.
- User studies evaluate command discovery, information density, error recovery, and long-session fatigue before token freeze.

## 10. Performance

Theme/layout updates must not rebuild unchanged document projections or render meshes. Repeated tree rows and property editors use virtualization when data exceeds the component threshold. Animation, blur, shadow, and translucent overlay costs require GPU traces on minimum hardware before inclusion.

## 11. Open decisions

- **VIS-OPEN-001:** Font families, licenses, metrics, and fallback coverage.
- **VIS-OPEN-002:** Exact light/dark color and contrast tokens.
- **VIS-OPEN-003:** Compact/comfortable control-size profiles.
- **VIS-OPEN-004:** Native versus custom title bar per platform.
- **VIS-OPEN-005:** Responsive breakpoints derived from workflow tests rather than the reference image.
- **VIS-OPEN-006:** Icon production workflow and contributor rules.

## 12. Definition of done

The visual system is implemented when the reference hierarchy can be reproduced from shared components without per-screen style forks; target-platform, DPI, localization, accessibility, and responsive catalogs pass; and every non-achievable reference element follows the normalized behavior above.
