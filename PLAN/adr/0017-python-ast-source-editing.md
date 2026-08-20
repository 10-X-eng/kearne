# ADR-0017: Python AST and Token Spans for Recognized Source Editing

- **Status:** Accepted
- **Date:** 2026-08-19
- **Related:** `PY-006`–`PY-008`, `SKH-001`–`SKH-003`, `TECH-012`

## Context

Generated Sketch source needs parse-without-execution, exact preservation outside edited calls, stale-digest protection, and edit-plus-reparse p95 below 16 ms for 100 entities. The parser is a coordinator dependency and must remain safe at larger scales.

## Decision

Use Python's standard `ast` to recognize the exact generated helper shape. Use AST UTF-8 byte spans for call replacement and `tokenize` only within the punctuation gap needed by append/delete. Apply byte splices against an expected BLAKE3 source digest, then parse and validate the result again.

Unrecognized valid Python remains untouched and usable through source editing and evaluation. Recognition never imports or executes the module.

## Consequences

- No third-party parser or duplicate Python grammar is required.
- Comments, formatting, and unrelated statements remain byte-identical.
- Recognition follows the pinned worker Python grammar; a Python-version update reruns the source corpus and performance profile.
- Edits reparse the function rather than maintaining a hidden syntax or feature graph.
- Large-function latency remains measurable through a parameterized benchmark; incremental parsing may be reconsidered only with a crash-safe candidate.

## Alternatives rejected

- LibCST 1.9.0 preserved formatting but measured about 100 ms median and 262 ms p95 for recognition of the 100-entity fixture.
- Tree-sitter 0.26.0 with tree-sitter-python 0.25.0 met the 100-entity latency target but reproducibly segfaulted between 500 and 1,000 entries. A native parser crash is unacceptable in the source path.
- Regular-expression rewriting cannot prove syntactic ownership or preserve arbitrary Python safely.

## Evidence

[`source_edit.py`](../../sdk/python/benchmarks/source_edit.py) validates output before measuring. On CPython 3.12.3/Linux 6.17 and a 16-thread Intel Xeon Silver 4112, 300 warm samples of the typed-unit 9,463-byte/100-entity fixture measured recognition p50/p95 5.27/6.79 ms and append-plus-reparse 10.86/13.19 ms. The 94,064-byte/1,000-entity fixture completed without a crash at 62.42/79.18 ms and 127.13/151.22 ms. Generated tests cover compact/multiline tuple/list forms, LF/CRLF, Unicode spans, class-qualified functions, comments, empty sections, stale edits, identity, and missing references.
