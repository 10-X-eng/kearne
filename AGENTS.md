# Kearne Agent Contract

Read [`PLAN/README.md`](PLAN/README.md), the owning plan, its dependencies, and applicable ADRs before changing behavior.

## Product boundary

- Kearne is a new mechanical CAD system, not a FreeCAD rewrite or compatibility clone.
- Prefer current supported technology when evidence shows a correctness, performance, security, or maintenance benefit. Novelty alone is not evidence.
- The semantic document and Engineering API are canonical. UI, Codex, Python, plugins, replay, and tests do not create alternate mutation paths.

## Implementation

- Deliver the smallest complete vertical slice through accepted boundaries.
- Name implemented requirement IDs and resolve material `OPEN` decisions through plans or ADRs.
- Keep engineering work off the UI thread and third-party live objects inside their owner.
- Generate adapters, schemas, metadata, and conformance enrollment from one descriptor when they express the same domain fact.

## Verification

- Extend shared generators, reference models, contract suites, or scenario data before adding a one-off test.
- Use fixed regressions only for external compatibility, a minimized defect that existing generators cannot express, or a critical user workflow.
- Do not use sleeps, exact BREP bytes, localized text, widget coordinates, or broad screenshot goldens as primary oracles.
- A desktop change is incomplete until the agent harness launches Kearne, captures the entire visible Kearne session, returns the lossless image artifact, and correlates it with a semantic UI snapshot.
- Never claim a UI state from code inspection alone.

Leave the repository buildable and required assurance profiles passing.
