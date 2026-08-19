# AI System

- **Status:** Proposed
- **Requirement prefix:** `AI`
- **Depends on:** [Engineering API](../foundations/08-engineering-api.md), [commands and revisions](../foundations/02-commands-transactions-revisions.md), [security](../delivery/06-security-threat-model.md)
- **Unblocks:** AI-native workflows and later optimization

## 1. Purpose

Make AI a permissioned, observable consumer of Kearne's engineering API. Models propose queries and commands; deterministic domain code validates and applies them. Model output is never canonical merely because it is well-formed.

## 2. Components

```text
AI interaction controller
  -> provider adapter (local or approved cloud)
  -> context/query budgeter
  -> capability-filtered tool registry
  -> plan/proposal workspace
  -> deterministic policy and approval engine
  -> ordinary Engineering API
```

No model SDK or prompt framework is linked into document, command, geometry, or persistence libraries.

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

Every tool call is schema-, permission-, revision-, reference-, and domain-validated by the same Engineering API used by other actors. Prompt instructions cannot bypass policy.

### AI-003 — Revision anchoring

AI context, queries, measurements, plans, and proposed commands name their source revision. If the workspace changes, stale proposals are re-inspected, explicitly rebased where safe, or rejected.

## 4. Context model

AI receives structured, bounded views:

- current selection as semantic references and engineering summaries;
- feature/dependency graph slices;
- parameter values and dimensions;
- diagnostics and model health;
- mass/bounds/measurements from revision-correct evaluators;
- assembly/simulation/drawing data only when permissioned;
- user-provided request and explicitly attached documents.

### AI-004 — Query before bulk disclosure

The controller favors typed queries over full document serialization, screenshots, raw BREP, or full meshes. Each query has item/byte/depth limits and sensitivity classification.

### AI-005 — Untrusted embedded content

Names, comments, imported metadata, plugin text, scripts, and document content are untrusted data, not controller instructions. Provider prompts delimit them and the policy engine ignores model attempts to alter permissions.

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

An AI mutation tool must correspond to an ordinary typed command or transaction composition. AI cannot write entity payloads, database rows, BREP, or hidden UI state directly.

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

Native AI-created geometry is ordinary editable features. Procedural output is clearly labeled with source/environment/topology limitations.

## 8. Privacy and providers

Modes are `No AI`, `Local Only`, and policy-approved cloud providers. Each provider adapter declares data residency/configuration, retention controls, model identity, supported tool protocol, context limits, and availability.

Project data is not sent merely to detect whether AI could help. Cloud requests present a disclosure summary and follow project/organization policy. Prompts and responses are not persisted into engineering history by default; tool calls, resulting commands, model/provider identity, and safe provenance are.

## 9. Verification strategy

Core AI correctness does not depend on nondeterministic live-model prose tests.

- A scripted fake model generates valid, invalid, adversarial, duplicated, stale, and infinite tool sequences.
- A policy state machine verifies no sequence exceeds granted capabilities or bypasses confirmation.
- All AI mutation tools inherit command conformance suites.
- Generated document content contains prompt-injection text and secret-like values to test disclosure filters.
- Transcript replay verifies provider-independent orchestration and schema evolution.
- Live-provider evaluations measure task completion, tool selection, context efficiency, and recovery statistically; they are versioned benchmark reports, not the sole merge gate.
- Red-team suites cover exfiltration, approval spoofing, destructive command chaining, hidden prompt content, oversized queries, and compromised provider responses.

## 10. MVP scope

MVP provides:

- inspect document/selection and bounded measure queries;
- create sketch primitives/constraints;
- create extrude, hole, pattern, fillet;
- set parameter;
- transaction preview, confirm, reject;
- explain a structured diagnostic;
- local `No AI` mode and one provider adapter selected later.

Autonomous design, branch exploration, simulation optimization, arbitrary Python generation, web access, and manufacturing validation are later gates.

## 11. Open decisions

- **AI-OPEN-001:** Initial local/cloud provider support and retention contract.
- **AI-OPEN-002:** On-device context summarization and embedding/search strategy.
- **AI-OPEN-003:** Default approval matrix by command risk.
- **AI-OPEN-004:** Whether prompts/transcripts may be optionally project artifacts and their encryption/retention.
- **AI-OPEN-005:** Quantitative MVP AI evaluation set and release thresholds.

## 12. Definition of done

AI MVP is implemented when adversarial fake-model sequences cannot escape policy, all mutation tools use ordinary commands, stale proposals fail safely, disclosure is auditable, outputs remain editable, and provider-independent benchmark reports meet approved task thresholds.
