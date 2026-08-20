#pragma once

#include "sketch_scene_projection.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <stop_token>
#include <vector>

namespace kearne::ui {

class PreparedSketchOverlay;

struct SketchOverlayPointInstance {
  render::SketchPrimitiveHandle primitive;
  sketch::PointKey point;
  render::Point2d positionMetres;
  std::uint16_t style = 0;
  bool operator==(const SketchOverlayPointInstance &) const = default;
};

struct SketchOverlayProjectionLimits {
  // Counts and bytes cover adapter-owned packed arrays. The immutable overlay
  // and prepared-base dependencies, stack values, allocator metadata, and
  // shared-pointer control blocks are excluded.
  std::size_t maximumScopeCount = 1'000'000U;
  std::size_t maximumDrawSpanCount = 4'000'000U;
  std::size_t maximumPointInstanceCount = 1'000'000U;
  std::size_t maximumRetainedBytes = 128U * 1024U * 1024U;
  std::size_t maximumScratchBytes = 128U * 1024U * 1024U;
  std::size_t maximumPeakBytes = 256U * 1024U * 1024U;
};

struct PreparedSketchOverlayRoleMetrics {
  std::size_t scopeCount = 0;
  std::size_t entityScopeCount = 0;
  std::size_t pointScopeCount = 0;
  std::size_t retainedBytes = 0;
  std::size_t scratchBytes = 0;
  std::size_t peakBytes = 0;
  bool operator==(const PreparedSketchOverlayRoleMetrics &) const = default;
};

class PreparedSketchOverlayRoleSet final {
public:
  [[nodiscard]] const render::SketchOverlayRoleSetPtr &source() const {
    return source_;
  }
  [[nodiscard]] render::SketchOverlayRole role() const {
    return source_->role();
  }
  [[nodiscard]] std::span<const SketchPrimitiveChunkSpan> drawSpans() const {
    return drawSpans_;
  }
  [[nodiscard]] std::span<const SketchOverlayPointInstance>
  pointInstances() const {
    return pointInstances_;
  }
  [[nodiscard]] const PreparedSketchOverlayRoleMetrics &metrics() const {
    return metrics_;
  }

private:
  PreparedSketchOverlayRoleSet(
      render::SketchOverlayRoleSetPtr source,
      std::vector<SketchPrimitiveChunkSpan> drawSpans,
      std::vector<SketchOverlayPointInstance> pointInstances,
      PreparedSketchOverlayRoleMetrics metrics);

  render::SketchOverlayRoleSetPtr source_;
  std::vector<SketchPrimitiveChunkSpan> drawSpans_;
  std::vector<SketchOverlayPointInstance> pointInstances_;
  PreparedSketchOverlayRoleMetrics metrics_;

  friend class PreparedSketchOverlay;
  friend Result<std::shared_ptr<const PreparedSketchOverlay>>
      prepareSketchOverlay(
          std::shared_ptr<const render::SketchPresentationOverlay>,
          std::shared_ptr<const PreparedSketchScene>,
          SketchOverlayProjectionLimits,
          std::shared_ptr<const PreparedSketchOverlay>, std::stop_token);
};

using PreparedSketchOverlayRoleSetPtr =
    std::shared_ptr<const PreparedSketchOverlayRoleSet>;

struct PreparedSketchOverlayMetrics {
  std::size_t builtRoleSets = 0;
  std::size_t reusedRoleSets = 0;
  std::size_t builtScopes = 0;
  std::size_t scopeCount = 0;
  std::size_t drawSpanCount = 0;
  std::size_t pointInstanceCount = 0;
  std::size_t retainedBytes = 0;
  std::size_t scratchBytes = 0;
  std::size_t peakBytes = 0;
  bool operator==(const PreparedSketchOverlayMetrics &) const = default;
};

class PreparedSketchOverlay final {
public:
  [[nodiscard]] const std::shared_ptr<const render::SketchPresentationOverlay> &
  source() const {
    return source_;
  }
  [[nodiscard]] const std::shared_ptr<const PreparedSketchScene> &base() const {
    return base_;
  }
  [[nodiscard]]
  std::span<const PreparedSketchOverlayRoleSetPtr, 4> roleSets() const {
    return roleSets_;
  }
  [[nodiscard]] PreparedSketchOverlayRoleSetPtr
  roleSet(render::SketchOverlayRole role) const;
  [[nodiscard]] const PreparedSketchOverlayMetrics &metrics() const {
    return metrics_;
  }

private:
  PreparedSketchOverlay(
      std::shared_ptr<const render::SketchPresentationOverlay> source,
      std::shared_ptr<const PreparedSketchScene> base,
      std::array<PreparedSketchOverlayRoleSetPtr, 4> roleSets,
      PreparedSketchOverlayMetrics metrics);

  std::shared_ptr<const render::SketchPresentationOverlay> source_;
  std::shared_ptr<const PreparedSketchScene> base_;
  std::array<PreparedSketchOverlayRoleSetPtr, 4> roleSets_;
  PreparedSketchOverlayMetrics metrics_;

  friend Result<std::shared_ptr<const PreparedSketchOverlay>>
      prepareSketchOverlay(
          std::shared_ptr<const render::SketchPresentationOverlay>,
          std::shared_ptr<const PreparedSketchScene>,
          SketchOverlayProjectionLimits,
          std::shared_ptr<const PreparedSketchOverlay>, std::stop_token);
};

[[nodiscard]] Result<std::shared_ptr<const PreparedSketchOverlay>>
prepareSketchOverlay(
    std::shared_ptr<const render::SketchPresentationOverlay> overlay,
    std::shared_ptr<const PreparedSketchScene> base,
    SketchOverlayProjectionLimits limits = {},
    std::shared_ptr<const PreparedSketchOverlay> reuse = {},
    std::stop_token cancellation = {});

} // namespace kearne::ui
