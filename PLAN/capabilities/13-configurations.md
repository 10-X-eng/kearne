# Configurations

- **Status:** Proposed; post-MVP core capability
- **Requirement prefix:** `CFG`
- **Depends on:** [Document model](../foundations/01-document-model.md), [units and expressions](../foundations/05-units-expressions-numerics.md), [evaluation](../foundations/03-evaluation-and-jobs.md)
- **Unblocks:** part families, assembly variants, configured simulation and drawings

## 1. Purpose

Represent related design variants as typed overrides on one function/product graph. Configurations do not copy source trees or hide process-global active values.

## 2. Canonical model

```text
ConfigurationDefinition
  inputs: map<ConfigurationInputId, typed declaration>
  named rows: map<ConfigurationId, input values>
  derived rows/inheritance
  validation rules

ConfigurationContext
  definition ID/version
  resolved input values
  canonical digest
```

Configuration-controlled fields contain a base value/expression plus a typed mapping or expression over configuration inputs.

### CFG-001 — Override, do not clone

A configuration changes declared parameter values, suppression, material, component selection, quantity, joint, metadata, or other configurable fields. It MUST NOT duplicate entities solely to represent another size.

### CFG-002 — Explicit context

Every configuration-dependent query, evaluation, export, assembly occurrence, simulation, and drawing carries a `ConfigurationContext`. A process-global active configuration is presentation convenience only.

### CFG-003 — Stable identity across variants

An entity retains the same ID across configurations. A suppressed or non-produced result remains addressable semantically and reports configuration-specific availability.

## 3. Input types and resolution

Inputs may be dimensional quantities, integer/boolean, enum, string metadata, material refs, component/version refs, or bounded sets defined by a schema.

Resolution order is explicit:

```text
base field
  -> inherited named configuration values
  -> selected named row
  -> permitted call-site parameter overrides
  -> typed validation and canonical context digest
```

### CFG-004 — No ambiguous precedence

The resolver reports the source of each effective value. Cyclic inheritance, duplicate conflicting assignments, and invalid dimensions fail before evaluation.

### CFG-005 — Resolved values are inspectable

Queries, diffs, AI context, and diagnostics can return base value, effective value, controlling input, and override source without executing function-specific UI code.

## 4. Suppression and references

Suppression is evaluated per configuration. Downstream references resolve in that context.

### CFG-006 — Context-specific health

A function call can evaluate successfully in one configuration and have a broken dependency in another. Health and last-known-good artifacts are keyed by configuration; success in one does not mask failure in another.

### CFG-007 — Configuration validation set

Documents declare named configurations required for validation/release. Kearne does not promise to eagerly evaluate every value in a combinatorial parameter space.

## 5. Caching and scale

Evaluation keys include the canonical digest of only configuration inputs that can reach a node through declared dependencies. Irrelevant configuration changes do not invalidate a function call.

### CFG-008 — Demand evaluation

Configurations evaluate on demand and through explicit validation matrices. Cache quotas prevent an unbounded product of configurations, revisions, and function artifacts.

## 6. Assemblies and external use

An instance references a component version and configuration context. A named configuration removed or renamed upstream does not silently choose another; update produces a broken-reference diagnostic or explicit remapping command.

Configuration tables are importable/exportable through typed columns with unit and enum metadata. CSV is an interchange view, not canonical configuration storage.

## 7. Diff and merge

Semantic diff distinguishes base-field edits, input declaration changes, row additions/removals, and cell overrides. Merge operates by stable input/row IDs, not row number or display name.

Changing an input type or dimension is a schema-affecting edit that validates all retained rows before commit.

## 8. Verification strategy

Generate typed configuration definitions, inheritance DAGs, rows, override subsets, and function-call dependency graphs. Verify:

- resolution is deterministic and independent of map order;
- unrelated inputs do not change evaluation keys;
- invalid dimensions/cycles fail before evaluation;
- stable entity IDs persist across suppression states;
- save/reload, rename, diff, and merge preserve row/input identity;
- validation evaluates the declared matrix and reports each context separately;
- cache use remains bounded under generated context sequences.

Function contracts and record schemas contribute configurable inputs to the same suite; no separate hand-written test is required for every parameter.

## 9. Open decisions

- **CFG-OPEN-001:** Inheritance versus formula-only derivation for named rows.
- **CFG-OPEN-002:** Allowed ad hoc overrides outside named configurations.
- **CFG-OPEN-003:** Configuration table UX and bulk-edit transaction behavior.
- **CFG-OPEN-004:** Release policy for parameter ranges versus enumerated named rows.

## 10. Definition of done

Configurations are implemented when all registered configurable fields use one resolver, generated variant matrices pass, evaluation keys depend only on reachable inputs, and assemblies/exports/diffs identify exact effective contexts.
