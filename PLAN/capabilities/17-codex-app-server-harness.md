# Codex App-Server Harness

- **Status:** Proposed; product choice accepted, integration spike required
- **Requirement prefix:** `HAR`
- **Depends on:** [AI system](06-ai-system.md), [Engineering API](../foundations/08-engineering-api.md), [processes and IPC](../foundations/07-processes-and-ipc.md), [security](../delivery/06-security-threat-model.md)
- **Unblocks:** embedded AI workflows and agent-driven development

## 1. Purpose

Use Codex app-server for AI threads, turns, streamed items, authentication, and approvals without giving it ownership of engineering state. Kearne remains functional in `No AI` mode.

The integration follows the installed version of the [official app-server protocol](https://learn.chatgpt.com/docs/app-server), not copied request types or inferred behavior.

## 2. Boundary

```text
Kearne AI controller
  <-> versioned app-server client
      <-> supervised Codex app-server
          -> capability-filtered Kearne Agent Bridge (local MCP)
              -> Engineering API commands/queries
              -> Desktop Observation API
```

### HAR-001 — App-server is orchestration, not authority

App-server owns conversational execution state. Kearne owns permissions, projects, revisions, commands, evaluations, artifacts, and audit provenance. An app-server thread ID or transcript MUST NOT become document identity or canonical history.

### HAR-002 — One engineering path

The Agent Bridge is a Kearne-owned local MCP server configured for app-server. It MUST derive command and query tools from Engineering API descriptors. It may add AI-facing descriptions, budgets, and confirmation metadata, but MUST NOT reimplement validation, normalization, evaluation, or mutation. Experimental client-executed dynamic tools are excluded from the production baseline.

### HAR-003 — Replaceable adapter

Only the app-server adapter knows its wire types. Domain, document, geometry, persistence, rendering, and QML libraries MUST NOT depend on Codex protocol types or authentication state.

## 3. Process and protocol

The production baseline launches a pinned Codex executable as a supervised local child and uses JSONL over standard input/output. Experimental WebSocket transport is excluded until an ADR accepts its stability and operational value.

### HAR-004 — Version and schema handshake

Developer, CI, and release builds pin one Codex version. CI generates JSON Schema from that executable, records its digest, and runs adapter conformance against it; language bindings are generated only for a selected host language. Startup rejects any version outside a reviewed allowlist with a structured diagnostic and leaves non-AI Kearne usable.

### HAR-005 — Explicit initialization

The client initializes once per process, identifies Kearne and its version, declares only used capabilities, then starts or resumes threads. Requests, responses, and notifications are correlated by native protocol IDs; unknown additive fields are preserved or ignored according to the generated schema contract.

### HAR-006 — Supervised lifecycle

The supervisor constructs the executable path, working directory, environment, configuration root, credentials access, sandbox, and network policy explicitly. Crash, hang, protocol corruption, authentication expiry, rate limits, and cancellation terminate the affected turn with a recoverable diagnostic; they cannot roll back or corrupt committed Kearne revisions.

## 4. Threads, turns, and revisions

A Kearne AI session maps to one app-server thread and one Kearne workspace/branch context. Each turn records its base revision, granted capabilities, disclosure budget, tool budget, and completion condition.

### HAR-007 — Revision anchoring

Every Kearne tool request carries the base revision and an idempotency key. A tool call against changed state is rejected or explicitly rebased by Kearne policy. Retried tool calls cannot duplicate a committed command.

### HAR-008 — Streamed state is operational

Turn and item events project into cancellable UI operation state. Partial reasoning, prose, progress, and approval items are not engineering evidence. Only typed Kearne query results and committed revisions support engineering claims.

### HAR-009 — Two permission boundaries

App-server approvals govern Codex runtime actions such as shell, files, and network. Kearne policy separately governs project disclosure and engineering tools. Approval at either boundary MUST NOT imply approval at the other.

## 5. Agent Bridge tools

The bridge exposes bounded capability sets:

```text
engineering.describe/query/preview/execute
operation.inspect/cancel
artifact.inspect
application.start/state/stop
ui.snapshot/await/capture/action
```

Command-specific metadata is generated from the registry; shared transport implementations execute it. Filesystem, Python, export, network, plugin, and future simulation access remain separate grants.

### HAR-010 — Images use protocol inputs

The host passes a captured image to a turn through the app-server image or local-image input supported by the pinned schema. The image path is brokered, short-lived, read-only to the agent process where enforceable, and subject to disclosure policy.

### HAR-011 — Bounded execution

Each session and turn has limits for model tokens, tool calls, elapsed time, retries, concurrent jobs, artifact bytes, screenshot rate, and cost. Exhaustion returns a resumable report with the current Kearne revision.

## 6. Verification

One protocol harness drives both a scripted fake app-server and the pinned executable. It covers initialization ordering, generated-schema conformance, notifications before responses, cancellation, duplicate/late events, malformed JSONL, stdout contamination, crash/restart, auth expiry, approval round trips, thread resume, local-image input, and Codex version skew.

Recorded protocol transcripts contain no credentials or proprietary project content. Semantic scenarios run through the Agent Bridge and compare resulting commands, queries, and revisions with CLI/Python/UI adapters.

### HAR-012 — Live models are not the correctness oracle

Merge gates use the fake app-server, deterministic tool sequences, and protocol contracts. Live Codex evaluations measure task completion and recovery statistically; a sampled response cannot replace deterministic policy or domain tests.

## 7. Open decisions

- **HAR-OPEN-001:** Supported Codex release cadence and compatibility window.
- **HAR-OPEN-002:** Authentication UX and credential-store ownership.
- **HAR-OPEN-003:** Agent Bridge topology: supervised stdio MCP child connected to the coordinator, or authenticated loopback MCP endpoint hosted by Kearne.
- **HAR-OPEN-004:** Thread retention, transcript privacy, and project-to-thread mapping policy.

## 8. Definition of done

The harness is implemented when the pinned and fake app-servers pass one protocol suite; a Codex turn can inspect, preview, modify, and verify the reference part only through Kearne tools; stale and repeated calls fail safely; approvals remain distinct; a crash leaves the project valid; and `No AI` workflows still pass.
