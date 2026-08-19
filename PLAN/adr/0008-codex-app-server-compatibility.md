# ADR-0008: Kearne Owns Codex App-Server Compatibility

- **Status:** Accepted
- **Date:** 2026-08-19
- **Supersedes:** [ADR-0006](0006-codex-app-server-harness.md)
- **Related:** [Codex harness](../capabilities/17-codex-app-server-harness.md), [TECH-010](../../prototype/010-codex-app-server/README.md)

## Context

Kearne requires Codex app-server as its AI harness. Current official documentation describes app-server as the deep-integration surface for rich clients while marking the app-server command experimental and unsupported for production workloads. Its generated schemas are binary-version-specific.

## Decision

Kearne will continue with app-server behind a replaceable adapter. Each developer, CI, and release profile pins one exact Codex build, generates its protocol schema, and must pass Kearne's conformance suite. Kearne owns compatibility, packaging, supervision, rollback, and user-visible unsupported-version behavior. App-server remains operational state; it cannot own engineering data or permissions. `No AI` remains functional when it is absent or rejected.

The baseline uses local JSONL stdio. Experimental app-server fields, dynamic tools, and WebSocket transport are disabled. The Kearne Agent Bridge uses the stable MCP surface unless a later ADR replaces it with evidence.

## Consequences

App-server cannot be described as a supported production dependency until OpenAI changes its support status or Kearne accepts and staffs the maintenance burden. Updating Codex is a reviewed dependency migration, not an automatic user-environment discovery. Codex 0.146.1 aggregate schema bytes are order-nondeterministic, so compatibility compares canonical JSON semantics. AI failure cannot block project open, edit, save, export, or recovery.

## Alternatives rejected

- Ignoring the documented support status would turn upstream change into an unowned release risk.
- Direct provider APIs would violate the selected harness and require Kearne to rebuild the rich-client lifecycle.
- Experimental dynamic tools remove a bridge process but do not provide a stable production boundary.

## Evidence

The [official app-server documentation](https://learn.chatgpt.com/docs/app-server) defines the support status, JSONL stdio protocol, initialization, binary-specific schema generation, stable/experimental capability split, MCP surface, and local-image input. TECH-010 records behavior for each proposed pinned build.
