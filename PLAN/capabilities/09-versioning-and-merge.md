# Versioning, Branching, Diff, and Merge

- **Status:** Proposed; revision foundation is MVP, product UI is post-MVP
- **Requirement prefix:** `VER`
- **Depends on:** [Commands and revisions](../foundations/02-commands-transactions-revisions.md), [persistence](../foundations/06-persistence-and-recovery.md), [document model](../foundations/01-document-model.md)
- **Unblocks:** releases, AI alternatives, collaboration

## 1. Purpose

Expose the immutable revision DAG as understandable versions and branches, compare engineering meaning rather than files, and merge independent changes without inventing geometry-level conflict resolution.

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

Existing entity IDs remain unchanged across branches. Independently created entities use collision-resistant IDs. Merge never rewrites identity merely to fit one branch's collection order.

## 3. Semantic diff

Diff begins with entity identity and schema-aware field comparison:

```text
Created / Deleted / Modified / Suppressed
Reference changed
Parameter expression/value changed
Feature/body binding changed
Instance configuration/revision changed
Material/metadata/requirement changed
Evaluation consequence: geometry, mass, diagnostics (derived)
```

### VER-004 — Intent and consequence separation

Canonical diff reports semantic mutations independently from evaluated consequences. Geometry delta, mass delta, or simulation delta is labeled with evaluator fingerprints and may be pending/unavailable.

### VER-005 — Descriptor-driven diff

Entity schemas provide meaningful field labels, dimensions, unordered/ordered collection semantics, and redaction. Generic byte or JSON diff is fallback developer detail, not the product result.

## 4. Three-way merge

Merge uses base `B`, ours `O`, and theirs `T`:

1. compare entity existence and schema-aware fields by stable ID;
2. accept changes made on only one side;
3. combine independently changed fields/collection members when the descriptor permits;
4. produce typed conflicts otherwise;
5. validate the staged merged snapshot structurally and semantically;
6. create one revision with parents `O` and `T` after resolution.

### VER-006 — No last-writer-wins default

Conflicting engineering edits are never resolved solely by timestamp, actor order, or branch priority.

### VER-007 — Conflict values are durable proposals

Merge conflicts identify base/ours/theirs semantic values, affected references, downstream consequences, and allowed resolution commands. An unresolved merge does not become an ordinary valid document revision.

### VER-008 — Delete/modify conflicts

Deletion on one side and modification/reference creation on the other is a conflict unless entity ownership rules prove the changed entity is wholly owned by an independently deleted parent and no external reference remains.

### VER-009 — Post-merge evaluation

A structurally valid merge may still produce geometry failures. These are evaluation diagnostics, not necessarily merge conflicts. The UI distinguishes semantic conflict resolution from downstream kernel feasibility.

## 5. Collection semantics

Schema descriptors declare whether collections are:

- maps/sets keyed by stable element ID;
- user-significant sequences;
- presentation-only order;
- mathematically unordered inputs with explicit deterministic normalization.

Sequence merge uses stable element IDs and an explicit ordering algorithm. Feature dependency order is not inferred from UI sequence.

## 6. External references

Pinned external references target immutable versions/revisions. Floating references target branch-like locators but record the last resolved revision. Updating a floating reference is a command producing a new revision and diff.

### VER-010 — Released reference policy

Released content MUST NOT depend on unpinned mutable external references. Release validation reports and blocks them unless an organization policy explicitly permits an exception.

## 7. AI alternatives

AI agent workspaces are ordinary branches or private workspace heads with budgets and provenance. Candidate comparison uses semantic/evaluated diff. Accepting a candidate is a normal merge; the AI cannot replace the user's branch pointer directly without permission.

## 8. Verification strategy

Generate revision DAGs and schema-aware mutations. Verify algebraic and model properties where applicable:

- diff of a revision with itself is empty;
- applying a generated non-conflicting diff reconstructs semantic target state;
- swapping ours/theirs changes labels but not resolvability for symmetric independent edits;
- merge preserves both independent creations and disjoint field edits;
- conflicts are deterministic under entity storage reordering;
- revision ancestors remain immutable;
- save/reload/compaction preserves DAG and reference targets;
- unknown plugin entities survive and conflict conservatively;
- generated merge results pass document invariants before commit.

The suite runs on all registered entity schemas through descriptor-provided generators and merge semantics rather than a hand-authored test per field.

## 9. Open decisions

- **VER-OPEN-001:** Revision and semantic digest construction.
- **VER-OPEN-002:** Sequence merge algorithm and move representation.
- **VER-OPEN-003:** User-facing unresolved merge workspace persistence.
- **VER-OPEN-004:** Retention and pruning policy.
- **VER-OPEN-005:** Geometry difference algorithm and approximation levels.

## 10. Definition of done

Versioning/merge v1 is implemented when generated multi-branch histories merge deterministically, schema plugins join the generic suite, immutable references survive persistence/compaction, and every automatic resolution can explain which independent semantic fields it combined.
