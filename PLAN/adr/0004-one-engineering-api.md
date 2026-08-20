# ADR-0004: One Engineering API for All Actors

- **Status:** Accepted
- **Date:** 2026-08-19
- **Related:** [Engineering API](../foundations/08-engineering-api.md), `PLAN-001`, `PLAN-002`

## Context

Separate GUI, scripting, plugin, and AI implementations would duplicate validation and produce different CAD behavior.

## Decision

GUI, CLI, Python, plugins, AI, replay, and tests submit source/function or typed engineering commands and use revision-explicit queries. Shared schema declarations generate boundary metadata and bindings where validated by TECH-001; strong domain validation remains handwritten once.

## Consequences

Adapters translate presentation and transport only. Public writes cannot patch storage records directly. Command, function-contract, and operation-tooling registration includes conformance metadata. Schema tooling becomes critical build infrastructure.

## Evidence required

[`TECH-001`](../../prototype/001-schema-binding-pipeline) runs one command/query scenario through C++, Qt, local IPC, generated Python, CLI JSON, and generated AI metadata with one validator. [ADR-0011](0011-protobuf-engineering-api.md) fixes the schema boundary.
