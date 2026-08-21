# Non-negotiable Kearne rules

Build every part of Kearne as compact, reusable, production software for humans: one canonical native build123d design and command model for UI, AI, scripts, plugins, history, files, and headless access; complete, coherent, fast workflows; engineering work off the UI thread; multithreaded CPU execution and GPU acceleration where useful; and visible results that are sane and beautiful.

Follow the user's instructions exactly and never claim that work is ready, working, complete, reviewable, or committable until the exact Release application has been exercised like a human through every affected real workflow, all controls and outputs have been checked, lossless full-screen captures have been critically inspected, every defect or omission has been fixed, and the answer to both “Does it work sanely?” and “Is it beautiful?” is unequivocally yes; tests are necessary but never sufficient, and launches, commits, pushes, prototypes, shortcuts, stubs, hidden gaps, and scope changes require the user's existing authorization.

# Kearne Agent Contract

Read [`PLAN/README.md`](PLAN/README.md), the owning plan, its dependencies, and applicable ADRs before changing behavior.

## Product boundary

- Kearne is a new mechanical CAD system, not a FreeCAD rewrite or compatibility clone.
- Prefer current supported technology when evidence shows a correctness, performance, security, or maintenance benefit. Novelty alone is not evidence.
- Native build123d functions are canonical part geometry. Typed engineering records own nongeometry product semantics. UI, Codex, Python, plugins, replay, and tests edit the same source/function graph and records through the Engineering API; never create a parallel feature model.

## Implementation

- Build production code under `apps/`, `modules/`, `api/`, `workers/`, `sdk/`, and `testkit/`. The root build MUST NOT include `prototype/`.
- Implement the complete desktop interaction system before engineering backends. UI data comes through typed replaceable ports; deterministic in-memory providers may supply development state but cannot duplicate domain rules.
- Use `prototype/` only when a material feasibility question cannot be answered safely while implementing production code. Product code MUST NOT include, link, package, or copy prototype code without a deliberate production rewrite and review.
- Deliver the smallest complete vertical slice through accepted boundaries.
- Name implemented requirement IDs and resolve material `OPEN` decisions through plans or ADRs.
- Keep engineering work off the UI thread and third-party live objects inside their owner.
- Generate adapters, schemas, metadata, and conformance enrollment from one descriptor when they express the same domain fact.

## Change gates

- Work locally until a coherent production work package passes its applicable correctness, architecture, test, observation, performance, security, and documentation gate.
- Do not commit as a checkpoint or after an isolated experiment. Commit only after the work-package gate passes. Push only after the requested repository audit.

## Verification

- Use `./tools/build.sh` for Release builds and acceptance runs. Do not infer
  Release readiness from ad hoc CMake commands or another build directory.
- Extend shared generators, reference models, contract suites, or scenario data before adding a one-off test.
- Use fixed regressions only for external compatibility, a minimized defect that existing generators cannot express, or a critical user workflow.
- Do not use sleeps, exact BREP bytes, localized text, widget coordinates, or broad screenshot goldens as primary oracles.
- A desktop change is incomplete until the agent harness launches Kearne, captures the entire visible Kearne session, returns the lossless image artifact, and correlates it with a semantic UI snapshot.
- Never claim a UI state from code inspection alone.

Leave the repository buildable and required assurance profiles passing.
