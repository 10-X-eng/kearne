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
| [0002](0002-semantic-document-is-canonical.md) | Superseded | Semantic document is canonical state |
| [0003](0003-immutable-revision-dag.md) | Proposed | Undo/history use an immutable revision DAG |
| [0004](0004-one-engineering-api.md) | Accepted | All actors use one Engineering API |
| [0005](0005-worker-artifact-boundary.md) | Proposed | Risky runtimes cross an immutable artifact boundary |
| [0006](0006-codex-app-server-harness.md) | Superseded | Codex app-server is the AI harness |
| [0007](0007-agent-observable-desktop.md) | Accepted | Desktop work is agent-observable |
| [0008](0008-codex-app-server-compatibility.md) | Accepted | Kearne owns app-server compatibility and pins exact builds |
| [0009](0009-native-build123d-function-graph.md) | Accepted | Native build123d functions are canonical part geometry |
| [0010](0010-uuidv7-semantic-identities.md) | Accepted | Stable semantic identities use UUIDv7 |
| [0011](0011-protobuf-engineering-api.md) | Accepted | Engineering wire contracts use Protobuf Editions 2024 |
| [0012](0012-content-addressed-revisions.md) | Accepted | Revision IDs are digests of canonical revision envelopes |
| [0013](0013-canonical-content-encoding.md) | Accepted | Canonical state uses KCE v1 and domain-separated BLAKE3-256 |
| [0014](0014-persistent-project-state.md) | Accepted | Published state uses hidden structurally shared CHAMP maps |
| [0015](0015-command-audit-retention.md) | Accepted | Revisions retain normalized audit facts, not raw requests |
| [0016](0016-in-process-engineering-api.md) | Accepted | In-process adapters use generated wire envelopes |
| [0017](0017-python-ast-source-editing.md) | Accepted | Recognized Python source edits use AST and token spans |
| [0018](0018-typed-si-sketch-boundary.md) | Accepted | Generated sketches use typed SI quantities and bound planes |
| [0019](0019-deterministic-evaluation-scheduler.md) | Accepted | One deterministic scheduler drives every executor |
| [0020](0020-inline-qrhi-sketch-renderer.md) | Proposed | Sketch uses one inline QSGRenderNode/QRhi renderer |

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
Prototype/results/review links. Required before acceptance for empirical choices.
```
