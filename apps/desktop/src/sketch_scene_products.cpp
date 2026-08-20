#include "sketch_scene_products.hpp"

namespace kearne::ui {

namespace {

[[nodiscard]] bool sameOverlayIdentity(
    const std::shared_ptr<const render::SketchPresentationOverlay> &first,
    const std::shared_ptr<const render::SketchPresentationOverlay> &second) {
  if (!first || !second)
    return first == second;
  return first->base() == second->base() &&
         first->generation() == second->generation() &&
         first->payloadDigest() == second->payloadDigest();
}

[[nodiscard]] bool sameProvisionalIdentity(
    const std::shared_ptr<const render::SketchProvisionalGeometry> &first,
    const std::shared_ptr<const render::SketchProvisionalGeometry> &second) {
  if (!first || !second)
    return first == second;
  return first->stamp() == second->stamp();
}

[[nodiscard]] bool sameMarkerIdentity(
    const std::shared_ptr<const render::SketchMarkerPacket> &first,
    const std::shared_ptr<const render::SketchMarkerPacket> &second) {
  if (!first || !second)
    return first == second;
  return first->base() == second->base() &&
         first->provisional() == second->provisional() &&
         first->stamp() == second->stamp();
}

} // namespace

Result<SketchProductGeneration>
SketchProductGeneration::create(std::uint64_t value) {
  if (value == 0U)
    return std::unexpected(
        diagnostic("desktop.sketch.product-generation-zero",
                   "sketch product generation must be positive"));
  return SketchProductGeneration{value};
}

Result<void> validateSketchSceneProducts(const SketchSceneProducts &products) {
  if (!products.scene)
    return std::unexpected(
        diagnostic("desktop.sketch.products-null-scene",
                   "sketch products require an evaluated scene"));
  if (products.stamp.target != products.scene->stamp().target)
    return std::unexpected(
        diagnostic("desktop.sketch.products-target",
                   "sketch product and evaluated scene targets do not match"));
  if (products.overlay && products.overlay->base() != products.scene)
    return std::unexpected(
        diagnostic("desktop.sketch.products-overlay-base",
                   "sketch overlay does not retain the exact evaluated scene"));
  if (products.provisional &&
      products.provisional->stamp().target.base != products.scene->stamp())
    return std::unexpected(
        diagnostic("desktop.sketch.products-provisional-base",
                   "provisional geometry does not match the evaluated scene"));
  if (products.markers && products.markers->base() != products.scene)
    return std::unexpected(
        diagnostic("desktop.sketch.products-marker-base",
                   "sketch markers do not retain the exact evaluated scene"));
  if (products.markers && products.markers->provisional() &&
      products.markers->provisional() != products.provisional)
    return std::unexpected(diagnostic(
        "desktop.sketch.products-marker-provisional",
        "sketch markers do not retain the exact provisional geometry"));
  if (products.markers && products.provisional) {
    const auto &interaction = products.markers->stamp().target.interaction;
    if (interaction) {
      const auto &target = products.provisional->stamp().target;
      if (interaction->editSession != target.editSession ||
          interaction->toolInstance != target.toolInstance)
        return std::unexpected(diagnostic(
            "desktop.sketch.products-marker-interaction",
            "sketch markers and provisional geometry belong to different "
            "tools"));
    }
  }
  return {};
}

bool sameSketchSceneProductComponents(const SketchSceneProducts &first,
                                      const SketchSceneProducts &second) {
  return first.scene == second.scene &&
         sameOverlayIdentity(first.overlay, second.overlay) &&
         sameProvisionalIdentity(first.provisional, second.provisional) &&
         sameMarkerIdentity(first.markers, second.markers);
}

} // namespace kearne::ui
