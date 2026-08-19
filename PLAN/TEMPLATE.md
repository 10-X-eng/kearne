# `<Subsystem>` Plan

- **Status:** Draft | Proposed | Accepted | Implemented | Superseded
- **Requirement prefix:** `<PREFIX>`
- **Depends on:** links to owning plans and accepted ADRs
- **Unblocks:** links to dependent plans

## 1. Purpose

State the user or architectural outcome. Avoid implementation detail here.

## 2. Scope

List capabilities included in this plan and the release gate to which they apply.

## 3. Non-goals

List adjacent behavior this subsystem deliberately does not own.

## 4. Vocabulary and ownership

Define domain terms, which subsystem owns each record, and whether records are canonical state, derived indices, caches, or external artifacts.

## 5. Invariants

Use stable requirements:

### ABC-001 — Short name

State one externally observable or architecture-enforced rule using MUST, SHOULD, or MAY.

## 6. Data model and contracts

Describe typed records, identifiers, schemas, public functions, events, and compatibility rules. Examples are illustrative unless marked as normative.

## 7. State transitions and data flow

Define state machines, allowed transitions, ordering, idempotency, and relevant sequence diagrams.

## 8. Concurrency and cancellation

State process/thread ownership, synchronization boundaries, stale-result behavior, cancellation guarantees, and operations that cannot be interrupted safely.

## 9. Failure and diagnostics

Define expected failures, stable error categories, recovery, user-visible diagnostics, and retained last-known-good state.

## 10. Persistence and migration

Define what is canonical, what is cached, schema versions, compatibility windows, migration, and behavior when dependencies are unavailable.

## 11. Security and permissions

Identify trust boundaries, required capabilities, validation, resource limits, and sensitive data.

## 12. Performance budgets

Specify a benchmark fixture, hardware profile, percentile, warm/cold state, data size, and measurement boundary. A number without these dimensions is not a performance requirement.

## 13. Verification strategy

Prefer reusable verification mechanisms:

- conformance suite;
- generated properties;
- model-based state machine;
- metamorphic properties;
- fuzz target;
- deterministic replay scenario;
- fault injection;
- bounded curated regression.

## 14. Acceptance scenarios

Each scenario states setup, action, observable result, and requirement IDs. It must be runnable through the headless engineering API; UI tests verify adapter behavior separately.

## 15. Implementation stages

Define thin vertical stages, each leaving the repository buildable and testable. Do not make phases that create a parallel temporary architecture.

## 16. Open decisions

Each unresolved material choice includes options, evidence required, deadline/milestone, and responsible ADR number. Never bury an unresolved choice in prose.

## 17. Definition of done

List objective evidence needed to move the plan to `Implemented`, including supported-platform results, performance measurements, migrations, documentation, and conformance results.
