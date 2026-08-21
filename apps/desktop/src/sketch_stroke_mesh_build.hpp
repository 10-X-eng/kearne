#pragma once

#include "sketch_stroke_mesh.hpp"

#include <kearne/sketch/nurbs.hpp>

#include <stop_token>
#include <vector>

namespace kearne::ui {

struct SketchMeshBatch {
  std::uint16_t style = 0;
  std::uint16_t layer = 0;
  std::vector<SketchMeshVertex> vertices;
  std::vector<std::uint32_t> indices;
  std::vector<std::uint32_t> triangleSources;
  std::vector<double> triangleAnalyticDeviationsMetres;
};

enum class SketchStrokeSourceKind : std::uint8_t {
  Point = 1,
  Line = 2,
  Circle = 3,
  Arc = 4,
  Ellipse = 5,
  EllipticalArc = 6,
  HyperbolicArc = 7,
  ParabolicArc = 8,
  BSpline = 9,
  Glyph = 10,
};

struct SketchStrokeSourcePrimitive {
  std::uint32_t sourceKey = 0;
  std::uint16_t style = 0;
  SketchStrokeSourceKind kind = SketchStrokeSourceKind::Point;
  bool visible = false;
  render::Point2d first;
  render::Point2d second;
  double radius = 0.0;
  double startAngleRadians = 0.0;
  double sweepAngleRadians = 0.0;
  std::uint16_t glyph = 0U;
  double secondaryRadius = 0.0;
  double rotationAngleRadians = 0.0;
};

struct SketchStrokeSourceBounds {
  render::Point2d minimum;
  render::Point2d maximum;
  bool empty = true;
};

struct SketchStrokeMeshSource {
  using PrimitiveAt = SketchStrokeSourcePrimitive (*)(const void *,
                                                      std::size_t) noexcept;
  using SplineAt = sketch::NurbsView (*)(const void *, std::size_t) noexcept;

  std::span<const render::SketchStyle> styles;
  const void *primitiveContext = nullptr;
  std::size_t primitiveCount = 0U;
  PrimitiveAt primitiveAt = nullptr;
  SketchStrokeSourceBounds bounds;
  SplineAt splineAt = nullptr;

  SketchStrokeMeshSource(std::span<const render::SketchStyle> requestedStyles,
                         const void *requestedContext,
                         std::size_t requestedCount,
                         PrimitiveAt requestedPrimitiveAt,
                         SketchStrokeSourceBounds requestedBounds,
                         SplineAt requestedSplineAt = nullptr)
      : styles(requestedStyles), primitiveContext(requestedContext),
        primitiveCount(requestedCount), primitiveAt(requestedPrimitiveAt),
        bounds(requestedBounds), splineAt(requestedSplineAt) {}
};

struct SketchStrokeMeshBuildOutput {
  SketchSceneMesh mesh;
  std::vector<SketchStrokePrimitiveSpanRecord> provenance;
  std::size_t sourceProvenanceEntries = 0U;
  std::size_t sourceProvenanceSpans = 0U;
  std::size_t sourceProvenanceBytes = 0U;
  std::size_t retainedOutputBytes = 0U;
  std::size_t scratchBytes = 0U;
  std::size_t peakBytes = 0U;
};

[[nodiscard]] Result<SketchStrokeSourceBounds>
sketchStrokePrimitiveBounds(const SketchStrokeSourcePrimitive &primitive);
[[nodiscard]] Result<SketchStrokeSourceBounds>
sketchStrokePrimitiveBounds(const SketchStrokeSourcePrimitive &primitive,
                            sketch::NurbsView spline);

struct SketchStrokeMeshBuildAccess {
  [[nodiscard]] static Result<SketchStrokeMeshBuildOutput>
  build(const SketchStrokeMeshSource &source, SketchCurveLod lod,
        SketchTessellationOptions tessellation, SketchUploadOptions upload,
        std::shared_ptr<const SketchSceneMesh> reuse,
        std::stop_token cancellation);
};

} // namespace kearne::ui
