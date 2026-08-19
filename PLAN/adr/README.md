# Architecture Decision Records

ADRs record decisions that constrain multiple plans, persisted data, public APIs, or expensive dependencies. They do not restate subsystem plans.

## Status

```text
Proposed -> Accepted -> Superseded
                    \-> Rejected
```

An accepted ADR is immutable except for status and links. Reversal creates a new ADR that supersedes it.

## Index

| ADR | Status | Decision |
|---|---|---|
| [0001](0001-kearne-product-identity.md) | Accepted | Product identity is Kearne |
| [0002](0002-semantic-document-is-canonical.md) | Accepted | Semantic document is canonical state |
| [0003](0003-immutable-revision-dag.md) | Proposed | Undo/history use an immutable revision DAG |
| [0004](0004-one-engineering-api.md) | Proposed | All actors use one Engineering API |
| [0005](0005-worker-artifact-boundary.md) | Proposed | Risky runtimes cross an immutable artifact boundary |
| [0006](0006-codex-app-server-harness.md) | Accepted | Codex app-server is the AI harness |
| [0007](0007-agent-observable-desktop.md) | Accepted | Desktop work is agent-observable |

## Template

```text
# ADR-NNNN: Title

Status: Proposed | Accepted | Rejected | Superseded
Date: YYYY-MM-DD
Owners: accountable roles
Related: plan/requirement links

## Context
Only facts and forces needed for the decision.

## Decision
One precise choice.

## Consequences
Required benefits, costs, constraints, and follow-up.

## Alternatives rejected
Alternatives actually evaluated and why they lost.

## Evidence
Spike/results/review links. Required before acceptance for empirical choices.
```
