#pragma once

#include "sketch_marker_projection.hpp"
#include "sketch_overlay_projection.hpp"
#include "sketch_scene_products.hpp"

#include <cstddef>
#include <memory>
#include <span>
#include <stop_token>
#include <vector>

namespace kearne::ui {

struct PreparedSketchProvisionalMetrics {
  std::size_t provenanceCount = 0U;
  std::size_t retainedBytes = 0U;
  std::size_t scratchBytes = 0U;
  std::size_t peakBytes = 0U;
  bool operator==(const PreparedSketchProvisionalMetrics &) const = default;
};

class PreparedSketchProvisional final {
public:
  [[nodiscard]]
  const std::shared_ptr<const render::SketchProvisionalGeometry> &
  source() const {
    return source_;
  }
  [[nodiscard]] const std::shared_ptr<const SketchSceneMesh> &mesh() const {
    return mesh_;
  }
  [[nodiscard]] std::span<const SketchStrokePrimitiveSpanRecord>
  provenance() const {
    return provenance_;
  }
  [[nodiscard]] SketchCurveLod lod() const { return lod_; }
  [[nodiscard]] const PreparedSketchProvisionalMetrics &metrics() const {
    return metrics_;
  }

private:
  PreparedSketchProvisional(
      std::shared_ptr<const render::SketchProvisionalGeometry> source,
      std::shared_ptr<const SketchSceneMesh> mesh,
      std::vector<SketchStrokePrimitiveSpanRecord> provenance,
      SketchCurveLod lod, PreparedSketchProvisionalMetrics metrics);

  std::shared_ptr<const render::SketchProvisionalGeometry> source_;
  std::shared_ptr<const SketchSceneMesh> mesh_;
  std::vector<SketchStrokePrimitiveSpanRecord> provenance_;
  SketchCurveLod lod_;
  PreparedSketchProvisionalMetrics metrics_;

  friend Result<std::shared_ptr<const PreparedSketchProvisional>>
      prepareSketchProvisional(
          std::shared_ptr<const render::SketchProvisionalGeometry>,
          SketchCurveLod, SketchTessellationOptions, SketchUploadOptions,
          std::shared_ptr<const PreparedSketchProvisional>, std::stop_token);
};

[[nodiscard]] Result<std::shared_ptr<const PreparedSketchProvisional>>
prepareSketchProvisional(
    std::shared_ptr<const render::SketchProvisionalGeometry> source,
    SketchCurveLod lod = {}, SketchTessellationOptions tessellation = {},
    SketchUploadOptions upload = {},
    std::shared_ptr<const PreparedSketchProvisional> reuse = {},
    std::stop_token cancellation = {});

struct SketchProductPreparationOptions {
  SketchTessellationOptions tessellation;
  render::SketchPickIndexOptions picking;
  SketchUploadOptions upload;
  SketchOverlayProjectionLimits overlay;
  SketchMarkerProjectionLimits markers;
};

struct PreparedSketchProductsMetrics {
  std::size_t baseRetainedBytes = 0U;
  std::size_t overlayRetainedBytes = 0U;
  std::size_t overlayPointMeshRetainedBytes = 0U;
  std::size_t provisionalRetainedBytes = 0U;
  std::size_t markerRetainedBytes = 0U;
  std::size_t markerMeshRetainedBytes = 0U;
  std::size_t totalRetainedBytes = 0U;
  bool operator==(const PreparedSketchProductsMetrics &) const = default;
};

class PreparedSketchProducts final {
public:
  [[nodiscard]] static Result<std::shared_ptr<const PreparedSketchProducts>>
  create(std::shared_ptr<const SketchSceneProducts> source,
         std::shared_ptr<const PreparedSketchScene> base,
         std::shared_ptr<const PreparedSketchOverlay> overlay = {},
         std::shared_ptr<const PreparedSketchProvisional> provisional = {},
         std::shared_ptr<const PreparedSketchMarkers> markers = {});

  [[nodiscard]] const std::shared_ptr<const SketchSceneProducts> &
  source() const {
    return source_;
  }
  [[nodiscard]] const SketchProductStamp &stamp() const {
    return source_->stamp;
  }
  [[nodiscard]] const std::shared_ptr<const PreparedSketchScene> &base() const {
    return base_;
  }
  [[nodiscard]] const std::shared_ptr<const PreparedSketchOverlay> &
  overlay() const {
    return overlay_;
  }
  [[nodiscard]]
  const std::shared_ptr<const PreparedSketchProvisional> &provisional() const {
    return provisional_;
  }
  [[nodiscard]] const std::shared_ptr<const PreparedSketchMarkers> &
  markers() const {
    return markers_;
  }
  [[nodiscard]] const std::shared_ptr<const SketchSceneMesh> &
  overlayPointMesh() const {
    return overlayPointMesh_;
  }
  [[nodiscard]] const std::shared_ptr<const SketchSceneMesh> &
  markerMesh() const {
    return markerMesh_;
  }
  [[nodiscard]] SketchCurveLod lod() const { return base_->lod(); }
  [[nodiscard]] const PreparedSketchProductsMetrics &metrics() const {
    return metrics_;
  }

private:
  [[nodiscard]] static Result<std::shared_ptr<const PreparedSketchProducts>>
  createPrepared(std::shared_ptr<const SketchSceneProducts> source,
                 std::shared_ptr<const PreparedSketchScene> base,
                 std::shared_ptr<const PreparedSketchOverlay> overlay,
                 std::shared_ptr<const PreparedSketchProvisional> provisional,
                 std::shared_ptr<const PreparedSketchMarkers> markers,
                 std::shared_ptr<const SketchSceneMesh> overlayPointMesh,
                 std::shared_ptr<const SketchSceneMesh> markerMesh);

  PreparedSketchProducts(
      std::shared_ptr<const SketchSceneProducts> source,
      std::shared_ptr<const PreparedSketchScene> base,
      std::shared_ptr<const PreparedSketchOverlay> overlay,
      std::shared_ptr<const PreparedSketchProvisional> provisional,
      std::shared_ptr<const PreparedSketchMarkers> markers,
      std::shared_ptr<const SketchSceneMesh> overlayPointMesh,
      std::shared_ptr<const SketchSceneMesh> markerMesh,
      PreparedSketchProductsMetrics metrics);

  std::shared_ptr<const SketchSceneProducts> source_;
  std::shared_ptr<const PreparedSketchScene> base_;
  std::shared_ptr<const PreparedSketchOverlay> overlay_;
  std::shared_ptr<const PreparedSketchProvisional> provisional_;
  std::shared_ptr<const PreparedSketchMarkers> markers_;
  std::shared_ptr<const SketchSceneMesh> overlayPointMesh_;
  std::shared_ptr<const SketchSceneMesh> markerMesh_;
  PreparedSketchProductsMetrics metrics_;

  friend Result<std::shared_ptr<const PreparedSketchProducts>>
      prepareSketchProducts(std::shared_ptr<const SketchSceneProducts>,
                            SketchCurveLod, SketchProductPreparationOptions,
                            std::shared_ptr<const PreparedSketchProducts>,
                            std::stop_token);
};

[[nodiscard]] Result<std::shared_ptr<const PreparedSketchProducts>>
prepareSketchProducts(std::shared_ptr<const SketchSceneProducts> source,
                      SketchCurveLod lod = {},
                      SketchProductPreparationOptions options = {},
                      std::shared_ptr<const PreparedSketchProducts> reuse = {},
                      std::stop_token cancellation = {});

} // namespace kearne::ui
