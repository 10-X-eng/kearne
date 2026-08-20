# ADR-0015: Command Audit Retention

- **Status:** Accepted
- **Date:** 2026-08-19
- **Related:** `CMD-003`, `CMD-012`, `CMD-OPEN-002`

## Context

History needs replay, idempotency, and audit evidence without retaining secrets or raw AI prompts that may occur in requests.

## Decision

Each revision retains normalized mutations, stable command keys, request ID and digest, transaction ID, actor, origin, permission-context ID, optional gesture ID, schema set, parents, and commit time. Normalized mutations contain the bytes needed for replay. Raw command requests are not stored in the project by default.

An organization may configure a separate encrypted audit sink with its own permission and retention policy. Its failure cannot weaken project commit atomicity silently.

## Consequences

Project history can verify request reuse and reconstruct state without becoming a transcript or secret store. A request body cannot be recovered from ordinary project history; diagnostics and descriptions must be generated from normalized facts.

## Alternatives rejected

- Retaining every raw request leaks unrelated prompt and credential data.
- Retaining only prose loses replay, stable diff, and idempotency evidence.
