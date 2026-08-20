# Materials and Standard Components

- **Status:** Proposed; required by sheet metal, assemblies, simulation, CAM, drawings, and BOM
- **Requirement prefix:** `MAT`
- **Depends on:** [Document model](../foundations/01-document-model.md), [units](../foundations/05-units-expressions-numerics.md), [configurations](13-configurations.md)
- **Unblocks:** engineering calculations and reusable fasteners

## 1. Purpose

Provide versioned engineering properties and reusable standard components without treating appearance, a catalog name, or copied geometry as sufficient identity.

## 2. Canonical records

```text
MaterialDefinition
MaterialAssignment
MaterialLibraryRef
StandardComponentDefinition
StandardComponentSelection
ThreadSpecification
```

Definitions carry stable identity, source and edition, units, applicable temperature/process/condition, uncertainty or approval class, and license. Project assignments target semantic components, bodies, regions, or simulation idealizations. Appearance is a separate property group.

### MAT-001 — Versioned engineering values

Density, elastic and thermal properties, strengths, forming data, cost, and custom properties MUST identify their units, conditions, provenance, and library version. A changed library creates an explicit update proposal; it never changes an existing revision silently.

### MAT-002 — Required-property profiles

Consumers declare required properties and conditions. Simulation, sheet metal, CAM, mass, and costing fail with structured missing/inapplicable-property diagnostics instead of substituting a similarly named material.

### MAT-003 — Assignment precedence

Region override, body, component, configuration, and project defaults use one declared precedence order. Queries return the effective value and its complete resolution path.

## 3. Standard components and fasteners

A standard component selection records catalog/standard edition, family, size, material/coating, thread, configuration, supplier data when relevant, and geometry capability. Geometry is produced by a pinned native build123d library function and referenced as an ordinary component output; catalog records do not contain a second BREP model.

### MAT-004 — Standard identity

`M6 × 1 × 20` is not a complete identity. Thread series, tolerance class, head/drive, length convention, material, coating, and standard edition are explicit where applicable.

### MAT-005 — Cosmetic threads by default

Thread semantics are canonical. Cosmetic, simplified, and modeled geometry are declared representations. BOM, hole matching, interference policy, and drawings use thread semantics rather than inferring them from tessellation.

### MAT-006 — Fit-aware insertion

Fastener tools resolve compatible hole/thread standards, grip stack, washers/nuts, length availability, and access. Automatic selection is a proposal; acceptance creates ordinary component occurrences and joints.

### MAT-007 — Forkable library content

Users may pin a library item or fork its source and metadata into the project. Forking allocates new identity and preserves provenance; it never mutates the shared definition.

### MAT-008 — Release support manifest

Every shipped library has a signed, versioned machine-readable support manifest. It enumerates each material card and standard-component parameter row, provenance/license, validation status, required consumer properties, generator fingerprint, available geometry representations, semantic interfaces, and any exclusion with reason. GUI, AI, Python, headless queries, packaging checks, and conformance generators consume this manifest rather than separate allowlists.

### MAT-009 — Exact catalog resolution

Selection resolves one exact published row. An invalid family/standard/size/pitch/length/variant combination returns bounded deterministic allowed or nearest alternatives but does not select them. Recompute against a missing, excluded, or changed row retains identity and last-known-good derived geometry with a direct unavailable diagnostic.

### MAT-010 — Modeled-thread resource profile

Modeled threads are opt-in asynchronous evaluations. Each release profile sets finite limits for nominal diameter, axial length, turn count, modeled-thread objects per evaluation, wall time, worker memory, and output topology. Missing limits disable modeled threads. Exceeding a limit refuses or requests an explicit higher resource profile; it never silently substitutes cosmetic geometry. Preview, navigation, BOM, fit, and drawings use thread semantics or cosmetic representation unless exact helical topology is explicitly required.

### MAT-011 — Shared fit tables

Hole and fastener tools consume the same versioned fit/thread tables. A table row declares standard/edition, nominal series, tolerance class, hole form, clearance/tap/counterbore/countersink dimensions, interpolation policy when applicable, and supported component interfaces. Modeling, assembly checks, drawings, and BOM report the effective row and resolution path.

## 4. Libraries and trust

Built-in, user, team, and project libraries use signed/versioned manifests and content digests. External content is parsed under size/depth limits. A missing library preserves assignments and standard-component selections as opaque usable references with an unavailable diagnostic.

## 5. Verification

Schema-driven generators create valid and incomplete material cards across units, conditions, and versions. Properties verify unit equivalence, precedence, stale invalidation, missing-property refusal, and save/reload preservation.

Standard-component generators cover metric and inch series, boundary sizes, invalid combinations, representation changes, repeated instances, hole-fit checks, and BOM aggregation. Geometry tests compare declared envelopes, masses, interfaces, and labeled outputs rather than exact BREP bytes.

Package verification enumerates the support manifest, rejects undeclared or generator-failing rows, and proves the GUI/query/generator sets are identical. Resource tests generate thread-limit boundaries and verify refusal, cancellation, worker loss, and semantic fallback behavior.

## 6. Performance and cancellation

### MAT-012 — Bounded library and geometry cost

`MATERIAL-REF-1` measures cold/warm catalog load, deterministic search, effective-property resolution, standard-component generation by representation, and repeated-instance reuse. It records row/property counts, index and worker memory, artifact bytes, topology count, and cancellation time. Catalog indexing is lazy and byte-bounded; project open does not generate component geometry or load unused material payloads.

Repeated identical selections share geometry artifacts. Cosmetic selection and property resolution meet the performance-plan targets. Modeled-thread work follows its resource profile, remains cancellable, and cannot block UI, assembly navigation, or BOM queries.

## 7. Acceptance

- Assign a versioned material; mass, sheet-metal rules, and a linear-static study consume the same effective properties and provenance.
- Insert repeated standard fasteners into compatible holes; occurrences share geometry artifacts, joints solve, cosmetic threads remain selectable, and BOM quantities/configurations are correct.
- Remove the library cache; project intent remains intact and reports exactly which evaluation is unavailable.

## 8. Open decisions

- **MAT-OPEN-001:** Built-in material/standard data sources, editions, licenses, and qualified review.
- **MAT-OPEN-002:** Signed library package and update format.
- **MAT-OPEN-003:** First fastener families, fit tables, thread standards, and modeled-thread policy.
- **MAT-OPEN-004:** Cost/supplier data privacy and refresh policy.

## 9. Definition of done

The capability is implemented when versioned property resolution passes generated unit/condition/update suites, missing data cannot create unsupported engineering claims, and fastener selection produces reusable canonical components that survive configuration, assembly, drawing, and BOM workflows.
