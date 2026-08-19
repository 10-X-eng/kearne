# Risk Register

- **Status:** Proposed; reviewed at every stage gate
- **Requirement prefix:** `RISK`
- **Depends on:** [Technical spikes](02-technical-spikes.md), all subsystem plans

## 1. Scoring

Likelihood and impact use `Low`, `Medium`, `High`, `Critical`. A critical-impact risk cannot be accepted implicitly. Each risk has a leading indicator, prevention, contingency, and release gate.

## 2. Active risks

| ID | Risk | Likelihood / impact | Leading indicator | Prevention | Contingency / gate |
|---|---|---|---|---|---|
| RISK-001 | Persistent topology cannot meet useful edit guarantees | High / Critical | generated edit matrix has nondeterministic or low-confidence matches | feature-defined names, history, honest ambiguity, early spike | narrow guarantee/feature set; no downstream-feature breadth before SPIKE-004 passes |
| RISK-002 | Document schema bakes in single-part or positional identity | Medium / Critical | adapters need special IDs or copied entities | typed global IDs, component/body/result distinction, assembly/config review | migrate before public format freeze; Gate A blocks shortcuts |
| RISK-003 | Async jobs publish mixed/stale revisions | Medium / Critical | intermittent visual/geometry mismatch | immutable snapshots, evaluation keys, generation checks, virtual scheduler | stop feature work; scheduler state-machine gate |
| RISK-004 | Undo, persistence, versioning, and collaboration diverge | Medium / Critical | multiple history representations or inverse-command logic | one revision DAG and head model | remove alternate history path before format freeze |
| RISK-005 | OCCT crashes/thread limits undermine responsiveness | High / High | hangs, global-state races, large serialization cost | process spike, risk-based workers, pinned version, artifacts | reduce concurrency/use dedicated workers; preserve UI/core |
| RISK-006 | Qt Quick/AIS integration blocks portability or replacement | High / High | OpenGL-only leakage, QML/render deadlocks | renderer port and viewport spike | choose Kearne mesh backend before UI breadth |
| RISK-007 | Sketch solver lacks diagnostics/stability/license fit | High / High | order-sensitive solutions, weak conflict sets, redistribution issue | generated candidate comparison | narrow constraints or fund solver; no solver-specific persistence |
| RISK-008 | One-file storage has latency/corruption/large-artifact limits | Medium / Critical | WAL/copy contention, huge DB amplification | SQLite fault/performance spike, external derived cache | change physical storage before public format; logical ports remain |
| RISK-009 | Schema/code generation increases complexity instead of reuse | Medium / High | duplicate converters, slow builds, weak unions/units | one end-to-end boundary spike | reduce generation scope; retain typed domain boundary |
| RISK-010 | Python/plugin isolation is overstated | High / Critical | filesystem/network/native escape | OS probes, denied capabilities, honest wording | restrict to trusted code or disable capability per platform |
| RISK-011 | AI produces unsafe/stale or data-leaking actions | High / Critical | policy bypass in adversarial sequence | deterministic local policy, revision anchoring, disclosure limits | disable provider/tools; `No AI` remains functional |
| RISK-012 | Feature growth outruns tests and migrations | High / High | descriptor without generators/migration, rising fixed tests | registry-enforced conformance and stage gates | freeze new features until assurance debt clears |
| RISK-013 | Tests become slow, flaky, and example-bound | Medium / High | retries, sleeps, fixture count and CI time rise linearly | virtual time, seeded scalable suites, sharding, shrink/replay | quarantine with expiry; repair assurance primitive |
| RISK-014 | Performance targets are gamed by vague fixtures | Medium / High | FPS claims omit triangles/instances/cache state | parameterized workloads and full environment records | reject result; block release budget claim |
| RISK-015 | Third-party licenses prevent distribution | Medium / Critical | solver/Qt/translator obligations unresolved | SPIKE-009, SBOM/license gate | replace/narrow capability before dependency lock |
| RISK-016 | Plugin extensibility compromises ABI/data longevity | Medium / High | private headers/in-process state enter plugins | wire boundary, opaque data preservation, conformance SDK | restrict trusted-native plugins; safe-open mode |
| RISK-017 | Configuration cache/state explodes combinatorially | Medium / High | artifacts scale with unused Cartesian product | explicit context, reachable-input keys, demand evaluation, quotas | validate named matrix only; evict derived variants |
| RISK-018 | Simulation output is treated as validated engineering truth | Medium / Critical | FOS shown without assumptions/convergence | provenance, capability validation, reference solutions, expert gate | label unsupported; block automated requirement pass/release |
| RISK-019 | Cross-platform behavior diverges | High / High | projects/tests pass on one OS only | shared schemas/contracts, early physical CI, pinned profiles | narrow supported baseline transparently before release |
| RISK-020 | Team duplicates rules across UI/Python/AI/plugins | High / High | adapter-specific validators or direct document writes | one command/query API, architecture tests, generated metadata | reject/remove alternate path before merge |
| RISK-021 | QML screens drift into inconsistent, inaccessible chrome | Medium / High | hardcoded colors/spacing, local control forks, clipped DPI/locales | token layer, state-complete component catalog, responsive/accessibility matrix | block new screen patterns until the shared component supports them |

## 3. Review protocol

At each stage gate:

1. update evidence and indicator trend;
2. add risks exposed by failures/spikes;
3. link accepted mitigation requirements/tests/ADRs;
4. assign an accountable subsystem role and next decision gate;
5. mark residual risk explicitly as accepted, mitigated, transferred, or blocking.

Closed risks remain recorded. A recurring cause reopens the original risk rather than receiving a new ID.

## 4. Definition of done

No release proceeds with an unowned critical risk, a missed gate, or mitigation described only as future monitoring. Accepted residual risks appear in release notes where users need them to make engineering decisions.
