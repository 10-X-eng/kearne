# Build, Packaging, and Release

- **Status:** Proposed
- **Requirement prefix:** `BLD`
- **Depends on:** [System architecture](../01-system-architecture.md), [security](06-security-threat-model.md)
- **Unblocks:** reproducible development and distribution

## 1. Purpose

Build, test, package, sign, update, debug, and reproduce Kearne across Windows and Linux with pinned dependencies and traceable generated code.

## 2. Build system

The proposed baseline is CMake presets plus Ninja, CTest integration, C++23, and a manifest/lock-based C++ dependency manager selected by spike. Python environments use a separate immutable lock.

### BLD-001 — Presets are the interface

Documented CMake presets define supported developer, CI, sanitizer, fuzz, benchmark, and release builds. CI invokes the same presets available locally.

### BLD-002 — No ambient dependency search

Release and CI builds resolve dependencies from pinned manifests/toolchains. User-global include/library paths, Python packages, PATH-order DLLs, and undeclared SDKs cannot affect the build.

### BLD-003 — Generated code is deterministic

Schema/binding/resource generation declares inputs/outputs and tool versions. CI regenerates and fails on drift. Generated files are either reproducibly checked in by policy or built from pinned tools; the repository does not mix approaches per generator.

## 3. Toolchain matrix

Named release toolchains include:

- MSVC on Windows;
- Clang and/or GCC baseline selected for Linux;
- one Clang-based sanitizer/static-analysis build;
- pinned Qt, OCCT, Python, OCP/build123d, solver, IDL, database, and packaging versions.

Warnings are centrally configured. Third-party warnings remain isolated. Compiler-specific behavior cannot enter persisted semantics without evaluator fingerprint/version policy.

## 4. Dependency governance

Every dependency record contains:

```text
name/version/source/digest
license and redistribution obligations
runtime/build-only classification
known ABI coupling
security/advisory source
update owner and cadence
replacement boundary
local patches and upstream status
```

### BLD-004 — Single declared version

A release contains one compatible version per runtime dependency unless isolation justifies multiple versions. Qt/OCCT/OCP ABI or serialization mismatches fail build/worker handshake.

### BLD-005 — SBOM and notices

Release artifacts include a machine-readable SBOM and generated notices/source-offer material required by licenses. License compliance is a release gate.

## 5. Source and module boundaries

CMake targets match architectural libraries and expose minimal public headers. Include/link dependency tests enforce the architecture graph. Unity builds may improve developer speed but non-unity builds remain required to catch missing includes and hidden coupling.

Public headers avoid Qt/OCCT exposure unless the boundary requires their types. PImpl/type-erasure is limited to external ABI boundaries where measurements show lower rebuild or compatibility cost.

## 6. CI

Required pipelines:

- format/generated-schema/architecture checks;
- supported compiler builds;
- per-change assurance profile;
- sanitizer and concurrency builds;
- fuzz and fault jobs;
- package/install/uninstall smoke;
- dependency/license/security scan;
- performance trend jobs;
- signed release candidate and update/rollback test.

Jobs publish exact source revision, lockfiles, toolchains, test seeds, logs, symbols, SBOM, and package digests.

### BLD-006 — Hermetic release input

Release builds use reviewed source and lockfiles, isolated builders, pinned base images/toolchains, and no undeclared network fetch during compilation/package assembly.

## 7. Packaging

Windows and Linux packages install the application, workers, runtime libraries, resources, schema manifests, pinned Python environment where selected, licenses, and crash-symbol identity coherently.

### BLD-007 — Relocatable verified workers

Worker discovery uses signed installation metadata and absolute verified paths. Current directory and user PATH cannot substitute worker executables or DLLs.

### BLD-008 — User data separation

Install/update/uninstall never treats project files, user scripts, plugins, cache, or settings as application-owned removable content without explicit scope and confirmation.

## 8. Updates and rollback

- Update metadata and packages are signed and verified before execution.
- Channels are explicit: development, preview, stable.
- Application rollback remains possible while project migrations may be forward-only; the UI warns and creates recovery copies before format migration.
- Worker/application versions update atomically as a compatible set.
- Offline installers remain available for local-first use.

### BLD-009 — No silent format lockout

An update does not migrate a project merely during background scanning. Migration begins on explicit open with compatibility/recovery behavior from the persistence plan.

### BLD-010 — Codex compatibility is generated and pinned

Developer, CI, and release builds pin one Codex executable version and generate the app-server JSON Schema used by the adapter. A host-language binding is generated only when consumed. CI fails on unreviewed schema drift. Packaging, external discovery, and independent update policies MUST verify executable identity before launch and preserve `No AI` operation when unavailable.

### BLD-011 — Observation driver ships with testable artifacts

Developer, CI, and release-candidate outputs include the application lifecycle/observation driver and its matching protocol schema. Release packaging may capability-disable agent control by default but MUST retain the same capturable UI implementation tested before release.

## 9. Diagnostics and symbols

Build identity maps every binary, generated schema set, dependency fingerprint, and symbol package. Crash dumps exclude project memory/content under available platform controls and require consent for upload; retained symbols support offline analysis.

## 10. Open decisions

- **BLD-OPEN-001:** C++ dependency manager after reproducibility/license spike.
- **BLD-OPEN-002:** Linux distribution baselines and package formats.
- **BLD-OPEN-003:** Windows installer/updater framework and signing identity.
- **BLD-OPEN-004:** CI and release-builder service.
- **BLD-OPEN-005:** Crash reporting provider and privacy posture.
- **BLD-OPEN-006:** Bundle Codex, install it as a managed companion, or validate a separately installed executable.

## 11. Definition of done

Build/release v1 is implemented when a clean machine can reproduce supported builds from locked inputs, packages install/update/rollback under tests, binaries and workers verify compatibility, SBOM/licenses pass, and user projects remain untouched by uninstall/update failure.
