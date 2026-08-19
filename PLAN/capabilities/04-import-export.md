# Import and Export

- **Status:** Proposed
- **Requirement prefix:** `XCHG`
- **Depends on:** [Persistence](../foundations/06-persistence-and-recovery.md), [processes](../foundations/07-processes-and-ipc.md), [numerics](../foundations/05-units-expressions-numerics.md), [solid modeling](03-solid-modeling.md)
- **Unblocks:** MVP interchange and migration workflows

## 1. Purpose

Exchange engineering data without allowing untrusted parsers, ambiguous units, lossy healing, or partial output files to compromise project integrity.

## 2. Import model

Import is a staged operation:

```text
explicit file grant
  -> source artifact capture and digest
  -> isolated parse
  -> neutral import report
  -> optional policy-driven healing
  -> proposed semantic entities/artifacts
  -> user confirmation/command transaction
```

### XCHG-001 — Source retention

The exact imported source bytes and declared media/format metadata are retained as an immutable source artifact unless the user explicitly chooses an external-link-only workflow. An imported body can therefore be recovered or retranslated after cache loss.

### XCHG-002 — No parser mutation

Import workers cannot mutate the document. They return a bounded proposal containing semantic metadata, artifact handles, diagnostics, and provenance; an ordinary command transaction accepts it.

### XCHG-003 — Units are explicit

Format-declared units are recorded. Missing or conflicting units require a visible policy/confirmation and are never guessed from bounding-box size without reporting that inference.

## 3. MVP formats

- STEP import/export with pinned OCCT translator capabilities.
- STL export, binary by default with explicit tessellation profile and units convention.

IGES, BREP, OBJ, 3MF, DXF, DWG, glTF, JT, Parasolid, and proprietary formats require separate format descriptors, security review, license review, and conformance profiles. A filename extension alone does not imply full-standard support.

## 4. STEP import semantics

The importer reports and preserves where supported:

- source schema/AP and translator version;
- product names and hierarchy;
- units and coordinate systems;
- colors/layers/property metadata;
- assembly structure for later use;
- exact body/shell shape status;
- unsupported or dropped entities;
- healing actions and tolerances.

MVP may flatten assembly structure into retained metadata plus imported bodies only if the report says so and the source artifact remains available.

### XCHG-004 — Imported identity

Imported component/body IDs are allocated once at transaction acceptance. Reimport/update later uses source entity identifiers plus structural/geometric reconciliation and never assumes source ordering is stable.

## 5. Healing

Healing is an explicit versioned policy and produces a before/after diagnostic report. Actions are classified as:

- representation normalization;
- tolerance adjustment;
- gap/edge repair;
- topology-changing repair;
- discarded invalid content.

### XCHG-005 — Material repair disclosure

Topology-changing repair and tolerance expansion beyond the normal profile require confirmation or an import-policy setting that is recorded in provenance. Original bytes are always retained.

## 6. Export model

An export request identifies:

```text
project and immutable revision
configuration context
semantic targets
format/profile version
unit/precision settings
tessellation settings when applicable
destination capability grant
overwrite policy
```

### XCHG-006 — Immutable source revision

Export resolves and evaluates one immutable revision. Edits made while it runs do not produce mixed-revision output.

### XCHG-007 — Atomic destination

Export writes to a private sibling temporary file or brokered destination, flushes and validates it, then atomically replaces/renames according to explicit overwrite policy. Cancellation or failure leaves the previous destination intact.

### XCHG-008 — Export report

Every export returns format/version, source revision, entities written/skipped, units, tolerances/tessellation, warnings, output digest, and only destination details permitted by the caller's capability.

## 7. Security and limits

- Import parsing runs isolated with file/network capabilities denied except brokered input.
- Compressed/nested formats have expanded-byte, entity-count, depth, time, and memory limits.
- Export destination access is capability-scoped and does not expose arbitrary filesystem browsing to AI.
- Filenames and embedded metadata are sanitized for UI/log display.
- Unknown extensions are sniffed conservatively; content and chosen parser must agree.

## 8. Verification strategy

Each format adapter implements a shared import/export conformance port. Generated analytic shapes and assemblies, dimensional scale variations, transforms, names, and metadata exercise round trips.

Oracles compare supported invariants:

- valid shape/body count policy;
- volume/area/bounds within profile tolerance;
- hierarchy/name/color preservation declared by the profile;
- unit equivalence;
- source and output digests/provenance;
- no document mutation before acceptance;
- atomic output under injected failures.

Parsers are fuzzed with malformed, truncated, oversized, and adversarial files. A curated corpus covers externally produced dialects and past crashes, but does not become the only test mechanism.

## 9. Open decisions

- **XCHG-OPEN-001:** Exact STEP application protocols and metadata subset promised by MVP.
- **XCHG-OPEN-002:** STL unit convention and companion metadata UX.
- **XCHG-OPEN-003:** Reimport/update identity scope after MVP.
- **XCHG-OPEN-004:** Per-format licensing and third-party translator roadmap.
- **XCHG-OPEN-005:** Default healing profile after corpus measurement.

## 10. Definition of done

A format is supported only when its declared profile, isolated adapter, limits, reports, generated conformance suite, fuzz target, atomic output behavior, and known-loss documentation pass on both supported platforms.
