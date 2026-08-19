# ADR-0009: Native build123d Functions Are Canonical Part Geometry

- **Status:** Accepted
- **Date:** 2026-08-19
- **Supersedes:** [ADR-0002](0002-semantic-document-is-canonical.md)
- **Related:** `DOC-006`–`DOC-011`, `PY-001`–`PY-014`, `VER-004`–`VER-009`

## Context

Kearne must let AI author unrestricted native build123d while keeping geometry editable, asynchronous, versioned, and usable from the GUI. A second native-feature graph would make source and UI disagree. Requiring arbitrary Python to reverse into feature dialogs is not generally possible.

## Decision

Canonical project state is a versioned content tree plus typed engineering records. Part geometry is defined by native Python/build123d source functions with declared inputs, named outputs, dependencies, environment, and topology-publication capability. Function bodies may use any valid Python and mix build123d algebra and builder modes.

The GUI, AI, scripts, plugins, and headless clients mutate the same source/function graph through the Engineering API. GUI tools generate or structurally edit native source. Recognized source patterns receive specialized graphical editors; all valid functions retain generic source and parameter editing. Failure to recognize a function never rewrites or disables it.

Assemblies, configurations, materials, simulation studies, drawings, BOMs, and release records remain typed engineering records referencing named function outputs. They do not duplicate part geometry.

OCCT BREP, meshes, generated UI projections, parsed syntax trees, and evaluation state are derived.

## Consequences

- AI is not limited to Kearne's current GUI feature catalog.
- Function boundaries are the units of dependency tracking, evaluation, diff, and merge.
- Source inspection never executes user code; evaluation occurs in isolated pinned workers.
- A function without explicit topology labels has body-level identity only. Downstream subshape references are not promised stable.
- Specialized GUI round-tripping is a capability of recognized source, not a condition of valid source.
- Project history may retain syntactically or geometrically failed source revisions without publishing their outputs.
- Source and typed project records share one revision DAG and atomic transaction boundary.

## Alternatives rejected

- Parallel native-feature and Python models: creates conflicting authorities and lossy synchronization.
- Restricting Python to a reversible subset: constrains AI and excludes ordinary build123d techniques.
- Treating each Python statement as a feature: control flow, helpers, classes, and builder contexts have no stable one-to-one GUI inverse.
- Raw BREP as state: loses source intent, parameters, and reproducible evaluation.
- Python as the entire product model: build123d does not define persistent assembly, study, drawing, BOM, or release semantics.
