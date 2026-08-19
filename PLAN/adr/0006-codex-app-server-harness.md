# ADR-0006: Codex App-Server Is the AI Harness

- **Status:** Superseded
- **Date:** 2026-08-19
- **Superseded by:** [ADR-0008](0008-codex-app-server-compatibility.md)
- **Related:** [Codex harness](../capabilities/17-codex-app-server-harness.md), [AI system](../capabilities/06-ai-system.md)

## Context

Kearne needs a rich local AI integration with persistent threads, streamed turns and items, approvals, authentication, tool execution, and image inputs. Implementing that lifecycle directly would duplicate a fast-changing AI client surface.

## Decision

Kearne will integrate Codex through app-server. The production baseline supervises a version-pinned local process and uses its stable local protocol transport. Kearne wraps the protocol behind an adapter, generates conformance schemas from the pinned executable, and retains sole authority over engineering state, commands, permissions, and evidence.

## Consequences

Kearne gains the Codex interaction lifecycle without embedding it into CAD libraries. The release must pin, package or locate, authenticate, supervise, test, and update a compatible Codex executable. Protocol change cannot leak into document or Engineering API schemas. `No AI` remains a complete operating mode.

## Alternatives rejected

- Direct model-provider SDK integration would make Kearne own threads, approvals, streamed tool state, and provider differences.
- Using app-server as canonical project history would couple durable CAD state to conversational runtime state.
- Depending on experimental WebSocket transport would add stability risk without an MVP need.

## Evidence

The [official app-server documentation](https://learn.chatgpt.com/docs/app-server) defines the required lifecycle, JSONL stdio transport, generated schemas, approvals, and local-image turn input. SPIKE-010 must verify the installed-version contract, packaging, recovery, and Agent Bridge path before implementation acceptance.
