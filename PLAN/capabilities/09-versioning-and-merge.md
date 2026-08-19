# Versioning, Branching, Diff, and Merge

- **Status:** Proposed; revision foundation is MVP, product UI is post-MVP
- **Requirement prefix:** `VER`
- **Depends on:** [Commands and revisions](../foundations/02-commands-transactions-revisions.md), [persistence](../foundations/06-persistence-and-recovery.md), [document model](../foundations/01-document-model.md)
- **Unblocks:** releases, AI alternatives, collaboration

## 1. Purpose

Expose the immutable revision DAG as understandable versions and branches, merge source and typed engineering state, and show geometry as evaluated evidence rather than pretending to merge shapes.

## 2. References

```text
Workspace  local editing context with mutable head
Branch     named mutable reference to a revision
Version    named immutable reference to a revision
Tag        optional immutable metadata reference
Release    controlled immutable version plus lifecycle evidence
```

### VER-001 — Immutable revisions and versions

Revision content and version targets never move. Branch/workspace heads move through explicit, durable reference updates governed by optimistic concurrency.

### VER-002 — Reachability

Deleting a branch name does not immediately delete revisions or artifacts. Retention and garbage collection operate from versions, releases, active heads, undo windows, external references, and policy roots.

### VER-003 — Branch-safe IDs

Existing function, call, record, and occurrence IDs remain unchanged across branches. Independently created items use collision-resistant IDs. Merge never rewrites identity merely to fit one branch's path or collection order.

## 3. Semantic diff

Diff begins with stable identity, project paths, and schema-aware comparison:

```text
Source module created / moved / deleted / modified
Model function signature or implementation changed
Input binding or named output changed
Typed record created / deleted / modified / suppressed
Reference or parameter expression/value changed
Instance configuration/revision changed
Material/metadata/requirement changed
Evaluation consequence: geometry, mass, diagnostics (derived)
```

### VER-004 — Intent and consequence separation

Canonical diff reports source, contract, binding, and record changes independently from evaluated consequences. Geometry, mass, or simulation delta is labeled with evaluator fingerprints and may be pending or unavailable.

### VER-005 — Descriptor-driven diff

Function contracts and record schemas provide meaningful labels, dimensions, collection semantics, and redaction. Source uses function-aware and text diff. Raw blob or JSON diff is fallback developer detail.

## 4. Three-way merge

Merge uses base `B`, ours `O`, and theirs `T`:

1. compare content paths, function/call identity, and record fields;
2. accept changes made on only one side;
3. combine independent contract, binding, and record fields when schemas permit;
4. merge concurrent source through syntax-aware or text three-way merge;
5. retain base, ours, and theirs when source or typed values conflict;
6. validate the staged content tree and typed graph;
7. create one revision with parents `O` and `T` after resolution;
8. evaluate geometry and other consequences asynchronously.

### VER-006 — No last-writer-wins default

Conflicting engineering edits are never resolved solely by timestamp, actor order, or branch priority.

### VER-007 — Conflict values are durable proposals

Merge conflicts identify base/ours/theirs source or typed values, affected references, downstream consequences, and allowed resolution commands. An unresolved merge does not become an ordinary valid project revision.

### VER-008 — Delete/modify conflicts

Deletion on one side and modification or new reference on the other is a conflict unless ownership rules prove the changed item is wholly owned by an independently deleted parent and no external reference remains.

### VER-009 — Post-merge evaluation

A structurally valid source merge may still fail parsing or geometry evaluation. These are evaluation diagnostics, not silently resolved merge conflicts. The UI distinguishes source/record conflict resolution from downstream feasibility.

## 5. Collection semantics

Schema descriptors declare whether collections are:

- maps/sets keyed by stable element ID;
- user-significant sequences;
- presentation-only order;
- mathematically unordered inputs with explicit deterministic normalization.

Sequence merge uses stable element IDs and an explicit ordering algorithm. Function dependency order is not inferred from file order or UI sequence.

## 6. External references

Pinned external references target immutable versions/revisions. Floating references target branch-like locators but record the last resolved revision. Updating a floating reference is a command producing a new revision and diff.

### VER-010 — Released reference policy

Released content MUST NOT depend on unpinned mutable external references. Release validation reports and blocks them unless an organization policy explicitly permits an exception.

## 7. AI alternatives

AI agent workspaces are ordinary branches or private workspace heads with budgets and provenance. Candidate comparison uses semantic/evaluated diff. Accepting a candidate is a normal merge; the AI cannot replace the user's branch pointer directly without permission.

## 8. Verification strategy

Generate revision DAGs and schema-aware mutations. Verify algebraic and model properties where applicable:

- diff of a revision with itself is empty;
- applying a generated non-conflicting diff reconstructs the target content tree and records;
- swapping ours/theirs changes labels but not resolvability for symmetric independent edits;
- merge preserves both independent creations and disjoint field edits;
- conflicts are deterministic under entity storage reordering;
- revision ancestors remain immutable;
- save/reload/compaction preserves DAG and reference targets;
- unknown source types and plugin records survive and conflict conservatively;
- generated merge results pass project invariants before commit;
- automatic text merge never implies successful parse or geometry evaluation.

The suite runs on all registered entity schemas through descriptor-provided generators and merge semantics rather than a hand-authored test per field.

## 9. Open decisions

- **VER-OPEN-001:** Revision and semantic digest construction.
- **VER-OPEN-002:** Sequence merge algorithm and move representation.
- **VER-OPEN-003:** User-facing unresolved merge workspace persistence.
- **VER-OPEN-004:** Retention and pruning policy.
- **VER-OPEN-005:** Geometry difference algorithm and approximation levels.

## 10. Definition of done

Versioning/merge v1 is implemented when generated multi-branch source and record histories merge deterministically, registered schemas join the generic suite, immutable references survive persistence and compaction, and every automatic resolution explains what it combined without claiming geometric correctness.
