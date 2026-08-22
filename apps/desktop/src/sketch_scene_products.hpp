#pragma once

#include <kearne/base/value.hpp>
#include <kearne/render/sketch_scene.hpp>

#include <compare>
#include <cstdint>
#include <memory>
#include <utility>

namespace kearne::ui {

enum class SketchLengthDisplayUnit : std::uint8_t {
  Millimeter = 1,
  Centimeter = 2,
  Meter = 3,
  Inch = 4,
};

struct SketchConstraintDisplay {
  SketchLengthDisplayUnit lengthUnit = SketchLengthDisplayUnit::Millimeter;
  bool constraintsVisible = true;
  bool dimensionsVisible = true;
  bool referenceDimensionsVisible = true;
  bool operator==(const SketchConstraintDisplay &) const = default;
};

struct SketchMarkerEmphasis {
  std::uint32_t selectedMarker = 0U;
  std::uint32_t hoveredMarker = 0U;
  bool operator==(const SketchMarkerEmphasis &) const = default;
};

struct SketchProductDigestTag;
using SketchProductDigest = TypedDigest<SketchProductDigestTag>;

class SketchProductGeneration final {
public:
  [[nodiscard]] static Result<SketchProductGeneration>
  create(std::uint64_t value);
  [[nodiscard]] std::uint64_t value() const { return value_; }
  auto operator<=>(const SketchProductGeneration &) const = default;

private:
  explicit SketchProductGeneration(std::uint64_t value) : value_(value) {}
  std::uint64_t value_;
};

// One identity covers the complete product packet, including absent products.
// Generations are monotonic across every target in one controller/session.
struct SketchProductStamp {
  render::SceneTarget target;
  SketchProductGeneration generation;
  SketchProductDigest digest;
  bool operator==(const SketchProductStamp &) const = default;
};

struct SketchSceneProducts {
  SketchSceneProducts(
      SketchProductStamp requestedStamp,
      std::shared_ptr<const render::SketchSceneSnapshot> requestedScene,
      std::shared_ptr<const render::SketchPresentationOverlay>
          requestedOverlay = {},
      std::shared_ptr<const render::SketchProvisionalGeometry>
          requestedProvisional = {},
      std::shared_ptr<const render::SketchMarkerPacket> requestedMarkers = {},
      SketchConstraintDisplay requestedConstraintDisplay = {},
      SketchMarkerEmphasis requestedMarkerEmphasis = {})
      : stamp(std::move(requestedStamp)), scene(std::move(requestedScene)),
        overlay(std::move(requestedOverlay)),
        provisional(std::move(requestedProvisional)),
        markers(std::move(requestedMarkers)),
        constraintDisplay(requestedConstraintDisplay),
        markerEmphasis(requestedMarkerEmphasis) {}

  SketchProductStamp stamp;
  std::shared_ptr<const render::SketchSceneSnapshot> scene;
  std::shared_ptr<const render::SketchPresentationOverlay> overlay;
  std::shared_ptr<const render::SketchProvisionalGeometry> provisional;
  std::shared_ptr<const render::SketchMarkerPacket> markers;
  SketchConstraintDisplay constraintDisplay;
  SketchMarkerEmphasis markerEmphasis;
};

[[nodiscard]] Result<void>
validateSketchSceneProducts(const SketchSceneProducts &products);

[[nodiscard]] bool
sameSketchSceneProductComponents(const SketchSceneProducts &first,
                                 const SketchSceneProducts &second);

} // namespace kearne::ui
