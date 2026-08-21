# ADR-0003: Immutable Revision DAG for Undo and History

- **Status:** Accepted
- **Date:** 2026-08-19
- **Related:** [Commands, transactions, and revisions](../foundations/02-commands-transactions-revisions.md)

## Context

Imperative command objects with custom `undo()` state do not survive process crashes cleanly and duplicate mechanisms needed for versions, branches, merge, replay, and collaboration.

## Decision

Each committed transaction creates one Git commit containing the complete project tree and normalized transaction record. Workspaces and branches are Git refs. Undo/redo moves a workspace ref; editing after undo creates a divergent commit while a retention ref preserves redo history. [ADR-0021](0021-git-project-packages.md) defines storage and packaging.

## Consequences

Git commit identity and refs ship in the first format. Command normalization remains deterministic and idempotent. Retention roots prevent promised history from becoming unreachable. User-facing branch/merge features can arrive later without replacing undo.

## Evidence required

Generated command/repository state machine, fault-safe package tests, and measured long-history behavior must pass before Save/Open is enabled.
