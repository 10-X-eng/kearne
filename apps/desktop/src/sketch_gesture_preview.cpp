#include "sketch_gesture_preview.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace kearne::ui {
namespace {

constexpr double maximumCoordinateMillimeters = 1.0e9;

bool validCoordinate(double value) {
  return std::isfinite(value) &&
         std::abs(value) <= maximumCoordinateMillimeters;
}

} // namespace

SketchGesturePreview::SketchGesturePreview(QObject *parent) : QObject(parent) {}

bool SketchGesturePreview::updateDrag(
    const QString &commandId, qreal firstXMillimeters, qreal firstYMillimeters,
    qreal oppositeXMillimeters, qreal oppositeYMillimeters, bool construction) {
  const std::array<double, 4> values{firstXMillimeters, firstYMillimeters,
                                     oppositeXMillimeters,
                                     oppositeYMillimeters};
  if (commandId != QStringLiteral("sketch.rectangle") ||
      !std::ranges::all_of(values, validCoordinate))
    return false;
  const std::array<QPointF, 4> next{
      QPointF{firstXMillimeters, firstYMillimeters},
      QPointF{oppositeXMillimeters, firstYMillimeters},
      QPointF{oppositeXMillimeters, oppositeYMillimeters},
      QPointF{firstXMillimeters, oppositeYMillimeters},
  };
  if (visible_ && corners_ == next && construction_ == construction)
    return true;
  corners_ = next;
  construction_ = construction;
  visible_ = true;
  emit previewChanged();
  return true;
}

void SketchGesturePreview::clear() {
  if (!visible_)
    return;
  visible_ = false;
  emit previewChanged();
}

} // namespace kearne::ui
