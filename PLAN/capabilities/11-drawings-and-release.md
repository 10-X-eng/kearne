# Drawings, Standards, and Release

- **Status:** Proposed; post-modeling and assembly
- **Requirement prefix:** `DRW`
- **Depends on:** [Persistent topology](../foundations/04-persistent-topology.md), [versioning](09-versioning-and-merge.md), [rendering](01-rendering-and-selection.md)
- **Unblocks:** manufacturing documentation and controlled release

## 1. Purpose

Create associative, editable engineering documentation tied to immutable design revisions and versioned standards data. A drawing is a semantic document projection, not a collection of disconnected rendered lines.

## 2. Canonical drawing entities

```text
Drawing
Sheet
DrawingView
ViewProjection/SectionDefinition/DetailDefinition
DimensionAnnotation
GD&TAnnotation
Surface/Weld/HoleNote
Balloon
Table/BOMView
TitleBlockInstance
DrawingStyle/StandardProfileRef
```

Generated HLR curves, raster previews, PDF bytes, and DXF geometry are derived artifacts.

### DRW-001 — Revision and configuration association

Every drawing view references an immutable effective source revision/configuration or an explicitly floating source with last-resolved revision. Published/released drawings use immutable sources.

### DRW-002 — Semantic annotation attachment

Dimensions and annotations attach to semantic entities/topology names and store the measurement/annotation intent. Screen coordinates and generated 2D curve indices are layout results, not sole identity.

### DRW-003 — Honest associativity

After source changes, references resolve through persistent topology. Ambiguous/broken attachments are visible and block release according to policy; Kearne never moves a critical dimension to a guessed edge silently.

## 3. View generation

View evaluators consume exact geometry and camera/projection definitions to produce versioned 2D curve artifacts with semantic source mappings. Supported view types are independently gated: base, projected, section, detail, auxiliary, and exploded assembly.

### DRW-004 — HLR backend isolation

OCCT hidden-line removal or another implementation sits behind a drawing-view port. Output curves use Kearne artifact schemas so annotation/layout and export do not depend on live AIS objects.

### DRW-005 — Geometry versus layout

View geometry generation, annotation measurement, and sheet layout are separate evaluators. Moving a view or note does not rerun exact HLR unnecessarily.

## 4. Standards

ISO and ANSI profiles define versioned defaults and permitted forms for projection, units, line styles, dimensions/tolerances, symbols, text, sheet sizes, title blocks, and GD&T syntax.

### DRW-006 — Standards provenance

Standards assets record source, edition, licensing, locale, and Kearne interpretation version. Updating a library does not alter a released drawing or silently rewrite an existing drawing profile.

### DRW-007 — Semantic GD&T

GD&T records are typed structures referencing datum systems and toleranced features. Free text may supplement but is not treated as equivalent validated semantics.

Kearne validates syntax and referential completeness; it does not claim manufacturing/design correctness beyond explicitly implemented rules.

## 5. Dimensions and automatic drawing assistance

Driving model dimensions and measured drawing dimensions are distinct but may be associated. Editing a driving dimension through a drawing uses an ordinary parameter command with revision conflict handling.

Automatic/AI view selection and dimensioning produce editable proposals. They must identify omitted assumptions and never mark a drawing production-ready automatically.

## 6. Export

PDF and DXF export use immutable drawing revisions and atomic output behavior. Export reports fonts, standard profile, source revisions, unresolved/stale annotations, unsupported constructs, and output digest.

Font embedding/substitution and line/text metric behavior are pinned for released artifacts. Visual portability requires a supported font bundle/license strategy.

## 7. Release model

```text
InWork -> Review -> Approved -> Released -> Obsolete
```

Transitions are policy-controlled commands with actor/role, immutable target version, required approvals, diagnostics gate, referenced released dependencies, generated artifacts, and audit evidence.

### DRW-008 — Release immutability

A released record targets immutable revisions and artifact digests. Corrections create new revisions/releases; they do not replace prior released bytes.

### DRW-009 — Release validation

Release policy can require no broken references, no stale views/BOM, pinned external dependencies, approved materials, successful required checks, and complete metadata. Overrides are explicit, permissioned, and audited.

## 8. Verification strategy

- Generate analytic parts and view orientations; verify projected bounds, visibility classes, scale, and semantic curve association.
- Metamorphic tests rotate/translate models and views while preserving expected projection relationships.
- Generate annotation attachment edits through the topology edit matrix.
- Use schema-driven standard profiles to generate valid/invalid annotation structures.
- Compare PDF/DXF parsed semantic structure, dimensions, fonts, pages/layers, and bounds rather than relying mainly on pixels.
- Keep a small raster/perceptual smoke suite for rendering integration.
- Model-based release workflows generate roles, approvals, revisions, stale results, and dependency states to prove invalid transitions cannot occur.

## 9. Open decisions

- **DRW-OPEN-001:** HLR implementation and artifact format.
- **DRW-OPEN-002:** Standards content source and licensing.
- **DRW-OPEN-003:** Font bundle and text-shaping portability.
- **DRW-OPEN-004:** Exact ISO/ANSI/GD&T subset and qualified review.
- **DRW-OPEN-005:** Organization-configurable release policy language.

## 10. Definition of done

A drawing/release capability is implemented when associations survive its declared topology matrix, generated standards/workflow suites pass, exported artifacts are structurally validated on both platforms, and released records remain reproducible and immutable.
