#include "sketch_camera_controller.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numbers>

namespace kearne::ui {
namespace {

constexpr double minimumScale = 1.0e-9;
constexpr double maximumScale = 1.0e3;
constexpr double maximumCoordinate = 1.0e6;
constexpr double maximumCenterUlpLogicalPixels = 1.0 / 256.0;

bool finite(QPointF point) {
  return std::isfinite(point.x()) && std::isfinite(point.y());
}

bool validViewport(QSizeF viewport) {
  constexpr double maximumLogicalDimension =
      static_cast<double>(std::numeric_limits<float>::max());
  return std::isfinite(viewport.width()) && std::isfinite(viewport.height()) &&
         viewport.width() > 0.0 && viewport.height() > 0.0 &&
         viewport.width() <= maximumLogicalDimension &&
         viewport.height() <= maximumLogicalDimension;
}

double coordinateUlp(double coordinate) {
  const double upward =
      std::nextafter(coordinate, std::numeric_limits<double>::infinity()) -
      coordinate;
  const double downward =
      coordinate -
      std::nextafter(coordinate, -std::numeric_limits<double>::infinity());
  return std::max(upward, downward);
}

bool preciseCenter(render::Point2d centerMetres, double metresPerLogicalPixel) {
  return coordinateUlp(centerMetres.x) / metresPerLogicalPixel <=
             maximumCenterUlpLogicalPixels &&
         coordinateUlp(centerMetres.y) / metresPerLogicalPixel <=
             maximumCenterUlpLogicalPixels;
}

render::Point2d canonicalItemDelta(const SketchCamera2d &camera,
                                   double deltaXLogicalPixels,
                                   double deltaYLogicalPixels,
                                   double metresPerLogicalPixel) {
  const double cosine = std::cos(camera.rotationRadians);
  const double sine = std::sin(camera.rotationRadians);
  return {(cosine * deltaXLogicalPixels - sine * deltaYLogicalPixels) *
              metresPerLogicalPixel,
          (-sine * deltaXLogicalPixels - cosine * deltaYLogicalPixels) *
              metresPerLogicalPixel};
}

double wrappedRadians(double radians) {
  radians = std::remainder(radians, 2.0 * std::numbers::pi);
  return radians == -std::numbers::pi ? std::numbers::pi : radians;
}

} // namespace

SketchCameraController::SketchCameraController(QObject *parent)
    : QObject(parent) {
  // Generation one belongs to the native presenter's valid fallback camera.
  // The controller is authoritative and must supersede that fallback on its
  // first publication.
  camera_.generation = 2U;
  camera_.metresPerLogicalPixel = 0.0005;
}

qulonglong SketchCameraController::generation() const {
  return camera_.generation;
}

QPointF SketchCameraController::centerMetres() const {
  return {camera_.centerMetres.x, camera_.centerMetres.y};
}

qreal SketchCameraController::metresPerLogicalPixel() const {
  return camera_.metresPerLogicalPixel;
}

qreal SketchCameraController::pixelsPerMetre() const {
  return 1.0 / camera_.metresPerLogicalPixel;
}

qreal SketchCameraController::rotationRadians() const {
  return camera_.rotationRadians;
}

SketchCamera2d SketchCameraController::camera() const { return camera_; }

bool SketchCameraController::pan(qreal deltaXLogicalPixels,
                                 qreal deltaYLogicalPixels) {
  if (!std::isfinite(deltaXLogicalPixels) ||
      !std::isfinite(deltaYLogicalPixels))
    return false;
  const render::Point2d delta =
      canonicalItemDelta(camera_, deltaXLogicalPixels, deltaYLogicalPixels,
                         camera_.metresPerLogicalPixel);
  return replace(
      {camera_.centerMetres.x - delta.x, camera_.centerMetres.y - delta.y},
      camera_.metresPerLogicalPixel, camera_.rotationRadians);
}

bool SketchCameraController::zoomAt(qreal wheelSteps,
                                    qreal anchorXLogicalPixels,
                                    qreal anchorYLogicalPixels,
                                    qreal viewportWidthLogicalPixels,
                                    qreal viewportHeightLogicalPixels) {
  const QPointF anchor{anchorXLogicalPixels, anchorYLogicalPixels};
  const QSizeF viewport{viewportWidthLogicalPixels,
                        viewportHeightLogicalPixels};
  if (!std::isfinite(wheelSteps) || !finite(anchor) || !validViewport(viewport))
    return false;
  const double nextScale =
      std::clamp(camera_.metresPerLogicalPixel * std::exp(-wheelSteps * 0.18),
                 minimumScale, maximumScale);
  const render::Point2d anchorDelta =
      canonicalItemDelta(camera_, anchor.x() - viewport.width() * 0.5,
                         anchor.y() - viewport.height() * 0.5,
                         camera_.metresPerLogicalPixel - nextScale);
  return replace({camera_.centerMetres.x + anchorDelta.x,
                  camera_.centerMetres.y + anchorDelta.y},
                 nextScale, camera_.rotationRadians);
}

bool SketchCameraController::rotate(qreal deltaRadians) {
  if (!std::isfinite(deltaRadians))
    return false;
  return replace(camera_.centerMetres, camera_.metresPerLogicalPixel,
                 wrappedRadians(camera_.rotationRadians + deltaRadians));
}

void SketchCameraController::reset() {
  static_cast<void>(replace({}, 0.0005, 0.0));
}

bool SketchCameraController::fit(render::Bounds2d bounds,
                                 QSizeF viewportLogicalPixels,
                                 qreal marginLogicalPixels) {
  if (bounds.empty || !validViewport(viewportLogicalPixels) ||
      !std::isfinite(marginLogicalPixels) || marginLogicalPixels < 0.0 ||
      marginLogicalPixels * 2.0 >= viewportLogicalPixels.width() ||
      marginLogicalPixels * 2.0 >= viewportLogicalPixels.height())
    return false;
  const std::array<double, 4> values{bounds.minimum.x, bounds.minimum.y,
                                     bounds.maximum.x, bounds.maximum.y};
  if (std::ranges::any_of(values,
                          [](double value) { return !std::isfinite(value); }) ||
      bounds.maximum.x < bounds.minimum.x ||
      bounds.maximum.y < bounds.minimum.y)
    return false;
  const render::Point2d center{bounds.minimum.x * 0.5 + bounds.maximum.x * 0.5,
                               bounds.minimum.y * 0.5 + bounds.maximum.y * 0.5};
  const double cosine = std::cos(camera_.rotationRadians);
  const double sine = std::sin(camera_.rotationRadians);
  const double width = bounds.maximum.x - bounds.minimum.x;
  const double height = bounds.maximum.y - bounds.minimum.y;
  const double rotatedWidth =
      std::abs(cosine) * width + std::abs(sine) * height;
  const double rotatedHeight =
      std::abs(sine) * width + std::abs(cosine) * height;
  const double availableWidth =
      viewportLogicalPixels.width() - 2.0 * marginLogicalPixels;
  const double availableHeight =
      viewportLogicalPixels.height() - 2.0 * marginLogicalPixels;
  const double requiredScale =
      std::max(rotatedWidth / availableWidth, rotatedHeight / availableHeight);
  if (!std::isfinite(width) || !std::isfinite(height) ||
      !std::isfinite(rotatedWidth) || !std::isfinite(rotatedHeight) ||
      !std::isfinite(requiredScale) || requiredScale > maximumScale)
    return false;
  const double scale = std::max(requiredScale, minimumScale);
  return replace(center, scale, camera_.rotationRadians);
}

bool SketchCameraController::replace(render::Point2d centerMetres,
                                     double metresPerLogicalPixel,
                                     double rotationRadians) {
  if (camera_.generation == std::numeric_limits<std::uint64_t>::max() ||
      !std::isfinite(centerMetres.x) || !std::isfinite(centerMetres.y) ||
      std::abs(centerMetres.x) > maximumCoordinate ||
      std::abs(centerMetres.y) > maximumCoordinate ||
      !std::isfinite(metresPerLogicalPixel) ||
      metresPerLogicalPixel < minimumScale ||
      metresPerLogicalPixel > maximumScale ||
      !preciseCenter(centerMetres, metresPerLogicalPixel) ||
      !std::isfinite(rotationRadians))
    return false;
  rotationRadians = wrappedRadians(rotationRadians);
  if (camera_.centerMetres == centerMetres &&
      camera_.metresPerLogicalPixel == metresPerLogicalPixel &&
      camera_.rotationRadians == rotationRadians)
    return false;
  camera_.centerMetres = centerMetres;
  camera_.metresPerLogicalPixel = metresPerLogicalPixel;
  camera_.rotationRadians = rotationRadians;
  ++camera_.generation;
  emit cameraChanged();
  return true;
}

} // namespace kearne::ui
