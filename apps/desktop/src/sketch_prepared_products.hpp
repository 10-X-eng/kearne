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
  [[nodiscard]] const std::shared_ptr<const SketchVectorPacket> &
  packet() const {
    return packet_;
  }
  [[nodiscard]] std::span<const SketchVectorPrimitiveSpanRecord>
  provenance() const {
    return provenance_;
  }
  [[nodiscard]] const PreparedSketchProvisionalMetrics &metrics() const {
    return metrics_;
  }

private:
  PreparedSketchProvisional(
      std::shared_ptr<const render::SketchProvisionalGeometry> source,
      std::shared_ptr<const SketchVectorPacket> packet,
      std::vector<SketchVectorPrimitiveSpanRecord> provenance,
      PreparedSketchProvisionalMetrics metrics);

  std::shared_ptr<const render::SketchProvisionalGeometry> source_;
  std::shared_ptr<const SketchVectorPacket> packet_;
  std::vector<SketchVectorPrimitiveSpanRecord> provenance_;
  PreparedSketchProvisionalMetrics metrics_;

  friend Result<std::shared_ptr<const PreparedSketchProvisional>>
      prepareSketchProvisional(
          std::shared_ptr<const render::SketchProvisionalGeometry>,
          SketchVectorUploadOptions,
          std::shared_ptr<const PreparedSketchProvisional>, std::stop_token);
};

[[nodiscard]] Result<std::shared_ptr<const PreparedSketchProvisional>>
prepareSketchProvisional(
    std::shared_ptr<const render::SketchProvisionalGeometry> source,
    SketchVectorUploadOptions upload = {},
    std::shared_ptr<const PreparedSketchProvisional> reuse = {},
    std::stop_token cancellation = {});

struct SketchProductPreparationOptions {
  render::SketchPickIndexOptions picking;
  SketchVectorUploadOptions upload;
  SketchOverlayProjectionLimits overlay;
  SketchMarkerProjectionLimits markers;
};

struct PreparedSketchProductsMetrics {
  std::size_t baseRetainedBytes = 0U;
  std::size_t overlayRetainedBytes = 0U;
  std::size_t overlayPointPacketRetainedBytes = 0U;
  std::size_t provisionalRetainedBytes = 0U;
  std::size_t markerRetainedBytes = 0U;
  std::size_t markerPacketRetainedBytes = 0U;
  std::size_t markerProvenanceRetainedBytes = 0U;
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
  [[nodiscard]] const std::shared_ptr<const SketchVectorPacket> &
  overlayPointPacket() const {
    return overlayPointPacket_;
  }
  [[nodiscard]] const std::shared_ptr<const SketchVectorPacket> &
  markerPacket() const {
    return markerPacket_;
  }
  [[nodiscard]] std::span<const SketchVectorPrimitiveSpanRecord>
  markerProvenance() const {
    return markerProvenance_
               ? std::span{*markerProvenance_}
               : std::span<const SketchVectorPrimitiveSpanRecord>{};
  }
  [[nodiscard]] const PreparedSketchProductsMetrics &metrics() const {
    return metrics_;
  }

private:
  [[nodiscard]] static Result<std::shared_ptr<const PreparedSketchProducts>>
  createPrepared(
      std::shared_ptr<const SketchSceneProducts> source,
      std::shared_ptr<const PreparedSketchScene> base,
      std::shared_ptr<const PreparedSketchOverlay> overlay,
      std::shared_ptr<const PreparedSketchProvisional> provisional,
      std::shared_ptr<const PreparedSketchMarkers> markers,
      std::shared_ptr<const SketchVectorPacket> overlayPointPacket,
      std::shared_ptr<const SketchVectorPacket> markerPacket,
      std::shared_ptr<const std::vector<SketchVectorPrimitiveSpanRecord>>
          markerProvenance);

  PreparedSketchProducts(
      std::shared_ptr<const SketchSceneProducts> source,
      std::shared_ptr<const PreparedSketchScene> base,
      std::shared_ptr<const PreparedSketchOverlay> overlay,
      std::shared_ptr<const PreparedSketchProvisional> provisional,
      std::shared_ptr<const PreparedSketchMarkers> markers,
      std::shared_ptr<const SketchVectorPacket> overlayPointPacket,
      std::shared_ptr<const SketchVectorPacket> markerPacket,
      std::shared_ptr<const std::vector<SketchVectorPrimitiveSpanRecord>>
          markerProvenance,
      PreparedSketchProductsMetrics metrics);

  std::shared_ptr<const SketchSceneProducts> source_;
  std::shared_ptr<const PreparedSketchScene> base_;
  std::shared_ptr<const PreparedSketchOverlay> overlay_;
  std::shared_ptr<const PreparedSketchProvisional> provisional_;
  std::shared_ptr<const PreparedSketchMarkers> markers_;
  std::shared_ptr<const SketchVectorPacket> overlayPointPacket_;
  std::shared_ptr<const SketchVectorPacket> markerPacket_;
  std::shared_ptr<const std::vector<SketchVectorPrimitiveSpanRecord>>
      markerProvenance_;
  PreparedSketchProductsMetrics metrics_;

  friend Result<std::shared_ptr<const PreparedSketchProducts>>
      prepareSketchProducts(std::shared_ptr<const SketchSceneProducts>,
                            SketchProductPreparationOptions,
                            std::shared_ptr<const PreparedSketchProducts>,
                            std::stop_token);
};

[[nodiscard]] Result<std::shared_ptr<const PreparedSketchProducts>>
prepareSketchProducts(std::shared_ptr<const SketchSceneProducts> source,
                      SketchProductPreparationOptions options = {},
                      std::shared_ptr<const PreparedSketchProducts> reuse = {},
                      std::stop_token cancellation = {});

} // namespace kearne::ui
