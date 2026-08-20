# Engineering Simulation

- **Status:** Proposed; post-assembly
- **Requirement prefix:** `SIM`
- **Depends on:** [Units](../foundations/05-units-expressions-numerics.md), [persistent topology](../foundations/04-persistent-topology.md), [processes](../foundations/07-processes-and-ipc.md), [assemblies](08-assemblies.md)
- **Unblocks:** validation and optimization loops

## 1. Purpose

Represent finite element analysis (FEA) and later simulation setup as revisioned engineering intent, execute meshing/solving through replaceable backends, and preserve enough evidence to determine exactly what was solved.

Kearne simulation is decision support, not automatic certification.

## 2. Canonical study model

```text
SimulationStudy
  analysis type and schema
  geometry set by semantic refs
  idealizations
  material assignments
  contacts/connections
  fixtures and loads
  mesh policy
  solver policy
  requested result fields
  validation requirements
```

Mesh, solver input decks, logs, matrices, and result fields are immutable artifacts, not canonical study state.

### SIM-001 — Revision-pinned solve

Every solve records document revision, configuration, effective component revisions, resolved topology table, materials database versions, mesher/solver fingerprints, units, policies, and assumptions.

### SIM-002 — Staleness from dependency keys

A study is current only when its setup/evaluation key matches the relevant semantic and geometry inputs. Cosmetic/display changes do not invalidate it; geometry, material, load, contact, mesh, or solver-policy changes do.

### SIM-003 — No silent remapping

Loads, fixtures, contacts, and probes use persistent topology references. Ambiguous or broken remapping blocks the affected study setup until repaired or explicitly accepted.

## 3. Backend ports

Separate ports exist for:

```text
geometry idealization/preparation
volume/surface meshing
solver deck generation
solve execution
result decoding
derived result evaluation
```

Solver adapters can share meshers; result visualization remains backend-independent.

### SIM-004 — Capability negotiation

Backends declare supported analysis types, element families, material models, contact/load/constraint kinds, units, limits, restart behavior, and determinism. Unsupported study constructs fail before expensive execution.

Study execution policy is explicit: manual, on-commit, or idle-debounced. Automatic policies create cancellable generation-tagged jobs and cannot consume unbounded resources or publish an older solve over a newer setup.

### SIM-005 — Neutral validated handoff

Each stage publishes a versioned intermediate manifest with input/output digests and diagnostics. The solver adapter cannot reinterpret an unsupported load or material silently.

## 4. Initial analyses

Phase 1 supports linear static structural and modal analysis under a precisely declared subset:

- isotropic linear elastic materials;
- supported solid element families;
- fixed and selected displacement constraints;
- force, pressure, gravity, and bearing loads as separately gated;
- bonded and selected assembly connection idealizations;
- small-displacement assumptions;
- requested displacement, stress, strain, reaction, factor-of-safety, and modal fields.

Every unsupported nonlinearity or missing material property produces a validation diagnostic.

Steady-state thermal and thermal-stress studies follow the same study/backend/artifact contracts after structural and modal validation. Later optimization varies declared semantic parameters in isolated revisions, evaluates stated objectives/constraints, and retains every candidate's CAD/simulation evidence. It does not mutate geometry inside a solver adapter.

## 5. Materials and safety factors

Material properties carry units, temperature/context applicability, source, version, uncertainty/notes, and approval classification. A visual appearance named “Aluminum” is not sufficient simulation material.

### SIM-006 — Factor-of-safety evidence

Factor of safety states the selected failure criterion, material property source, result component, averaging policy, and excluded/singular regions. It is not a universal scalar inferred from color.

## 6. Meshing

Mesh policy includes element type/order, global size, curvature/growth settings, topology-based local controls, quality criteria, and convergence intent.

### SIM-007 — Mesh quality gates

The mesher reports element counts, quality distributions, failed regions, topology association, and any geometry defeaturing. A solver does not start when mandatory quality/association gates fail.

### SIM-008 — Convergence is explicit

Kearne may automate refinement studies, but a single solved mesh is never labeled converged without a declared convergence metric and recorded sequence.

## 7. Execution and results

Solver jobs use isolated workers with bounded files/resources and explicit cancellation. Result fields are chunked, content-addressed artifacts associated with mesh entity IDs and semantic regions.

Visualization reports deformed-scale factor, component/averaging mode, units, min/max location, clipping, and stale status. Probes reference semantic topology plus nearest mesh evidence rather than persistent node indices alone.

Modal animation is a view of normalized eigenvectors with mode, frequency, phase, scale, and undeformed reference shown. It is not physical time response. Result data streams by bounded field/region chunks so first inspection does not require loading every result value.

### SIM-009 — Partial results

Non-converged or terminated solves may expose diagnostic partial artifacts only when the backend declares them valid for inspection. They are prominently classified and cannot satisfy design requirements automatically.

## 8. Verification strategy

Every backend passes capability and artifact conformance suites. Numerical verification uses parameterized families with known/manufactured solutions and convergence behavior, including bars, beams where applicable, plates, modal shapes, and thermal cases when introduced.

Properties include:

- unit scaling and rigid-transform invariance;
- mesh refinement trends toward known solutions;
- reaction/load balance within declared tolerance;
- symmetry relations;
- positive/ordered modal eigenvalues under valid constraints;
- identical input manifests produce compatible cache identity;
- topology edits correctly preserve or invalidate loads;
- cancellation/crash never publishes a result as complete.

Cross-backend comparison is diagnostic and tolerance-based, not assumed byte-identical truth. A curated validation suite with peer-reviewed reference results complements generators.

## 9. Open decisions

- **SIM-OPEN-001:** Initial mesher, solver, licenses, and redistribution policy.
- **SIM-OPEN-002:** Neutral mesh/result artifact schemas.
- **SIM-OPEN-003:** Exact first-release element/load/contact subset.
- **SIM-OPEN-004:** Material database source, licensing, and approval model.
- **SIM-OPEN-005:** Validation thresholds and disclaimers reviewed by qualified analysts.

## 10. Definition of done

An analysis type is supported only when setup schemas, capability validation, reference families, convergence evidence, artifact provenance, cancellation/fault behavior, and result visualization semantics pass review by a qualified simulation engineer.
