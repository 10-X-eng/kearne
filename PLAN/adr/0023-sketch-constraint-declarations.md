# ADR-0023: Sketch Constraint Declarations Own Intent

- **Status:** Accepted
- **Date:** 2026-08-22
- **Related:** `SKH-001`–`SKH-006`, `SKH-009`–`SKH-012`, `RND-010`, `RND-017`, ADR-0017, ADR-0018

## Context

A constraint needs stable identity, human naming, activation, dimensional mode,
and typed operands. Storing those facts in UI state or a solver side table would
create a second sketch model and lose them through source edits, history, and
project transfer. Solver rows, residuals, conflict sets, glyph positions, and
selection colors are revision-scoped results, not design intent.

Professional CAD systems make constraint badges and dimensions selectable,
link them to affected geometry, distinguish errors and references, and provide
visibility controls. Coincident badges need deterministic separation to remain
selectable.

## Decision

Each recognized source constraint is one declaration containing a UUIDv7 ID,
human label, active or suppressed lifecycle, typed operands, and its
kind-specific payload. A dimensional declaration is driving with a typed value
or reference with a derived measurement; geometric constraints cannot be
reference constraints. Suppressed and reference declarations do not contribute
solver equations. Changing lifecycle or dimensional mode is a structural source
edit against the expected digest.

Constraint status is a derived projection keyed by declaration ID. It combines
source validation, solver residuals, redundancy and conflict sets, and reports
driving, reference, suppressed, redundant, or conflicting without exposing
solver row numbers. Malformed source is rejected before it becomes a
declaration.

Canvas markers are derived from the exact solved scene and status projection.
Their native vector glyphs, dimension graphics, layout, visibility, hover, and
selection state are presentation products. Picking resolves the displayed
marker to its declaration ID before invoking the same typed commands used by
source, AI, Python, and plugins. Marker coordinates, draw order, and GPU handles
never become constraint identity or geometry intent.

Marker size is screen-stable. Constraints with the same semantic anchor use
bounded deterministic screen-space lanes ordered by diagnostic priority and
stable declaration order. Separate controls show or hide geometric
constraints, dimensions, and reference dimensions. Presentation failure cannot
change the solve result.

## Consequences

- Source, wire, history, solver input, Structure, Inspector, and canvas share
  one constraint declaration and stable identity.
- Existing generated source without labels or lifecycle fields receives
  deterministic compatibility defaults without rewriting until edited.
- Reference dimensions require measurement projection after solving.
- Constraint layout and marker picking join render publication budgets and
  exact-frame selection tests.
- A filtered Structure/Inspector projection replaces a separate mutable
  constraint-manager model.

## Alternatives rejected

- UI-only labels, modes, or suppression lose intent outside one workstation.
- Solver-owned constraint records expose implementation order and cannot survive
  solver replacement.
- Persisted glyph coordinates mix camera-dependent layout with engineering
  source.
- Always showing every marker makes dense sketches harder to read and select.

## Evidence

[Fusion constraint and dimension guidance](https://help.autodesk.com/cloudhelp/ENU/Fusion-Sketch/files/SKT-SKETCH-CREATE-DIMENSIONS.htm),
[Onshape constraint behavior and manager](https://cad.onshape.com/help/Content/Sketch/working_with_constraints.htm),
and [SOLIDWORKS relation management](https://help.solidworks.com/2025/english/SolidWorks/sldworks/HIDD_DVE_SK_EDIT_RELATIONS.htm?format=P&value=)
were reviewed for badge, inference, selection, status, filtering, suppression,
and repair behavior. Kearne keeps typed declarations, solve results, immutable
marker packets, and native vector presentation separate while linking them by
stable constraint ID.
