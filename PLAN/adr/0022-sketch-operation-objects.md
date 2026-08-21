# ADR-0022: Recognized Sketch Source Retains Operation Objects

- **Status:** Accepted
- **Date:** 2026-08-20
- **Related:** `SKH-001`–`SKH-003`, `SKH-009`, `RND-002`, ADR-0009, ADR-0010

## Context

A compound Sketch tool creates several primitive entities and constraints. Those
values are sufficient for solving but lose the human operation. Reconstructing
that operation from profiles or topology is ambiguous and produced UI labels
such as `Geometry (4)` and `Profile 1` instead of `Rectangle 1`.

## Decision

Recognized Sketch source stores operation objects beside primitive entities and
constraints in `SketchDefinition`. Each object has a UUIDv7 identity, a human
label, a kind, and uniquely named member roles that reference stable entity IDs.

Objects retain intent and ownership; they do not duplicate coordinates or
constraints. Solving and profile extraction continue to consume primitives and
constraints. A primitive belongs to at most one operation object. Standalone
primitives remain valid without an owner.

A compound tool writes its object, primitives, and constraints in one structural
source edit. Edits that preserve the operation preserve its identity and member
roles. An edit that destroys the operation replaces it explicitly with the
resulting human objects; it does not silently keep a false label.

Structure presents operation objects. Member primitives are disclosed only by
expansion or contextual selection. Derived profiles never replace source intent
in Structure. Selection refers to the object and, when applicable, its stable
member entity and point key.

Unrecognized native build123d remains valid and is not assigned inferred
operation objects.

## Consequences

- GUI, AI, source, history, and persistence share the same human object identity.
- Compound tools require source helpers and conformance data for object roles.
- Source and wire schemas gain an operation-object section.
- Profile detection remains derived and cannot name user-created objects.

## Alternatives rejected

- Infer objects from closed profiles: different source can produce the same loop.
- Store labels only in UI state: source edits, undo, and project transfer lose them.
- Treat every primitive as the created object: this discards the command the user
  performed and clutters Structure.
