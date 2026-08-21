# Sketch tool inventory

Source audit: `VibeCADNativeActionManifest.py` (`sketch.edit`, `sketch.setup`), `VibeCADRibbon.cpp` (`sketchGroups`, `sketchSetupGroups`), and `VibeCADNativeSketchGeometrySchema.py`. Composite menu commands are containers, not tools.

States:

- **local:** production path exists through source, model, evaluation, and desktop contracts; human GUI gate remains open.
- **core:** reusable source/model/solver support exists; complete tool interaction does not.
- **partial:** only some listed inputs or targets work.
- **missing:** no production path.

Every row must use stable IDs, typed units, structural source edits, exact presented-frame selection, preview/cancel, undo/redo, save/open, refusal diagnostics, generated conformance, and bounded performance before enablement.

## Session, view, and setup

| Tool | Canonical effect | Input | State |
|---|---|---|---|
| New Sketch | Attached Sketch function | Plane or planar face | partial |
| Edit Sketch | Digest-bound edit session | Sketch | local |
| Finish | Atomic validated session commit | Active session | missing |
| Cancel session | Restore entry revision and workspace | Active session | missing |
| Sketch view | Camera normal to attachment | Active Sketch | local |
| Section view | Temporary section presentation | Active Sketch | missing |
| Fit | Camera fit without model edit | Presented geometry | partial |
| Grid | Adaptive unit grid presentation | Camera and unit settings | local |
| Snap | Ranked point, curve, grid, and reference candidates | Pointer and modifiers | partial |
| Map attachment | Replace attachment reference | Plane or planar face | missing |
| Reorient | Replace attachment orientation | Attachment and orientation | missing |
| Validate and repair | Diagnostic set plus explicit repair proposal | Sketch | missing |
| Merge Sketches | New or selected Sketch with copied stable ancestry | Sketch set | missing |
| Mirror whole Sketch | Mirrored objects and constraints | Sketch and axis | missing |

## Geometry

| Tool | Canonical effect | Input | State |
|---|---|---|---|
| Point | Point object | Point | local |
| Line | Line object | Two points | local |
| Polyline | Connected line objects; optional closure | Point sequence | local |
| Center arc | Circular arc object | Center, radius point, endpoints | local |
| Three-point arc | Circular arc object | Two endpoints and rim point | local |
| Elliptical arc | Elliptical-arc object | Center, axes, parameter range | local |
| Hyperbolic arc | Hyperbolic-arc object | Center, axes, parameter range | local |
| Parabolic arc | Parabolic-arc object | Vertex, focus, parameter range | local |
| Center circle | Circle object | Center and radius point | local |
| Three-point circle | Circle object | Three rim points | local |
| Center ellipse | Ellipse object | Center and axes | local |
| Three-point ellipse | Ellipse object | Axis endpoints and rim point | local |
| Corner rectangle | Rectangle object with `bottom`, `right`, `top`, `left` members | Opposite corners | local |
| Center rectangle | Rectangle object with named members | Center and corner | local |
| Oblong | Oblong object | Axis endpoints and width | local |
| Triangle | Regular-polygon object | Center, vertex, 3 sides | local |
| Square | Regular-polygon object | Center, vertex, 4 sides | local |
| Pentagon | Regular-polygon object | Center, vertex, 5 sides | local |
| Hexagon | Regular-polygon object | Center, vertex, 6 sides | local |
| Heptagon | Regular-polygon object | Center, vertex, 7 sides | local |
| Octagon | Regular-polygon object | Center, vertex, 8 sides | local |
| Regular polygon | Regular-polygon object | Center, vertex, side count | local |
| Straight slot | Slot object | Axis endpoints and width | local |
| Arc slot | Arc-slot object | Arc definition and width | local |
| B-spline | Spline object | Control points and degree | local |
| Periodic B-spline | Closed spline object | Control points and degree | local |
| Interpolated B-spline | Cubic spline object | Interpolation points | local |
| Periodic interpolated B-spline | Closed cubic spline object | Interpolation points | local |
| Text | Text object | Text, baseline, font, size, alignment | missing |
| Construction toggle | Replace selected objects' construction role | Objects or curves; `X` | local |

## Dimensions and constraints

| Tool | Canonical effect | Input | State |
|---|---|---|---|
| Auto dimension | Inferred driving dimension | Compatible geometry selection | partial |
| Distance | Driving or reference distance constraint | Line or two point references | partial |
| Horizontal distance | Driving or reference horizontal-distance constraint | Two point references | partial |
| Vertical distance | Driving or reference vertical-distance constraint | Two point references | partial |
| Radius | Driving or reference radius constraint | Circle or arc | partial |
| Diameter | Driving or reference diameter constraint | Circle or arc | partial |
| Angle | Driving or reference angle constraint | Two lines | partial |
| Lock | Explicit position constraint | Point | local |
| Coincident | Coincident constraint | Two point references | local |
| Point-on-object | Point-on-curve constraint | Point and curve | local |
| Horizontal / vertical inference | Horizontal or vertical constraint selected from the line's dominant direction | Line | local |
| Horizontal | Horizontal constraint | Line | local |
| Vertical | Vertical constraint | Line | local |
| Parallel | Parallel constraint | Two lines | local |
| Perpendicular | Perpendicular constraint | Two lines | local |
| Tangent | Tangent constraint with explicit internal/external mode | Compatible curves | local |
| Concentric | Concentric constraint | Two centered curves | local |
| Equal | Equal-length or equal-radius constraint | Compatible geometry pair | local |
| Symmetric | Symmetric constraint | Two point references and a line axis or center point | local |
| Midpoint | Midpoint constraint | Point and line | local |
| Block | Rigid object constraint | Object | local |
| Collinear | Collinear constraint | Two lines | local |
| Group | Rigid group constraint | Two to 1024 geometry entities | local |
| Snell | Refraction constraint | Curves, interface, refractive ratio | missing |
| Driving / reference toggle | Replace dimension mode | Dimensions | missing |
| Active / suppressed toggle | Replace constraint activation | Constraints | missing |

## Modify and reuse

| Tool | Canonical effect | Input | State |
|---|---|---|---|
| Fillet | Trimmed curves plus tangent arc object | Corner or curve pair and radius | partial |
| Chamfer | Trimmed curves plus chamfer line object | Corner or curve pair and distance | partial |
| Trim | Structural curve replacement or deletion | Curve segment | missing |
| Split | Multiple curve objects preserving ancestry | Curve and split points | missing |
| Extend | Structural curve replacement | Curve end and boundary | missing |
| Join | Joined compatible curve object | Ordered curves | missing |
| Project | Associative external-geometry reference | Model geometry | missing |
| Intersection | Associative intersection-point reference | Model geometry | missing |
| Linked Sketch copy | Associative copied-geometry reference | Sketch and objects | missing |
| Translate / rectangular array | Moved objects or one-/two-vector copies with copied or equalized dimensions | Objects, vectors, counts, constraint policy | local |
| Rotate / polar array | Rotated objects or angular copies with copied or equalized dimensions | Objects, center, angle, count, constraint policy | local |
| Scale | Transformed objects with explicit constraint policy | Objects, center, scale | local |
| Offset | Associative or independent offset objects | Curves, side, distance | partial |
| Symmetry | Mirrored objects | Objects and axis | local |
| Remove axis alignment | Delete horizontal and vertical constraints from selected lines | One to 1024 lines | local |
| Copy / cut / paste | Stable-ID-remapped source fragment | Objects and insertion transform | missing |
| Rendering order | Presentation-only object order | Objects and order action | missing |

## B-spline editing

| Tool | Canonical effect | Input | State |
|---|---|---|---|
| Convert to NURBS | Exact NURBS representation | Compatible curve | missing |
| Increase / decrease degree | Exact elevation or bounded lower-degree approximation | Spline and maximum deviation when reducing | local |
| Increase / decrease knot multiplicity | Exact insertion or bounded knot removal | Spline, knot, and maximum deviation when removing | local |
| Insert knot | Shape-preserving knot-vector and control-point replacement | Spline and parameter | local |
| Pole weight | Rational weight replacement | Spline pole and weight | local |
| Control polygon | Presentation toggle | Spline | missing |
| Curvature comb | Presentation toggle | Spline | missing |
| Degree / knot overlay | Presentation toggle | Spline | missing |

## Selection and inspection

| Tool | Canonical effect | Input | State |
|---|---|---|---|
| Hover preselection | Hover overlay only | Pointer; nearest eligible hit | local |
| Select geometry | Selection overlay only | Curve, endpoint, center, profile, reference | partial |
| Select constraints | Selection set | Constraint filter | missing |
| Select associated elements | Selection set | Constraint or geometry | missing |
| Arc overlay | Presentation toggle | Arcs | missing |
| Restore internal alignment | Explicit repair proposal | Derived alignment geometry | missing |
| Virtual-space review | Presentation filter | Suppressed or virtual geometry | missing |
| DOF inspection | Solver-mode overlay and human explanation | Geometry or Sketch | missing |
| Conflict inspection | Stable-ID conflict set and explanation | Sketch | core |
| Redundancy inspection | Stable-ID redundancy set and explanation | Sketch | core |
