#pragma once

#include <kearne/render/sketch_scene.hpp>
#include <kearne/sketch/model.hpp>

#include <memory>
#include <span>
#include <stop_token>

namespace kearne::ui {

// Projects evaluated constraint declarations into one immutable native marker
// packet. Callers run this with solving and scene projection, never on the UI
// or render thread.
[[nodiscard]] Result<std::shared_ptr<const render::SketchMarkerPacket>>
projectSketchConstraintMarkers(
    std::span<const sketch::Constraint> constraints,
    std::span<const sketch::ConstraintHealth> health,
    std::shared_ptr<const render::SketchSceneSnapshot> scene,
    render::SketchMarkerGeneration generation,
    render::SketchMarkerLimits limits = {}, std::stop_token cancellation = {});

} // namespace kearne::ui
