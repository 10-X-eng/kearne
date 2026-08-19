# ADR-0004: One Engineering API for All Actors

- **Status:** Proposed
- **Date:** 2026-08-19
- **Related:** [Engineering API](../foundations/08-engineering-api.md), `PLAN-001`, `PLAN-002`

## Context

Separate GUI, scripting, plugin, and AI implementations would duplicate validation and produce different CAD behavior.

## Decision

GUI, CLI, Python, plugins, AI, replay, and tests submit source/function or typed engineering commands and use revision-explicit queries. Shared schema declarations generate boundary metadata and bindings where validated by TECH-001; strong domain validation remains handwritten once.

## Consequences

Adapters translate presentation and transport only. Public writes cannot patch storage records directly. Command, function-contract, and operation-tooling registration includes conformance metadata. Schema tooling becomes critical build infrastructure.

## Evidence required

One command/query scenario must traverse every MVP adapter with equivalent semantic results and no duplicate validator before acceptance.
