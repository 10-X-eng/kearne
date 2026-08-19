# Units, Expressions, and Numerical Policy

- **Status:** Proposed
- **Requirement prefix:** `NUM`
- **Depends on:** [Document model](01-document-model.md)
- **Unblocks:** sketcher, modeling, import/export, simulation, API schemas

## 1. Purpose

Give every subsystem one dimensional value model and one tolerance policy. Numerical behavior is engineering state: ad hoc conversions and per-feature epsilons create irreproducible geometry, invalid simulations, and incompatible APIs.

## 2. Quantity model

### NUM-001 — Dimensional types

All public engineering values use `Quantity<Dimension>` or a runtime equivalent carrying a checked dimension. Plain floating-point parameters are limited to truly dimensionless ratios and internal numerical implementation.

The dimension system initially distinguishes:

```text
Length, Mass, Time, Angle, TemperatureAbsolute, TemperatureDelta,
ElectricCurrent, Amount, LuminousIntensity, Dimensionless
```

Angle remains a distinct engineering dimension even though SI treats radians as derived dimensionless. This deliberately rejects adding degrees to a scalar. Absolute and differential temperatures obey different arithmetic.

### NUM-002 — Canonical units

Canonical semantic and wire values use SI base units and IEEE-754 binary64 finite values. The OCCT adapter converts length to its pinned kernel convention, initially millimeters, at one boundary. Display units never alter canonical values.

### NUM-003 — No exceptional floating values

NaN and positive/negative infinity are invalid in canonical documents, commands, expressions, hashes, and wire messages. Parsers reject overflow and underflow that would silently produce unusable engineering values.

### NUM-004 — Locale-independent persistence

Canonical serialization and expression grammar use `.` as decimal separator and locale-independent unit symbols. UI input/output may use locale formatting but converts at the adapter boundary.

## 3. Units and formatting

Unit definitions include stable ID, symbol aliases, dimension, exact or declared conversion factor, display precision policy, and source/version for standards-based units.

### NUM-005 — Values are not strings

APIs and persistence transmit numeric canonical value plus dimension/unit metadata where needed. Strings such as `"5 mm"` are accepted only by explicit parsing endpoints, never as the only typed representation.

### NUM-006 — Display precision is non-destructive

Changing project display units or decimal precision MUST NOT round stored values. Commands generated from unchanged fields preserve their original semantic value.

## 4. Expression model

An accepted expression stores:

```text
source_text
parsed versioned AST
resolved symbol bindings by ID
inferred result dimension
declared result type
```

### NUM-007 — Parse, bind, type-check, evaluate

Expression processing is split into reusable stages. Features MUST NOT implement their own expression parsers or variable lookup.

### NUM-008 — Dependency extraction

Resolved parameter bindings are explicit dependencies used by the document graph. Name changes do not rebind an accepted expression; editing its source triggers binding again.

### NUM-009 — Cycle diagnostics

Parameter cycles are rejected before evaluation with a diagnostic containing a minimal cycle path.

### NUM-010 — Deterministic function set

The core function set is versioned and deterministic for a pinned runtime. Trigonometric functions require angles and document their return dimensions. Randomness, current time, filesystem, network, and implicit global configuration are prohibited.

### NUM-011 — Expression evolution

The parser grammar and function semantics carry a language version. Stored ASTs migrate explicitly; opening an older document MUST NOT reinterpret source under new precedence or function rules silently.

## 5. Numerical profile

Every evaluation request carries a versioned `NumericalProfile` containing at least:

```text
model linear resolution
relative comparison tolerance
angular resolution
parameter/domain resolution
topology matching tolerances
import healing policy ID
tessellation defaults (separate from exact geometry)
allowed model extent range
```

### NUM-012 — Central comparisons

Foundation utilities implement approximate equality, ordering near tolerance, normalized signature comparison, and geometry scale checks. Feature code MUST NOT introduce unexplained magic epsilons.

### NUM-013 — Tolerance purpose separation

Modeling tolerance, import healing tolerance, topology matching tolerance, constraint-solver convergence, tessellation deflection, and display pixel thresholds are distinct values. One “global tolerance” MUST NOT serve all purposes.

### NUM-014 — Persisted numerical identity

The project records its modeling numerical profile ID. Evaluation keys include it. Changing the profile is an explicit document operation that invalidates affected results and may generate topology diagnostics.

### NUM-015 — Geometry range validation

Commands reject semantically detectable out-of-range geometry before kernel evaluation. Kernel-discovered range failures use the same diagnostic category. Diagnostics report the value in user units and the supported range.

## 6. Determinism

Canonical hashing normalizes:

- message field ordering through the canonical encoder;
- negative zero where semantically equivalent;
- unit representation to canonical values;
- map ordering;
- prohibited NaN encodings;
- expression AST versions.

Kearne does not promise bit-identical transcendentals or OCCT BREP across different platforms/versions. It promises semantic values within declared tolerance for a pinned evaluator fingerprint and reports determinism class with artifacts.

## 7. Verification strategy

Generated dimensional ASTs verify:

- type-correct expressions evaluate with the inferred dimension;
- invalid dimension combinations are rejected;
- converting through any compatible unit round-trips within declared tolerance;
- changing display units leaves canonical digests unchanged;
- binding is stable under display-name changes;
- generated dependency cycles are detected;
- parser/formatter fuzz inputs never crash or create non-finite canonical values.

Metamorphic geometry tests apply unit-equivalent inputs, rigid transforms, and supported uniform scaling to validate feature relations rather than fixed coordinates.

## 8. Open decisions

- **NUM-OPEN-001:** Quantity library: audited third-party library versus a small Kearne implementation.
- **NUM-OPEN-002:** Decimal input preservation requirements beyond retained expression source.
- **NUM-OPEN-003:** Initial supported modeling extent and resolution based on OCCT spike measurements.
- **NUM-OPEN-004:** Cross-platform math library requirements for expression determinism.

## 9. Definition of done

This plan is implemented when all command/API fields use registered dimensions, generated expression and conversion suites pass, the kernel conversion exists in one adapter, and a repository scan/architecture test prevents unmanaged dimensional doubles in public engineering schemas.
