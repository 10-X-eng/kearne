#pragma once

#include "sketch_stroke_mesh.hpp"

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
  Glyph = 5,
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
};

struct SketchStrokeSourceBounds {
  render::Point2d minimum;
  render::Point2d maximum;
  bool empty = true;
};

struct SketchStrokeMeshSource {
  std::span<const render::SketchStyle> styles;
  const void *primitiveContext = nullptr;
  std::size_t primitiveCount = 0U;
  SketchStrokeSourcePrimitive (*primitiveAt)(const void *,
                                             std::size_t) noexcept = nullptr;
  SketchStrokeSourceBounds bounds;
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

struct SketchStrokeMeshBuildAccess {
  [[nodiscard]] static Result<SketchStrokeMeshBuildOutput>
  build(const SketchStrokeMeshSource &source, SketchCurveLod lod,
        SketchTessellationOptions tessellation, SketchUploadOptions upload,
        std::shared_ptr<const SketchSceneMesh> reuse,
        std::stop_token cancellation);
};

} // namespace kearne::ui
