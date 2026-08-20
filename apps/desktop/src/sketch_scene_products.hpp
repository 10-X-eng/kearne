#pragma once

#include <kearne/base/value.hpp>
#include <kearne/render/sketch_scene.hpp>

#include <compare>
#include <cstdint>
#include <memory>

namespace kearne::ui {

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
  SketchProductStamp stamp;
  std::shared_ptr<const render::SketchSceneSnapshot> scene;
  std::shared_ptr<const render::SketchPresentationOverlay> overlay;
  std::shared_ptr<const render::SketchProvisionalGeometry> provisional;
  std::shared_ptr<const render::SketchMarkerPacket> markers;
};

[[nodiscard]] Result<void>
validateSketchSceneProducts(const SketchSceneProducts &products);

[[nodiscard]] bool
sameSketchSceneProductComponents(const SketchSceneProducts &first,
                                 const SketchSceneProducts &second);

} // namespace kearne::ui
