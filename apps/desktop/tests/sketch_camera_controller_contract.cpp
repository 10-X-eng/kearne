#include "sketch_camera_controller.hpp"

#include <kearne/testkit/property.hpp>

#include <QCoreApplication>

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <limits>
#include <numbers>
#include <sstream>
#include <stdexcept>

namespace {

using namespace kearne;
using namespace kearne::ui;

void require(bool condition, const char *message) {
  if (!condition)
    throw std::runtime_error(message);
}

bool close(double first, double second, double absoluteTolerance = 1.0e-10) {
  const double roundoff = 16.0 * std::numeric_limits<double>::epsilon() *
                          std::max(std::abs(first), std::abs(second));
  return std::abs(first - second) <= std::max(absoluteTolerance, roundoff);
}

double coordinateUlp(double coordinate) {
  return std::max(
      std::nextafter(coordinate, std::numeric_limits<double>::infinity()) -
          coordinate,
      coordinate -
          std::nextafter(coordinate, -std::numeric_limits<double>::infinity()));
}

double centerUlpLogicalPixels(const SketchCamera2d &camera) {
  return std::max(coordinateUlp(camera.centerMetres.x),
                  coordinateUlp(camera.centerMetres.y)) /
         camera.metresPerLogicalPixel;
}

double panRoundoffBound(const SketchCamera2d &before,
                        const SketchCamera2d &after,
                        render::Point2d trackedPoint, QSizeF viewport) {
  const double coordinateRoundoff = coordinateUlp(before.centerMetres.x) +
                                    coordinateUlp(before.centerMetres.y) +
                                    coordinateUlp(after.centerMetres.x) +
                                    coordinateUlp(after.centerMetres.y) +
                                    coordinateUlp(trackedPoint.x) +
                                    coordinateUlp(trackedPoint.y);
  const double arithmeticRoundoff =
      64.0 * std::numeric_limits<double>::epsilon() *
      std::max(viewport.width(), viewport.height());
  return 4.0 * coordinateRoundoff / before.metresPerLogicalPixel +
         arithmeticRoundoff;
}

void requireConditioned(const SketchCamera2d &camera) {
  require(centerUlpLogicalPixels(camera) <= 1.0 / 256.0,
          "accepted sketch camera exceeded its coordinate precision ceiling");
}

void verifyGeneratedCamera(const testkit::PropertyProfile &profile) {
  testkit::checkProperty(
      "sketch camera pointer invariants", profile,
      [](testkit::Random &random, std::uint64_t) {
        SketchCameraController camera;
        const QSizeF viewport{random.between(64.0, 8'192.0),
                              random.between(64.0, 8'192.0)};
        const double initialRotation =
            random.between(0.01, std::numbers::pi) *
            ((random.next() & 1U) == 0U ? -1.0 : 1.0);
        require(camera.rotate(initialRotation),
                "valid initial sketch rotation was rejected");
        for (std::size_t step = 0; step < 64U; ++step) {
          const QPointF point{random.between(0.0, viewport.width()),
                              random.between(0.0, viewport.height())};
          const auto beforeCamera = camera.camera();
          auto before = SketchViewTransform::create(beforeCamera, viewport);
          require(before.has_value(), "valid camera transform was rejected");
          const render::Point2d fixed = before->toCanonical(point);
          const double wheel = random.between(-4.0, 4.0);
          const bool zoomed = camera.zoomAt(
              wheel, point.x(), point.y(), viewport.width(), viewport.height());
          require(zoomed || camera.camera() == beforeCamera,
                  "valid anchored sketch zoom was rejected");
          requireConditioned(camera.camera());
          auto after = SketchViewTransform::create(camera.camera(), viewport);
          require(after.has_value(), "zoom produced an invalid transform");
          const render::Point2d preserved = after->toCanonical(point);
          require(close(fixed.x, preserved.x) && close(fixed.y, preserved.y),
                  "anchored sketch zoom moved the canonical pointer point");

          const double dx = random.between(-200.0, 200.0);
          const double dy = random.between(-200.0, 200.0);
          const QPointF screenBefore = after->toItem(fixed);
          const auto beforePan = camera.camera();
          const double cosine = std::cos(beforePan.rotationRadians);
          const double sine = std::sin(beforePan.rotationRadians);
          const render::Point2d expectedCenter{
              beforePan.centerMetres.x -
                  (cosine * dx - sine * dy) * beforePan.metresPerLogicalPixel,
              beforePan.centerMetres.y -
                  (-sine * dx - cosine * dy) * beforePan.metresPerLogicalPixel};
          const bool pannedCamera = camera.pan(dx, dy);
          if (!pannedCamera) {
            require(camera.camera() == beforePan,
                    "bounded sketch pan partially changed the camera");
            camera.reset();
            continue;
          }
          auto panned = SketchViewTransform::create(camera.camera(), viewport);
          require(panned.has_value(), "pan produced an invalid transform");
          const QPointF screenAfter = panned->toItem(fixed);
          const double actualX = screenAfter.x() - screenBefore.x();
          const double actualY = screenAfter.y() - screenBefore.y();
          const double roundoff =
              panRoundoffBound(beforePan, camera.camera(), fixed, viewport);
          require(roundoff <= 0.125,
                  "accepted sketch camera permits visible pan jitter");
          if (std::abs(actualX - dx) > roundoff ||
              std::abs(actualY - dy) > roundoff) {
            std::ostringstream failure;
            failure << std::setprecision(17)
                    << "sketch pan delta expected=" << dx << ',' << dy
                    << " actual=" << actualX << ',' << actualY
                    << " scale=" << beforePan.metresPerLogicalPixel
                    << " center=" << beforePan.centerMetres.x << ','
                    << beforePan.centerMetres.y << " bound=" << roundoff;
            throw std::runtime_error(failure.str());
          }
          require(close(camera.camera().centerMetres.x, expectedCenter.x) &&
                      close(camera.camera().centerMetres.y, expectedCenter.y),
                  "sketch pan disagrees with the canonical view transform");
          requireConditioned(camera.camera());

          if ((step & 7U) == 7U) {
            const double rotationDelta =
                random.between(0.001, 0.25) *
                ((random.next() & 1U) == 0U ? -1.0 : 1.0);
            require(camera.rotate(rotationDelta),
                    "valid incremental sketch rotation was rejected");
          }
        }
      });
}

void verifyGeneratedPrecisionBoundary(const testkit::PropertyProfile &profile) {
  testkit::checkProperty(
      "sketch camera precision boundary", profile,
      [](testkit::Random &random, std::uint64_t) {
        SketchCameraController camera;
        const double initialCenter = random.between(300'000.0, 500'000.0);
        require(
            camera.pan(-initialCenter / camera.metresPerLogicalPixel(), 0.0),
            "valid large sketch translation was rejected");

        const double acceptedScale =
            coordinateUlp(camera.camera().centerMetres.x) *
            random.between(272.0, 480.0);
        const double acceptedWheel =
            -std::log(acceptedScale / camera.metresPerLogicalPixel()) / 0.18;
        require(camera.zoomAt(acceptedWheel, 500.0, 400.0, 1'000.0, 800.0),
                "well-conditioned boundary zoom was rejected");
        requireConditioned(camera.camera());

        const auto beforeRejectedZoom = camera.camera();
        const double rejectedScale =
            coordinateUlp(beforeRejectedZoom.centerMetres.x) *
            random.between(64.0, 240.0);
        const double rejectedWheel =
            -std::log(rejectedScale /
                      beforeRejectedZoom.metresPerLogicalPixel) /
            0.18;
        require(!camera.zoomAt(rejectedWheel, 500.0, 400.0, 1'000.0, 800.0) &&
                    camera.camera() == beforeRejectedZoom,
                "ill-conditioned zoom was not rejected atomically");

        const auto beforeRejectedPan = camera.camera();
        const double destination = random.between(600'000.0, 900'000.0);
        const double delta = -(destination - beforeRejectedPan.centerMetres.x) /
                             beforeRejectedPan.metresPerLogicalPixel;
        require(!camera.pan(delta, 0.0) && camera.camera() == beforeRejectedPan,
                "ill-conditioned pan was not rejected atomically");
      });
}

void verifyFitAndGuards() {
  SketchCameraController camera;
  const auto initial = camera.camera();
  require(!camera.pan(std::numeric_limits<double>::quiet_NaN(), 0.0) &&
              !camera.zoomAt(1.0, 0.0, 0.0, 0.0, 100.0) &&
              !camera.zoomAt(1.0, std::numeric_limits<double>::max() * 0.5,
                             50.0, std::numeric_limits<double>::max(), 100.0) &&
              !camera.rotate(std::numeric_limits<double>::infinity()) &&
              !camera.fit({{1.0, 0.0}, {0.0, 1.0}, false}, {100.0, 100.0}) &&
              camera.camera() == initial,
          "invalid input changed the sketch camera");

  require(!camera.fit({{-std::numeric_limits<double>::max(), 0.0},
                       {std::numeric_limits<double>::max(), 1.0},
                       false},
                      {100.0, 100.0}) &&
              camera.camera() == initial,
          "unrepresentable bounds partially changed the sketch camera");

  const render::Bounds2d bounds{{-0.4, -0.2}, {0.6, 0.3}, false};
  const QSizeF viewport{1'200.0, 800.0};
  require(camera.rotate(0.37) && camera.fit(bounds, viewport, 64.0),
          "valid rotated bounds did not fit");
  auto transform = SketchViewTransform::create(camera.camera(), viewport);
  require(transform.has_value(), "fit produced an invalid transform");
  for (const render::Point2d corner : {
           bounds.minimum,
           render::Point2d{bounds.minimum.x, bounds.maximum.y},
           bounds.maximum,
           render::Point2d{bounds.maximum.x, bounds.minimum.y},
       }) {
    const QPointF projected = transform->toItem(corner);
    require(projected.x() >= 64.0 - 1.0e-7 &&
                projected.x() <= viewport.width() - 64.0 + 1.0e-7 &&
                projected.y() >= 64.0 - 1.0e-7 &&
                projected.y() <= viewport.height() - 64.0 + 1.0e-7,
            "fit left a bounds corner outside its viewport margin");
  }
}

} // namespace

int main(int argc, char *argv[]) {
  try {
    QCoreApplication application(argc, argv);
    const auto profile = testkit::propertyProfile();
    verifyGeneratedCamera(profile);
    verifyGeneratedPrecisionBoundary(profile);
    verifyFitAndGuards();
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
