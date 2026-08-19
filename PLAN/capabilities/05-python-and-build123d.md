# Python SDK and build123d

- **Status:** Proposed; isolation and compatibility gates required
- **Requirement prefix:** `PY`
- **Depends on:** [document model](../foundations/01-document-model.md), [Engineering API](../foundations/08-engineering-api.md), [processes](../foundations/07-processes-and-ipc.md)
- **Unblocks:** parametric geometry, AI modeling, automation, reusable features

## 1. Purpose

Make native build123d source the editable definition of part geometry without restricting Python to a GUI-shaped subset or allowing code to control the application runtime.

## 2. Canonical function model

### PY-001 — Function boundary

A model function is identified by project module and qualified name. Its durable contract declares:

```text
ModelFunction
  id
  module source artifact and digest
  entry point
  typed inputs and bindings
  named output slots
  function dependencies
  environment fingerprint
  capability profile
  topology publication mode
```

The contract may live in a Kearne manifest or optional decorator. A decorator is not required for otherwise valid native build123d source.

### PY-002 — Native source freedom

Function bodies may use ordinary Python, helpers, classes, loops, conditionals, comprehensions, and any mixture of build123d algebra and builder modes allowed by the pinned environment. Kearne MUST NOT require translation to an internal feature language.

### PY-003 — Explicit boundary

Inputs are typed values or immutable named outputs. Publication requires returned values matching declared output slots. Module globals, printed values, and interpreter-resident objects are not outputs.

### PY-004 — One geometry authority

GUI tools, AI, direct source editing, plugins, replay, and headless clients mutate the same source/function graph. Kearne MUST NOT persist a parallel native-feature graph for source-defined geometry.

## 3. Graph granularity

Kearne does not prescribe function size. GUI tools normally create small composable functions for sketches and operations. AI and users may define a whole component in one function or factor reusable helpers into modules.

Only declared model functions and their bindings are dependency nodes. Internal Python calls remain implementation details covered by their source and environment digests.

### PY-005 — Dependency completeness

Evaluation keys include the transitive digests of project modules, declared inputs, named upstream outputs, environment lock, build123d/OCP/OCCT versions, numerical profile, and capability policy. Undeclared dynamic inputs make evaluation non-reproducible and unavailable for released revisions.

## 4. GUI and source coexistence

### PY-006 — Recognition is optional

Kearne may recognize generated or familiar source structures and offer sketch, extrude, fillet, pattern, and other specialized editors. Recognition is derived metadata with a confidence and source digest. It is never canonical.

### PY-007 — Honest degradation

If a source edit invalidates recognition, the function remains valid and editable through its source, signature, parameters, inputs, and outputs. Kearne MUST NOT silently rewrite it, replace it with BREP, or claim a specialized editor can preserve semantics.

### PY-008 — Structural editing

GUI edits apply source transformations against an expected source digest and return a previewable replacement. The transformation preserves unrelated source text or refuses with a diagnostic. It MUST NOT fall back to regular-expression rewriting.

Source inspection uses a parser or concrete syntax tree and never imports or executes the module.

## 5. Evaluation

Model functions execute asynchronously outside the UI process in a pinned worker. Evaluation has read-only declared inputs and private scratch space. It cannot submit project mutations, move a branch, invoke UI behavior, or read mutable workspace state.

### PY-009 — Artifact validation

Returned build123d/OCP objects cross the worker boundary as immutable versioned artifacts. Kearne validates type, size, shape health, units, output count, and protocol compatibility before publication.

### PY-010 — Failure retention

Syntax, import, timeout, cancellation, and geometry failures produce diagnostics for the source revision. They do not delete source, mutate the last successful artifact, or rewrite history.

### PY-011 — Topology capability

A function may publish explicit topology labels and ancestry, body-level identity only, or dumb topology. Kearne MUST NOT promise stable subshape references beyond the function's declared and verified capability.

## 6. Runtime policy

- Python, build123d, OCP, OCCT compatibility, Kearne helpers, and packages are lockfile-addressed.
- Filesystem and network access start denied; approved capabilities participate in provenance and release policy.
- CPU time, wall time, memory, process count, logs, and output sizes are bounded.
- Cancellation escalates to worker termination.
- `pip install` during evaluation is prohibited.
- Untrusted code uses a fresh or proven-sanitized worker identity.

### PY-012 — Honest isolation claim

Until platform escape testing meets the threat model, Kearne describes execution as crash-contained and capability-limited, not secure sandboxing.

## 7. Automation API

Python automation may query projects and submit source, binding, assembly, study, drawing, and other typed commands. Proxies carry the observed revision and cannot mutate a stale head implicitly.

### PY-013 — Source replacement command

Direct code editing submits the complete replacement artifact, expected prior digest, function-contract changes, and base revision. Parsing, permission, size, and contract checks precede commit. Evaluation follows commit.

### PY-014 — No evaluation side effects

A model function cannot mutate project state during evaluation. Automation that intentionally changes a project runs in a separate capability role through ordinary transactions.

## 8. Git-like diff and merge

Source modules are content-addressed revision-tree entries. Diff and merge operate in this order:

1. project path and function identity;
2. typed function contracts and bindings;
3. syntax-aware or text three-way source merge;
4. structural validation;
5. asynchronous geometry comparison.

Automatic source merge never implies geometric correctness. Conflicts retain base, ours, and theirs. Geometry and mass changes are derived review evidence.

## 9. Verification

- Generate functions across parameter domains and compare evaluation keys, output contracts, determinism, and cache reuse.
- Generate source edits and verify parse-without-execution, exact source preservation, stale-digest rejection, and recognized-editor degradation.
- Generate revision DAGs with module moves, concurrent function edits, binding changes, syntax failures, and merge conflicts.
- Run the same function corpus through algebra, builder, and mixed-mode implementations where equivalent properties exist.
- Fault-inject worker crashes, infinite loops, memory growth, log floods, malformed artifacts, forbidden capabilities, and incompatible OCP handshakes.
- Scale generator sizes in CI profiles instead of adding fixed tests per function or feature type.

## 10. Open decisions

- **PY-OPEN-001:** Manifest format and optional decorator API.
- **PY-OPEN-002:** Concrete-syntax tooling and source transformation protocol.
- **PY-OPEN-003:** Environment distribution and build123d/OCP/OCCT version matrix.
- **PY-OPEN-004:** Explicit topology-label and ancestry helper API.
- **PY-OPEN-005:** Platform isolation mechanisms and product wording.

## 11. Definition of done

The boundary is implemented when unrestricted native build123d functions evaluate through declared contracts; GUI-generated and AI-edited source share one revision history; unrecognized source remains editable without loss; generated merge, determinism, and fault suites pass at multiple scales; and worker failure cannot corrupt project state.
