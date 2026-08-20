# Bill of Materials

- **Status:** Proposed; post-assembly
- **Requirement prefix:** `BOM`
- **Depends on:** [assemblies](08-assemblies.md), [configurations](13-configurations.md), [materials and standard components](19-materials-and-standard-components.md)
- **Unblocks:** associative drawing tables, procurement export, and release

## 1. Purpose

Produce traceable quantities and product data from exact component occurrences without maintaining a second manual assembly list.

## 2. Canonical intent

```text
BOMViewDefinition
  source assembly/component and configuration scope
  structure mode, inclusion policy, grouping keys
  columns, filters, sort, units, currency context

BOMOverride
  semantic target, typed field, scope, justification
```

Rows, totals, drawing tables, and export files are derived from these records and the selected immutable product revision.

### BOM-001 — Product graph is authoritative

Occurrences, suppression, configurations, materials, standard-component selections, and metadata determine BOM membership and quantity. Editing a displayed row cannot create or delete an occurrence implicitly.

### BOM-002 — Exact evaluation scope

Every result records project revision, assembly root, effective component revisions and configurations, library versions, view definition, units, and evaluator version. Floating inputs resolve to an explicit snapshot before calculation.

## 3. Structure and identity

Supported views include indented, parts-only, and flattened. Inclusion policy distinguishes normal, phantom, reference, and excluded occurrences. Virtual/nongeometric items require explicit typed records.

### BOM-003 — Stable row identity

Grouping uses declared semantic keys such as component definition, effective configuration, material, finish, and part number—not display name or tree position. A row retains identity across sorting and presentation changes; a grouping-key change produces a new row relationship with provenance.

### BOM-004 — Honest hierarchy

Indented views preserve assembly paths and subassembly quantities. Flattening multiplies nested quantities with checked arithmetic. Cycles, unresolved links, and invalid configuration references make the affected result incomplete; they are never skipped silently.

### BOM-005 — Typed quantities

Discrete items use nonnegative integer quantities. Length, area, volume, mass, and other bulk items use dimensional quantities with explicit rounding and aggregation policy. Unit conversion cannot change the physical total.

## 4. Fields and overrides

Columns may project stable identity, part number, revision, description, configuration, material, finish, mass, make/buy, supplier/catalog references, cost context, quantity, and schema-registered custom properties.

### BOM-006 — Value provenance

Each cell can report its source field, effective revision/configuration, library source, expression or aggregation rule, and override. Missing or conflicting required values remain diagnostics rather than empty success.

### BOM-007 — Scoped overrides

Overrides target a stable definition, occurrence path, or BOM row and declare configuration/release scope. Precedence is explicit. Overrides cannot falsify occurrence count or erase unresolved structure; manual quantity adjustments require a separately classified nonmodel item or approved exception.

## 5. Associativity, export, and release

BOM evaluation is asynchronous and publishes generation-tagged immutable projections. A source change makes older results stale until a matching evaluation completes. The UI retains the last accepted result with its stale state.

Drawing tables reference a `BOMViewDefinition` and evaluated artifact. CSV and spreadsheet export are atomic views with schema, units, locale, revision, incompleteness, and digest metadata. Released BOMs pin all effective inputs and exported bytes.

### BOM-008 — No false completeness

Unresolved links, stale inputs, missing required fields, arithmetic overflow, unsupported item types, or failed policies produce a non-releasable result. Filtering does not convert an incomplete source graph into a complete BOM.

## 6. Verification

Generated occurrence graphs vary nesting, repeated definitions, configurations, suppression, phantom/reference policy, unresolved links, cycles, standard hardware, bulk items, overrides, and property completeness. Properties verify:

- tree-order and unit invariance;
- exact quantity multiplication and aggregation;
- stable rows across sorting and irrelevant metadata edits;
- configuration-specific inclusion and grouping;
- identical results through UI, headless API, Python, AI, drawing, and export adapters;
- stale, cancelled, or failed jobs never replace a current result;
- parsed exports reproduce the projected rows and metadata.

Scale profiles measure recomputation by affected occurrence subgraph, first-page latency, memory, sorting/filtering, and export at increasing hierarchy and column counts.

## 7. Acceptance

Create a configured assembly with repeated parts, nested subassemblies, standard fasteners, a phantom group, and a bulk item. Generate indented and parts-only views; trace every value; change configuration and suppression; verify quantities and staleness; place the associative table on a drawing; export it through UI, headless API, Python, and AI using the same query/command model; release the pinned result.

## 8. Open decisions

- **BOM-OPEN-001:** First-release field and custom-property schemas.
- **BOM-OPEN-002:** Cost/currency, supplier, and privacy policy.
- **BOM-OPEN-003:** Approved nonmodel-item and manual-exception workflow.
- **BOM-OPEN-004:** Spreadsheet round-trip scope versus export-only behavior.

## 9. Definition of done

BOM is implemented when generated product graphs produce traceable, configuration-correct results; incomplete inputs cannot be released; drawing/export round trips pass; and large-BOM workloads meet the accepted performance profile through every supported adapter.
