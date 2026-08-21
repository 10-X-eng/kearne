#pragma once

#include "frontend_contract.hpp"

#include <kearne/render/sketch_scene.hpp>

#include <memory>
#include <span>

namespace kearne::ui {

struct SketchProvisionalProjectionIdentity {
  render::SceneStamp base;
  render::SketchEditSessionHandle editSession;
  render::SketchToolInstanceHandle toolInstance;
  render::SketchProvisionalGeneration generation;
};

[[nodiscard]] Result<std::shared_ptr<const render::SketchProvisionalGeometry>>
projectSketchProvisional(SketchProvisionalProjectionIdentity identity,
                         std::span<const SketchPrimitiveProjection> primitives,
                         render::SketchProvisionalLimits limits = {});

} // namespace kearne::ui
