# ADR-0003: Immutable Revision DAG for Undo and History

- **Status:** Proposed
- **Date:** 2026-08-19
- **Related:** [Commands, transactions, and revisions](../foundations/02-commands-transactions-revisions.md)

## Context

Imperative command objects with custom `undo()` state do not survive process crashes cleanly and duplicate mechanisms needed for versions, branches, merge, replay, and collaboration.

## Decision

Each committed transaction creates an immutable revision with normalized semantic mutations. Workspaces and branches point to revisions. Undo/redo moves a workspace head; editing after undo creates a divergent child.

## Consequences

Revision identity and DAG storage ship in the first format. History storage needs checkpoints and retention. Command normalization must be deterministic and idempotent. User-facing branch/merge features can arrive later without replacing undo.

## Evidence required

Generated command/revision state machine, persistence crash prototype, and measured history/checkpoint behavior must pass before acceptance.
