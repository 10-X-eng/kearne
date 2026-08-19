# AI System

- **Status:** Proposed
- **Requirement prefix:** `AI`
- **Depends on:** [Engineering API](../foundations/08-engineering-api.md), [commands and revisions](../foundations/02-commands-transactions-revisions.md), [security](../delivery/06-security-threat-model.md)
- **Unblocks:** AI-native workflows and later optimization

## 1. Purpose

Make AI a permissioned, observable author of native build123d source and typed engineering changes. Kearne validates and commits them through the Engineering API; model output is not canonical until committed.

## 2. Components

```text
AI interaction controller
  <-> versioned Codex app-server adapter
  -> context/query budgeter
  -> capability-filtered tool registry
  -> plan/proposal workspace
  -> deterministic policy and approval engine
  -> ordinary Engineering API
```

Codex app-server owns threads, turns, streamed items, authentication, and runtime approvals. Kearne owns engineering permissions, tools, revisions, evidence, and policy. No Codex protocol or model SDK type enters document, command, geometry, persistence, or QML libraries. See the [harness plan](17-codex-app-server-harness.md).

## 3. Capability levels

```text
Read       bounded queries only
Suggest    prose/structured proposal, no preview execution
Preview    ephemeral command transactions and evaluation
Modify     approved persistent command subset
RunTools   approved long-running/export/simulation tools
Autonomous bounded task  isolated workspace, budgets, stop conditions
```

### AI-001 — Least authority

An interaction receives the lowest capability level and narrowest command/query set needed. Provider access, project-data disclosure, filesystem, export, network, Python, and future simulation are separate permissions.

### AI-002 — Revalidation

Every tool call is schema-, permission-, revision-, source-digest-, reference-, and domain-validated by the same Engineering API used by other actors. Prompt instructions cannot bypass policy.

### AI-003 — Revision anchoring

AI context, queries, measurements, plans, and proposed commands name their source revision. If the workspace changes, stale proposals are re-inspected, explicitly rebased where safe, or rejected.

## 4. Context model

AI receives structured, bounded views:

- current selection as typed references and engineering summaries;
- requested source modules, function contracts, calls, and dependency slices;
- parameter values and dimensions;
- diagnostics and model health;
- mass/bounds/measurements from revision-correct evaluators;
- assembly/simulation/drawing data only when permissioned;
- user-provided request and explicitly attached documents.

### AI-004 — Query before bulk disclosure

The controller retrieves only source and typed context relevant to the task rather than full-project serialization, raw BREP, or full meshes. Screenshots complement semantic UI state when visual layout matters. Each query has item, byte, and depth limits and a sensitivity class.

### AI-005 — Untrusted embedded content

Names, comments, imported metadata, plugin text, source, and project records are untrusted data, not controller instructions. Provider prompts delimit them and the policy engine ignores attempts to alter permissions.

## 5. Tool registry

AI tool definitions are filtered/generated from Engineering API descriptors and add:

```text
AI-safe description
disclosure classification
risk class
preview requirement
confirmation policy
rate/resource cost
result summarization policy
```

### AI-006 — No AI-only mutation

An AI mutation tool must correspond to an ordinary source/function or typed-record command. AI may replace native model source through an expected-digest transaction; it cannot write database rows, BREP, or hidden UI state directly.

### AI-007 — Bounded tool loops

Every interaction has limits for model calls, tool calls, elapsed time, tokens, evaluation resources, artifact bytes, and monetary cost. Limit exhaustion ends with a resumable report, never silent continuation.

## 6. Planning, preview, and approval

Non-trivial requests produce a structured plan containing goal, constraints, assumptions, referenced requirements, intended tools, risk, and completion checks.

### AI-008 — Deterministic policy owns confirmation

Whether a tool requires confirmation is decided by local policy from capability, command effects, project settings, and risk class—not by the model saying an action is safe.

### AI-009 — Preview equivalence

AI preview uses ephemeral transactions through the normal command/evaluation path. Accepting it submits the reviewed command batch against the reviewed revision; any changed payload/effects require a new preview or confirmation.

### AI-010 — Isolated alternatives

Long or aggressive tasks use workspace/revision branches. The user's active branch does not move until an explicit accept/merge command.

## 7. Engineering truth and claims

### AI-011 — Evidence-linked claims

Statements about dimensions, mass, stress, interference, manufacturability, or requirements link to typed query/evaluation results and revision IDs. Unverified inference is labeled as such.

### AI-012 — No certification implication

AI output and even successful simulation are not represented as certified or safe. Assumptions, solver/evaluator versions, stale status, and diagnostic warnings remain visible.

### AI-013 — Editable output

AI-created geometry is ordinary native build123d source with declared inputs and outputs. The UI exposes its source, parameters, environment, and topology capability even when no specialized graphical editor recognizes it.

### AI-014 — Harness does not define truth

App-server thread state, model prose, reasoning, and tool-selection history are operational records, not engineering state. Persistent effects exist only as ordinary committed Kearne commands; claims require revision-correct Kearne query evidence.

## 8. Privacy and providers

Modes are `No AI` and configured Codex app-server operation. Selected Codex authentication/provider configuration declares data residency, retention controls, model identity, context limits, and availability to the extent exposed by the pinned runtime.

Project data is not sent merely to detect whether AI could help. Cloud requests present a disclosure summary and follow project/organization policy. Prompts and responses are not persisted into engineering history by default; tool calls, resulting commands, model/provider identity, and safe provenance are.

## 9. Verification strategy

Core AI correctness does not depend on nondeterministic live-model prose tests.

- A scripted fake model generates valid, invalid, adversarial, duplicated, stale, and infinite tool sequences.
- A policy state machine verifies no sequence exceeds granted capabilities or bypasses confirmation.
- All AI mutation tools inherit command conformance suites.
- Generated project source and records contain prompt-injection text and secret-like values to test disclosure filters.
- Transcript replay verifies provider-independent orchestration and schema evolution.
- Live-provider evaluations measure task completion, tool selection, context efficiency, and recovery statistically; they are versioned benchmark reports, not the sole merge gate.
- Red-team suites cover exfiltration, approval spoofing, destructive command chaining, hidden prompt content, oversized queries, and compromised provider responses.

## 10. MVP scope

MVP provides:

- inspect selection, relevant native source, function graph, and bounded measurements;
- create and edit native build123d functions for the reference part;
- create or bind typed parameters and named outputs;
- preview, diff, confirm, reject, and repair source/function transactions;
- explain a structured diagnostic;
- local `No AI` mode and one pinned Codex app-server adapter.

Autonomous design, branch exploration, simulation optimization, network-enabled code, custom environments, and manufacturing validation are later gates.

## 11. Open decisions

- **AI-OPEN-001:** Supported Codex authentication/provider modes and retention contract.
- **AI-OPEN-002:** On-device context summarization and embedding/search strategy.
- **AI-OPEN-003:** Default approval matrix by command risk.
- **AI-OPEN-004:** Whether prompts/transcripts may be optionally project artifacts and their encryption/retention.
- **AI-OPEN-005:** Quantitative MVP AI evaluation set and release thresholds.

## 12. Definition of done

AI MVP is implemented when adversarial fake-model sequences cannot escape policy, native source edits use ordinary expected-digest transactions, stale proposals fail safely, disclosure is auditable, outputs remain editable, and provider-independent benchmarks meet approved thresholds.
