# ADR-0018: Typed SI Quantities and Bound Sketch Planes

- **Status:** Accepted
- **Date:** 2026-08-19
- **Related:** `PY-003`, `SKH-001`, `SKH-007`

## Context

Kearne's engineering state uses SI units while build123d uses millimetres and degrees at common geometry call sites. Unqualified floats would make source meaning depend on an implicit convention. A build123d `Plane` also carries geometry but no durable document identity.

## Decision

Kearne-generated sketch helpers accept finite `Length` values stored in metres and `Angle` values stored in radians. Source uses explicit constructors such as `mm`, `inch`, `deg`, and `rad`; conversion occurs once when the helper emits build123d geometry.

A generated sketch function receives a `SketchPlane` input containing the evaluated build123d plane and a `ModelBindingId`. The function contract binds that input to a component origin, construction-plane output, or topology reference. Source does not persist a global plane as attachment intent.

Native solve geometry remains plane-local. Evaluation carries `EvaluatedPlaneIdentity { attachment_binding, revision }`; a Sketch render target is `{ render_session, evaluated_plane, evaluation_key }`. The evaluated frame's artifact digest participates in the evaluation key. The plane identity does not enter `sketch::Definition`, solver input, or the Sketch wire schema.

These rules apply only to the recognized helper contract. Arbitrary native build123d functions retain their normal Python and build123d conventions.

## Consequences

- Unit display preferences cannot change source meaning.
- Equivalent units share one canonical value and evaluation key.
- Plane geometry and durable attachment identity cross the function boundary together.
- Render freshness distinguishes the binding, its resolving revision, and the evaluated frame.
- Generated source is longer than raw-float source, so source-edit latency remains a measured gate.

## Alternatives rejected

- Treating helper floats as millimetres conflicts with the SI engineering boundary and makes mixed APIs error-prone.
- Treating helper floats as metres is visually ambiguous and unsafe for hand-authored CAD source.
- Persisting `Plane.XY` records geometry without assembly, history, or topology identity.

## Evidence

Generated tests cover unit equivalence, non-finite refusal, quantity arithmetic, plane attachment refusal, and metre/radian conversion to build123d millimetres/degrees. The canonical 100-entity source fixture remains below the structural-edit budget recorded in [the performance plan](../delivery/04-performance.md).
