#pragma once

#include "sketch_scene_projection.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <stop_token>
#include <vector>

namespace kearne::ui {

struct SketchMarkerAnchorPoint {
  render::Point2d positionMetres;
  bool operator==(const SketchMarkerAnchorPoint &) const = default;
};

struct SketchMarkerRenderRecord {
  render::SketchMarkerHandle handle;
  std::optional<SketchConstraintId> constraint;
  double valueSi = 0.0;
  std::uint32_t firstAnchor = 0;
  std::uint8_t anchorCount = 0;
  render::SketchMarkerKind kind =
      render::SketchMarkerKind::CoincidentConstraint;
  render::SketchMarkerCategory category =
      render::SketchMarkerCategory::Constraint;
  render::SketchMarkerVisualState visual =
      render::SketchMarkerVisualState::Active;
  float screenOffsetXLogicalPixels = 0.0F;
  float screenOffsetYLogicalPixels = 0.0F;
  bool operator==(const SketchMarkerRenderRecord &) const = default;
};

struct SketchMarkerProjectionLimits {
  // Bytes cover adapter-owned packed arrays. Immutable source dependencies,
  // allocator metadata, and shared-pointer control blocks are excluded.
  std::size_t maximumMarkerCount = 1'000'000U;
  std::size_t maximumAnchorCount = 3'000'000U;
  std::size_t maximumRetainedBytes = 128U * 1024U * 1024U;
  std::size_t maximumScratchBytes = 64U * 1024U * 1024U;
  std::size_t maximumPeakBytes = 192U * 1024U * 1024U;
};

struct PreparedSketchMarkerMetrics {
  std::size_t markerCount = 0;
  std::size_t anchorCount = 0;
  std::size_t retainedBytes = 0;
  std::size_t scratchBytes = 0;
  std::size_t peakBytes = 0;
  bool operator==(const PreparedSketchMarkerMetrics &) const = default;
};

class PreparedSketchMarkers final {
public:
  [[nodiscard]] const std::shared_ptr<const render::SketchMarkerPacket> &
  source() const {
    return source_;
  }
  [[nodiscard]] const std::shared_ptr<const PreparedSketchScene> &base() const {
    return base_;
  }
  [[nodiscard]] std::span<const SketchMarkerRenderRecord> markers() const {
    return markers_;
  }
  [[nodiscard]] std::span<const SketchMarkerAnchorPoint> anchors() const {
    return anchors_;
  }
  [[nodiscard]] std::span<const SketchMarkerAnchorPoint>
  markerAnchors(render::SketchMarkerHandle marker) const;
  [[nodiscard]] const SketchMarkerRenderRecord *
  findMarker(render::SketchMarkerHandle marker) const;
  [[nodiscard]] const PreparedSketchMarkerMetrics &metrics() const {
    return metrics_;
  }
  [[nodiscard]] const SketchConstraintDisplay &display() const {
    return display_;
  }

private:
  PreparedSketchMarkers(
      std::shared_ptr<const render::SketchMarkerPacket> source,
      std::shared_ptr<const PreparedSketchScene> base,
      std::vector<SketchMarkerRenderRecord> markers,
      std::vector<SketchMarkerAnchorPoint> anchors,
      PreparedSketchMarkerMetrics metrics, SketchConstraintDisplay display);

  std::shared_ptr<const render::SketchMarkerPacket> source_;
  std::shared_ptr<const PreparedSketchScene> base_;
  std::vector<SketchMarkerRenderRecord> markers_;
  std::vector<SketchMarkerAnchorPoint> anchors_;
  PreparedSketchMarkerMetrics metrics_;
  SketchConstraintDisplay display_;

  friend Result<std::shared_ptr<const PreparedSketchMarkers>>
      prepareSketchMarkers(std::shared_ptr<const render::SketchMarkerPacket>,
                           std::shared_ptr<const PreparedSketchScene>,
                           SketchMarkerProjectionLimits,
                           std::shared_ptr<const PreparedSketchMarkers>,
                           std::stop_token, SketchConstraintDisplay);
};

[[nodiscard]] Result<std::shared_ptr<const PreparedSketchMarkers>>
prepareSketchMarkers(std::shared_ptr<const render::SketchMarkerPacket> source,
                     std::shared_ptr<const PreparedSketchScene> base,
                     SketchMarkerProjectionLimits limits = {},
                     std::shared_ptr<const PreparedSketchMarkers> reuse = {},
                     std::stop_token cancellation = {},
                     SketchConstraintDisplay display = {});

} // namespace kearne::ui
