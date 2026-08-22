# Sketch constraint inventory

This inventory owns user-visible constraint semantics. Source helpers, domain
types, wire variants, command descriptors, solver enrollment, glyphs, Inspector
fields, and generated conformance data must agree with it.

## Declaration states

| State | Solver equation | Canvas | Edit rule |
|---|---:|---|---|
| Driving | Yes | Normal glyph or dimension | Value and operands editable |
| Reference | No | Parenthesized measured dimension | Dimensional constraints only |
| Suppressed | No | Muted glyph | Can be reactivated |
| Redundant | Yes | Warning glyph | Explicit suppress, reference, edit, or delete |
| Conflicting | Unresolved | White on error badge | Explicit repair, suppress, edit, or delete |

Malformed source is refused before declaration or solve. Selection and hover
are presentation states, not declaration states.

## Geometric constraints

| Kind | Operands | Meaning | Marker anchor |
|---|---|---|---|
| Coincident | Two points | Points share one position | Shared solved point |
| Point on object | Point, curve | Point lies on the curve | Solved point |
| Horizontal | Line | Endpoints share Y | Line midpoint |
| Vertical | Line | Endpoints share X | Line midpoint |
| Parallel | Two lines | Directions are parallel | Nearest paired midpoints |
| Perpendicular | Two lines | Directions are orthogonal | Nearest intersection or midpoint pair |
| Tangent | Two compatible curves, internal/external mode | Curves share a tangent at contact | Solved contact region |
| Concentric | Two centered curves | Centers coincide | Shared center |
| Equal | Two lines or two radial curves | Lengths or radii match | Paired curve midpoints |
| Midpoint | Point, line | Point lies at line midpoint | Solved point |
| Symmetric | Two points, line axis | Points mirror across the axis | Axis projection between points |
| Point symmetry | Two points, center point | Points mirror about the center | Center point |
| Lock | Point, X and Y values | Point position is fixed | Locked point |
| Block | Object | Object shape and position are fixed | Object visual center |
| Group | Two to 1,024 entities | Members retain relative pose | Group visual center |
| Collinear | Two lines | Lines share an infinite support line | Nearest paired midpoints |
| Snell | Two ray endpoints, boundary curve, positive ratio n2/n1 | Incident and refracted angles obey Snell's law | Shared boundary point |

Horizontal/vertical inference chooses one ordinary constraint from the line's
dominant solved direction. It is not a separate persisted kind.

## Dimensional constraints

| Kind | Operands | Driving value | Reference measurement | Graphic |
|---|---|---|---|---|
| Distance | Two points | Nonnegative length | Euclidean distance | Aligned dimension |
| Horizontal distance | Two points | Signed length | X difference | Horizontal dimension |
| Vertical distance | Two points | Signed length | Y difference | Vertical dimension |
| Radius | Circle or circular arc | Positive length | Radius | Radial leader |
| Diameter | Circle or circular arc | Positive length | Twice radius | Diameter leader |
| Angle | Two lines | Angle | Directed line angle | Angular arc |

Displayed values use the project unit profile without changing SI source values.
Editing accepts the shared quantity/expression path. A reference-to-driving edit
uses the current measured value when the user does not supply another.

## Presentation and interaction

- Glyphs and text remain the same logical size while zooming and use native
  vector rendering.
- Errors outrank ordinary markers; selected and hovered associations receive
  emphasis without changing declaration state.
- Independent controls show or hide geometric constraints, dimensions, and
  reference dimensions.
- Constraints sharing one semantic anchor use bounded screen-space lanes ordered
  by diagnostic priority and stable declaration order.
- Hovering or selecting a marker highlights every operand. Hovering or selecting
  geometry reveals its constraints. Canvas, Structure, and Inspector select the
  same stable constraint ID.
- Delete removes the declaration through one source command. Conversion between
  driving/reference and active/suppressed is atomic and undoable. No diagnostic
  action silently changes intent.

## Enablement gate

A kind remains unavailable until source parse/emit/edit, domain validation,
wire round-trip, solver equation or reference measurement, health projection,
marker placement, glyph rendering, picking, commands, undo/redo, and generated
valid/invalid/dense conformance cases pass. Snell additionally requires analytic
curve-normal and total-internal-reflection diagnostics.
