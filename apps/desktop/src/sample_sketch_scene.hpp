#pragma once

#include <kearne/render/sketch_scene.hpp>

#include <memory>

namespace kearne::ui {

[[nodiscard]] Result<std::shared_ptr<const render::SketchSceneSnapshot>>
makeSampleSketchScene();

[[nodiscard]] Result<std::shared_ptr<const render::SketchSceneSnapshot>>
makeSampleSketchScene(render::SceneStamp stamp);

} // namespace kearne::ui
