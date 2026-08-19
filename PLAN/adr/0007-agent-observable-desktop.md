# ADR-0007: Desktop Work Must Be Agent-Observable

- **Status:** Accepted
- **Date:** 2026-08-19
- **Related:** [Agent-observable desktop](../capabilities/18-agent-observable-desktop.md), [Qt/QML shell](../capabilities/15-qt-qml-application-shell.md)

## Context

An implementation agent cannot verify a desktop it cannot launch, inspect, and see. Source review and headless controller tests miss layout, clipping, z-order, render, DPI, focus, and platform integration failures. Screenshot-only automation is also brittle and blind to hidden semantics.

## Decision

Kearne will ship a capability-gated observation plane that provides deterministic application lifecycle control, semantic UI snapshots, event-based readiness, semantic actions, and lossless capture of every visible Kearne-owned surface. Every desktop work package requires returned image and semantic evidence. Full OS-display capture is optional where platform privacy policy permits.

## Consequences

UI infrastructure precedes feature breadth. Controls need stable semantic IDs and accessibility state. The capture implementation must compose Kearne windows and transient surfaces across Qt and platform boundaries. Captures are sensitive temporary artifacts. OS-owned secure surfaces cannot be guaranteed and must be reported honestly.

## Alternatives rejected

- Manual screenshots cannot support continuous agent work or reproducible automation.
- Broad pixel-golden suites scale poorly across platforms and do not prove behavior.
- Private QML hooks create a second interaction path and can mask production defects.

## Evidence

SPIKE-011 must prove complete Kearne-session capture and semantic correlation on the selected Windows and Linux display stacks before desktop feature breadth.
