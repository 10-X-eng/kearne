# Engineering API and Schemas

- **Status:** Proposed; schema-tooling spike required
- **Requirement prefix:** `API`
- **Depends on:** [Document model](01-document-model.md), [commands and revisions](02-commands-transactions-revisions.md), [units](05-units-expressions-numerics.md)
- **Unblocks:** QML, CLI, Python, AI, plugins, workers, tests

## 1. Purpose

Expose one stable semantic command/query surface through multiple adapters without duplicating engineering rules or hand-maintaining inconsistent schemas, documentation, AI tools, and bindings.

## 2. API families

```text
Command API      submit/preview/validate transactions
Query API        inspect immutable document and derived results
Operation API    observe/cancel asynchronous work
Event API        subscribe to revision, projection, job, and diagnostic changes
Artifact API     bounded read/write leases for immutable bulk data
Registry API     discover command, feature, unit, schema, and capability descriptors
Admin API        migrations, repair, diagnostics; not exposed to normal automation
```

### API-001 — Revision-explicit reads

Every engineering query accepts a revision or snapshot token and returns the observed revision. “Current document” is adapter convenience resolved before entering the domain API.

### API-002 — Command-only writes

There are no general entity setters, mutable document objects, or arbitrary JSON patch endpoints in public APIs. Persistent writes are typed commands and transactions.

### API-003 — Bounded results

Collection queries support limits, stable continuation tokens, filters, and declared maximum response sizes. Geometry inspection returns semantic summaries or artifact handles, not unbounded raw triangulation by default.

## 3. Schema source and generation

The proposed approach is:

- a versioned IDL for wire envelopes, core values, commands, mutations, events, and worker messages;
- generated C++ and Python wire types;
- a descriptor compiler that emits JSON Schema/OpenAPI-like metadata for AI and tooling;
- handwritten typed domain conversion/validation at the boundary;
- QML-facing models generated or adapted from query projections, never wire records used as mutable UI state.

### API-004 — One fact, one source

Command name, schema version, field types, dimensions, optionality, enum values, permission class, stability, and documentation keys MUST each have one authoritative declaration. Derived bindings and tool schemas are generated and checked for drift.

### API-005 — Domain types remain strong

Schema generation MUST NOT force the semantic core into stringly typed maps. Generated wire types are boundary values converted to strong domain types before validation and execution.

### API-006 — No reflection-only business logic

Descriptors support discovery and generic UI/tooling, but domain behavior remains explicit typed code. A general property bag must not become the native feature implementation model merely to reduce LOC.

## 4. Command descriptor

Every command registers:

```text
stable type and schema version
typed request schema
required permission/capability
normalizer/validator
declared read/write effect calculator
preview support
human description formatter
result schema
compatibility/migration handlers
```

The registry powers command palette discovery, Python metadata, AI tool exposure, audit descriptions, contract tests, and documentation.

### API-007 — Capability-filtered discovery

Adapters see only commands permitted by their actor and runtime capability context. Hiding a command is not the security boundary; submission revalidates permissions.

## 5. Query and event semantics

### API-008 — Pure snapshot queries

Document queries are free of observable mutation and do not trigger unbounded evaluation implicitly. A query requiring missing derived data returns an operation handle or `NotEvaluated`, according to its contract.

### API-009 — Event resynchronization

Event streams are ordered per project coordinator and contain revision/generation sequence. Subscribers detecting a gap discard incremental assumptions and request a fresh projection; events are notifications, not canonical state.

### API-010 — Backpressure

Slow subscribers receive coalesced invalidations or disconnect with a resumable cursor. They cannot grow an unbounded coordinator queue.

## 6. Compatibility

### API-011 — Stable identifiers

Published command, field, enum, diagnostic, and capability identifiers are never repurposed. Removed values remain reserved.

### API-012 — Additive minor evolution

Within an API major version, readers tolerate unknown fields and writers do not require newly added optional fields. Semantic default changes require a new command/schema version even if wire compatibility appears additive.

### API-013 — Explicit negotiation

Out-of-process clients negotiate API versions and capabilities. The server reports unsupported commands and migrations structurally, never through parsing human error strings.

### API-014 — Python/API deprecation

Public SDK deprecations remain functional for a documented compatibility window and emit machine-readable warnings. Internal C++ APIs may evolve faster but obey library boundaries.

## 7. Adapter rules

### QML

QML uses immutable/read-only projections plus controller commands. Domain IDs and quantities have registered value wrappers. UI validation improves feedback but never replaces engine validation.

### Python

The SDK provides ergonomic typed wrappers over the same schemas. Python object convenience methods submit commands; they are not alternate document objects.

### AI

AI tools are filtered projections of command/query descriptors with stricter bounds, permissions, descriptions, and confirmation policies. AI never receives the admin API.

### CLI and replay

CLI commands and scenario logs serialize ordinary envelopes. Replay supplies controlled IDs/time and validates expected semantic results.

## 8. Security and privacy

- Permission checks occur after decoding and before expensive validation/evaluation.
- Query descriptors classify sensitive fields and maximum disclosure.
- Actor identity and capability context are coordinator-issued, not caller-asserted strings.
- Unknown or oversized messages are rejected before domain allocation.
- AI-facing schemas exclude filesystem paths, secrets, and arbitrary code unless a separately approved capability requires them.

## 9. Verification strategy

Generated conformance tests for every registered command/query verify:

- wire and JSON schema round-trip;
- unknown-field and version behavior;
- boundary-to-domain conversion;
- invalid enum, ID, dimension, count, depth, and byte limits;
- permission denial;
- deterministic descriptor output;
- Python/AI/CLI surface discovery parity;
- stable error mapping;
- pagination and subscription gap recovery.

The same semantic scenario corpus runs through in-process C++, local IPC, Python, CLI/replay, and fake AI tool adapters. It compares semantic outcomes, not adapter-specific formatting.

## 10. Open decisions

- **API-OPEN-001:** Confirm Protobuf or select another IDL after measuring schema evolution, C++/Python generation, JSON Schema quality, binary size, and fuzzability.
- **API-OPEN-002:** In-process API exposure: generated wire calls versus direct typed facade backed by the same descriptors.
- **API-OPEN-003:** Public remote API timing and authentication; not required for MVP.
- **API-OPEN-004:** Stable Python typing and documentation generation toolchain.

## 11. Definition of done

The API foundation is implemented when one sample command/query is consumed by QML, CLI, Python, AI schema, replay, and IPC without duplicated validation; generated compatibility/fuzz suites pass; and architecture checks prevent direct adapter mutation.
