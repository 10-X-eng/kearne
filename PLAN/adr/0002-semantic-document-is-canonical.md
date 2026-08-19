# ADR-0002: Semantic Document Is Canonical

- **Status:** Accepted
- **Date:** 2026-08-19
- **Related:** `DOC-001`–`DOC-018`, `ARCH-003`

## Context

GUI state, Python/build123d source, OCCT BREP, and meshes cannot independently preserve all parametric intent and product structure.

## Decision

Canonical product state is a versioned semantic document of typed entities and references. Native BREP and meshes are derived artifacts. Irreplaceable imports and scripts are source artifacts, not caches.

## Consequences

All persistent edits use semantic commands. The domain remains independent of Qt, OCCT, Python, and storage adapters. Missing evaluators preserve opaque entities and may retain stale fallback geometry.

## Alternatives rejected

- Raw BREP: loses native feature intent and reliable semantic diff.
- Python/build123d program as document: makes ordinary interactive edits and heterogeneous features dependent on one runtime.
- QML object graph: couples durable engineering state to presentation lifecycle.
