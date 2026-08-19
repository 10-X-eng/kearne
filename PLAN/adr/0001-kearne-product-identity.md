# ADR-0001: Kearne Product Identity

- **Status:** Accepted
- **Date:** 2026-08-19
- **Related:** [Plan index](../README.md)

## Context

The architecture baseline uses placeholder names such as `cadx`. The product owner selected `kearne` as the product name.

## Decision

Use **Kearne** for the product, `kearne` for executable/CLI/C++ namespace, `KEARNE_` for environment variables, and `.kearne` as the proposed project extension pending its physical-container decision.

## Consequences

New code, schemas, packages, and documentation do not introduce `cadx`. Renaming the project extension remains possible before the public format freeze.
