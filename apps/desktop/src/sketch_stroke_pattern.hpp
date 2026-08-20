#pragma once

#include <kearne/render/sketch_scene.hpp>

#include <algorithm>
#include <limits>

namespace kearne::ui {

struct SketchStrokePattern {
  float onLogicalPixels = 0.0F;
  float periodLogicalPixels = 0.0F;
  bool operator==(const SketchStrokePattern &) const = default;
};

[[nodiscard]] inline SketchStrokePattern
strokePattern(const render::SketchStyle &style) noexcept {
  const float width = style.strokeWidthPixels;
  const auto bounded = [](float on, float gap) {
    constexpr float maximum = std::numeric_limits<float>::max();
    return SketchStrokePattern{on, on > maximum - gap ? maximum : on + gap};
  };
  const auto scaled = [](float value, float factor) {
    constexpr float maximum = std::numeric_limits<float>::max();
    return value > maximum / factor ? maximum : value * factor;
  };
  switch (style.linePattern) {
  case render::SketchLinePattern::Solid:
    return {};
  case render::SketchLinePattern::Dashed: {
    const float on = std::max(6.0F, scaled(width, 3.0F));
    return bounded(on, std::max(4.0F, scaled(width, 2.0F)));
  }
  case render::SketchLinePattern::Dotted: {
    const float on = std::max(1.5F, width);
    return bounded(on, std::max(3.0F, scaled(width, 2.0F)));
  }
  }
  return {};
}

} // namespace kearne::ui
