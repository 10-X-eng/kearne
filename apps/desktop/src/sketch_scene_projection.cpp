#include "sketch_scene_projection.hpp"
#include "sketch_prepared_products.hpp"
#include "sketch_projection_support.hpp"
#include "sketch_stroke_mesh_build.hpp"
#include "sketch_stroke_pattern.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <functional>
#include <limits>
#include <numbers>
#include <ranges>
#include <stdexcept>
#include <utility>

namespace kearne::ui {
namespace {

Diagnostic cancelledPreparation() {
  return diagnostic("desktop.sketch.preparation-cancelled",
                    "sketch scene preparation was cancelled");
}

bool finite(QPointF point) {
  return std::isfinite(point.x()) && std::isfinite(point.y());
}

bool finite(render::Point2d point) {
  return std::isfinite(point.x) && std::isfinite(point.y);
}
bool validPickCoverage(SketchPickCoveragePolicy policy) {
  return policy.generation != 0U &&
         std::isfinite(policy.maximumToleranceLogicalPixels) &&
         policy.maximumToleranceLogicalPixels >= 0.0 &&
         policy.maximumToleranceLogicalPixels <=
             SketchPickCoveragePolicy::
                 maximumConfigurableToleranceLogicalPixels &&
         policy.maximumRenderedTriangleTests != 0U &&
         policy.maximumRenderedTriangleTests <=
             SketchPickCoveragePolicy::
                 maximumConfigurableRenderedTriangleTests &&
         policy.maximumRenderedSpanProbes != 0U &&
         policy.maximumRenderedSpanProbes <=
             SketchPickCoveragePolicy::maximumConfigurableRenderedSpanProbes &&
         policy.maximumPatternIntervals != 0U &&
         policy.maximumPatternIntervals <=
             SketchPickCoveragePolicy::maximumConfigurablePatternIntervals;
}
struct RenderedPickVertex {
  double x = 0.0;
  double y = 0.0;
  double pathLogicalPixels = 0.0;
  double coverageDistancePixels = 0.0;
  double coverageRadiusPixels = 0.0;
};

[[nodiscard]] bool finite(const RenderedPickVertex &vertex) {
  return std::isfinite(vertex.x) && std::isfinite(vertex.y) &&
         std::isfinite(vertex.pathLogicalPixels) &&
         std::isfinite(vertex.coverageDistancePixels) &&
         std::isfinite(vertex.coverageRadiusPixels) &&
         vertex.coverageRadiusPixels >= 0.0;
}

[[nodiscard]] RenderedPickVertex projectVertex(const SketchMeshVertex &vertex,
                                               const SketchGpuView &view,
                                               QSizeF viewport,
                                               bool patterned) {
  const float relativeX =
      (vertex.x - view.centerOffsetX) + (vertex.xLow - view.centerOffsetXLow);
  const float relativeY =
      (vertex.y - view.centerOffsetY) + (vertex.yLow - view.centerOffsetYLow);
  const float rotatedX = view.cosine * relativeX - view.sine * relativeY;
  const float rotatedY = view.sine * relativeX + view.cosine * relativeY;
  float itemX = static_cast<float>(viewport.width()) * 0.5F;
  float itemY = static_cast<float>(viewport.height()) * 0.5F;
  itemX += rotatedX / view.metresPerLogicalPixel;
  itemY -= rotatedY / view.metresPerLogicalPixel;
  itemX += view.cosine * vertex.extrusionX - view.sine * vertex.extrusionY;
  itemY -= view.sine * vertex.extrusionX + view.cosine * vertex.extrusionY;
  const float pathLogicalPixels =
      patterned ? vertex.pathDistanceMetres / view.metresPerLogicalPixel : 0.0F;
  return {itemX, itemY, pathLogicalPixels, vertex.coverageDistancePixels,
          vertex.coverageRadiusPixels};
}

[[nodiscard]] double pointDistance(double firstX, double firstY, double secondX,
                                   double secondY) {
  const double x = firstX - secondX;
  const double y = firstY - secondY;
  if (std::isfinite(x) && std::isfinite(y))
    return std::hypot(x, y);
  const double scale = std::max({std::abs(firstX), std::abs(firstY),
                                 std::abs(secondX), std::abs(secondY)});
  if (scale == 0.0)
    return 0.0;
  return std::hypot(firstX / scale - secondX / scale,
                    firstY / scale - secondY / scale) *
         scale;
}

[[nodiscard]] double segmentDistance(QPointF point,
                                     const RenderedPickVertex &first,
                                     const RenderedPickVertex &second) {
  const long double x = static_cast<long double>(second.x) - first.x;
  const long double y = static_cast<long double>(second.y) - first.y;
  const long double queryX = static_cast<long double>(point.x()) - first.x;
  const long double queryY = static_cast<long double>(point.y()) - first.y;
  const long double squared = x * x + y * y;
  if (squared == 0.0L)
    return pointDistance(point.x(), point.y(), first.x, first.y);
  const long double amount =
      std::clamp((queryX * x + queryY * y) / squared, 0.0L, 1.0L);
  const long double closestX = first.x + amount * x;
  const long double closestY = first.y + amount * y;
  const long double distance =
      std::hypot(static_cast<long double>(point.x()) - closestX,
                 static_cast<long double>(point.y()) - closestY);
  return distance > std::numeric_limits<double>::max()
             ? std::numeric_limits<double>::infinity()
             : static_cast<double>(distance);
}

[[nodiscard]] long double edgeSide(const RenderedPickVertex &first,
                                   const RenderedPickVertex &second,
                                   QPointF point) {
  return (static_cast<long double>(second.x) - first.x) *
             (static_cast<long double>(point.y()) - first.y) -
         (static_cast<long double>(second.y) - first.y) *
             (static_cast<long double>(point.x()) - first.x);
}

[[nodiscard]] double
polygonDistance(QPointF point, std::span<const RenderedPickVertex> polygon) {
  if (polygon.size() == 1U)
    return pointDistance(point.x(), point.y(), polygon.front().x,
                         polygon.front().y);
  if (polygon.size() == 2U)
    return segmentDistance(point, polygon.front(), polygon.back());
  bool positive = false;
  bool negative = false;
  double distance = std::numeric_limits<double>::infinity();
  long double area = 0.0L;
  for (std::size_t index = 0U; index < polygon.size(); ++index) {
    const RenderedPickVertex &first = polygon[index];
    const RenderedPickVertex &second = polygon[(index + 1U) % polygon.size()];
    const long double side = edgeSide(first, second, point);
    positive = positive || side > 0.0L;
    negative = negative || side < 0.0L;
    area += static_cast<long double>(first.x) * second.y -
            static_cast<long double>(first.y) * second.x;
    distance = std::min(distance, segmentDistance(point, first, second));
  }
  return area == 0.0L || (positive && negative) ? distance : 0.0;
}

template <typename Coordinate>
[[nodiscard]] std::size_t
clipRenderedBoundary(std::span<const RenderedPickVertex> input,
                     std::span<RenderedPickVertex> output, double boundary,
                     bool keepGreater, Coordinate coordinate) noexcept {
  constexpr std::size_t overflow = std::numeric_limits<std::size_t>::max();
  std::size_t write = 0U;
  const auto inside = [&](const RenderedPickVertex &vertex) {
    return keepGreater ? std::invoke(coordinate, vertex) >= boundary
                       : std::invoke(coordinate, vertex) <= boundary;
  };
  const auto append = [&](RenderedPickVertex vertex) {
    if (write == output.size())
      return false;
    output[write++] = vertex;
    return true;
  };
  RenderedPickVertex previous = input.back();
  bool previousInside = inside(previous);
  for (const RenderedPickVertex current : input) {
    const bool currentInside = inside(current);
    if (previousInside != currentInside) {
      const double previousCoordinate = std::invoke(coordinate, previous);
      const double amount =
          (boundary - previousCoordinate) /
          (std::invoke(coordinate, current) - previousCoordinate);
      if (!append({std::lerp(previous.x, current.x, amount),
                   std::lerp(previous.y, current.y, amount),
                   std::lerp(previous.pathLogicalPixels,
                             current.pathLogicalPixels, amount),
                   std::lerp(previous.coverageDistancePixels,
                             current.coverageDistancePixels, amount),
                   std::lerp(previous.coverageRadiusPixels,
                             current.coverageRadiusPixels, amount)}))
        return overflow;
    }
    if (currentInside && !append(current))
      return overflow;
    previous = current;
    previousInside = currentInside;
  }
  return write;
}

enum class RenderedTriangleDecision : std::uint8_t {
  Eligible,
  Ineligible,
  WorkBudgetExceeded,
  NonFiniteArithmetic,
};

struct RenderedTriangleEvaluation {
  RenderedTriangleDecision decision = RenderedTriangleDecision::Ineligible;
  double distanceLogicalPixels = std::numeric_limits<double>::infinity();
};

[[nodiscard]] RenderedTriangleEvaluation renderedTriangleEligible(
    const std::array<RenderedPickVertex, 3> &triangle, QPointF query,
    QSizeF viewportLogical, double toleranceLogicalPixels,
    SketchStrokePattern pattern, std::uint32_t &patternIntervals,
    std::uint32_t maximumPatternIntervals) noexcept {
  constexpr std::size_t clippingCapacity = 12U;
  constexpr std::size_t clippingOverflow =
      std::numeric_limits<std::size_t>::max();
  if (!std::ranges::all_of(triangle, [](const RenderedPickVertex &vertex) {
        return finite(vertex);
      }))
    return {RenderedTriangleDecision::NonFiniteArithmetic};

  std::array<RenderedPickVertex, clippingCapacity> firstClip{};
  std::array<RenderedPickVertex, clippingCapacity> secondClip{};
  std::array<RenderedPickVertex, clippingCapacity> viewportClip{};
  const auto x = [](const RenderedPickVertex &vertex) { return vertex.x; };
  const auto y = [](const RenderedPickVertex &vertex) { return vertex.y; };
  const auto positiveCoverage = [](const RenderedPickVertex &vertex) {
    return vertex.coverageDistancePixels - vertex.coverageRadiusPixels;
  };
  const auto negativeCoverage = [](const RenderedPickVertex &vertex) {
    return -vertex.coverageDistancePixels - vertex.coverageRadiusPixels;
  };
  std::size_t count =
      clipRenderedBoundary(triangle, firstClip, 0.0, false, positiveCoverage);
  if (count == clippingOverflow)
    return {RenderedTriangleDecision::WorkBudgetExceeded};
  count =
      count == 0U
          ? 0U
          : clipRenderedBoundary(
                std::span<const RenderedPickVertex>{firstClip.data(), count},
                secondClip, 0.0, false, negativeCoverage);
  if (count == clippingOverflow)
    return {RenderedTriangleDecision::WorkBudgetExceeded};
  count =
      count == 0U
          ? 0U
          : clipRenderedBoundary(
                std::span<const RenderedPickVertex>{secondClip.data(), count},
                firstClip, 0.0, true, x);
  if (count == clippingOverflow)
    return {RenderedTriangleDecision::WorkBudgetExceeded};
  count =
      count == 0U
          ? 0U
          : clipRenderedBoundary(
                std::span<const RenderedPickVertex>{firstClip.data(), count},
                secondClip, viewportLogical.width(), false, x);
  if (count == clippingOverflow)
    return {RenderedTriangleDecision::WorkBudgetExceeded};
  count =
      count == 0U
          ? 0U
          : clipRenderedBoundary(
                std::span<const RenderedPickVertex>{secondClip.data(), count},
                firstClip, 0.0, true, y);
  if (count == clippingOverflow)
    return {RenderedTriangleDecision::WorkBudgetExceeded};
  count =
      count == 0U
          ? 0U
          : clipRenderedBoundary(
                std::span<const RenderedPickVertex>{firstClip.data(), count},
                viewportClip, viewportLogical.height(), false, y);
  if (count == clippingOverflow)
    return {RenderedTriangleDecision::WorkBudgetExceeded};
  if (count == 0U)
    return {RenderedTriangleDecision::Ineligible};
  const std::span<const RenderedPickVertex> displayedTriangle{
      viewportClip.data(), count};

  if (pattern.periodLogicalPixels <= 0.0F ||
      pattern.onLogicalPixels >= pattern.periodLogicalPixels) {
    const double distance = polygonDistance(query, displayedTriangle);
    return {distance <= toleranceLogicalPixels
                ? RenderedTriangleDecision::Eligible
                : RenderedTriangleDecision::Ineligible,
            distance};
  }
  const float minimum = static_cast<float>(
      std::ranges::min(displayedTriangle, {},
                       &RenderedPickVertex::pathLogicalPixels)
          .pathLogicalPixels);
  const float maximum = static_cast<float>(
      std::ranges::max(displayedTriangle, {},
                       &RenderedPickVertex::pathLogicalPixels)
          .pathLogicalPixels);
  const float period = pattern.periodLogicalPixels;
  const float on = pattern.onLogicalPixels;
  float phase = std::fmod(minimum, period);
  if (!std::isfinite(phase))
    return {RenderedTriangleDecision::NonFiniteArithmetic};
  if (phase < 0.0)
    phase += period;
  float interval = minimum - phase;
  double minimumDistance = std::numeric_limits<double>::infinity();
  while (interval <= maximum) {
    if (patternIntervals == maximumPatternIntervals)
      return {RenderedTriangleDecision::WorkBudgetExceeded};
    ++patternIntervals;
    const float inclusiveEnd = on > std::numeric_limits<float>::max() - interval
                                   ? std::numeric_limits<float>::max()
                                   : interval + on;
    const float intervalEnd =
        std::nextafter(inclusiveEnd, -std::numeric_limits<float>::infinity());
    const std::size_t firstCount =
        clipRenderedBoundary(displayedTriangle, firstClip, interval, true,
                             &RenderedPickVertex::pathLogicalPixels);
    if (firstCount == clippingOverflow)
      return {RenderedTriangleDecision::WorkBudgetExceeded};
    const std::size_t secondCount =
        firstCount == 0U ? 0U
                         : clipRenderedBoundary(
                               std::span<const RenderedPickVertex>{
                                   firstClip.data(), firstCount},
                               secondClip, intervalEnd, false,
                               &RenderedPickVertex::pathLogicalPixels);
    if (secondCount == clippingOverflow)
      return {RenderedTriangleDecision::WorkBudgetExceeded};
    if (secondCount != 0U)
      minimumDistance =
          std::min(minimumDistance,
                   polygonDistance(query, std::span<const RenderedPickVertex>{
                                              secondClip.data(), secondCount}));
    if (period > std::numeric_limits<float>::max() - interval)
      break;
    const float next = interval + period;
    if (!(next > interval))
      return {RenderedTriangleDecision::WorkBudgetExceeded};
    interval = next;
  }
  return {minimumDistance <= toleranceLogicalPixels
              ? RenderedTriangleDecision::Eligible
              : RenderedTriangleDecision::Ineligible,
          minimumDistance};
}

struct DisplayedPickEligibilityContext {
  const SynchronizedSketchScene *frame = nullptr;
  QPointF query;
  SketchGpuView view;
  double toleranceLogicalPixels = 0.0;
  std::uint32_t spanProbes = 0U;
  std::uint32_t triangleTests = 0U;
  std::uint32_t patternIntervals = 0U;
};

[[nodiscard]] render::SketchPickEligibility::Evaluation
displayedPickEligible(void *opaque,
                      const render::SketchPickResult &candidate) noexcept {
  auto &context = *static_cast<DisplayedPickEligibilityContext *>(opaque);
  const SynchronizedSketchScene &frame = *context.frame;
  const render::PackedSketchPrimitive *primitive =
      frame.scene()->findPrimitive(candidate.entity);
  if (!primitive || primitive->handle != candidate.primitive ||
      (candidate.pointKey &&
       primitive->kind != render::SketchPrimitiveKind::Point))
    return {render::SketchPickEligibilityDecision::Ineligible};
  const auto &coverage = frame.presentedChunks();
  const auto &index = frame.prepared()->primitiveTessellationIndex();
  if (!coverage || !index)
    return {render::SketchPickEligibilityDecision::Ineligible};
  const auto spans = index->spans(primitive->handle);
  const auto chunks = frame.mesh()->chunks();
  const SketchStrokePattern pattern =
      strokePattern(frame.mesh()->styles()[primitive->style]);
  const bool patterned = pattern.periodLogicalPixels > 0.0F;
  double minimumDistance = std::numeric_limits<double>::infinity();
  for (const SketchPrimitiveChunkSpan span : spans) {
    if (context.spanProbes == frame.pickCoverage().maximumRenderedSpanProbes)
      return {render::SketchPickEligibilityDecision::WorkBudgetExceeded};
    ++context.spanProbes;
    if (!coverage->contains(span.chunk))
      continue;
    const auto &chunk = chunks[span.chunk];
    const auto vertices = chunk->vertices();
    const auto indices = chunk->indices();
    const std::size_t end =
        static_cast<std::size_t>(span.firstIndex) + span.indexCount;
    for (std::size_t first = span.firstIndex; first < end; first += 3U) {
      if (context.triangleTests ==
          frame.pickCoverage().maximumRenderedTriangleTests)
        return {render::SketchPickEligibilityDecision::WorkBudgetExceeded};
      ++context.triangleTests;
      std::array<RenderedPickVertex, 3> triangle;
      for (std::size_t corner = 0U; corner < triangle.size(); ++corner)
        triangle[corner] =
            projectVertex(vertices[indices[first + corner]], context.view,
                          frame.transform().viewportLogical(), patterned);
      const RenderedTriangleEvaluation evaluated = renderedTriangleEligible(
          triangle, context.query, frame.transform().viewportLogical(),
          context.toleranceLogicalPixels, pattern, context.patternIntervals,
          frame.pickCoverage().maximumPatternIntervals);
      switch (evaluated.decision) {
      case RenderedTriangleDecision::Eligible:
        minimumDistance =
            std::min(minimumDistance, evaluated.distanceLogicalPixels);
        if (minimumDistance == 0.0)
          return {render::SketchPickEligibilityDecision::Eligible, 0.0};
        break;
      case RenderedTriangleDecision::Ineligible:
        break;
      case RenderedTriangleDecision::WorkBudgetExceeded:
        return {render::SketchPickEligibilityDecision::WorkBudgetExceeded};
      case RenderedTriangleDecision::NonFiniteArithmetic:
        return {render::SketchPickEligibilityDecision::NonFiniteArithmetic};
      }
    }
  }
  if (!std::isfinite(minimumDistance))
    return {render::SketchPickEligibilityDecision::Ineligible};
  const double canonicalDistance =
      minimumDistance * frame.transform().camera().metresPerLogicalPixel;
  if (!std::isfinite(canonicalDistance))
    return {render::SketchPickEligibilityDecision::NonFiniteArithmetic};
  return {render::SketchPickEligibilityDecision::Eligible, canonicalDistance};
}

} // namespace

SketchViewTransform::SketchViewTransform(SketchCamera2d camera,
                                         QSizeF viewportLogical, double cosine,
                                         double sine)
    : camera_(camera), viewportLogical_(viewportLogical), cosine_(cosine),
      sine_(sine) {}

Result<SketchViewTransform>
SketchViewTransform::create(SketchCamera2d camera, QSizeF viewportLogical) {
  if (camera.generation == 0U || !finite(camera.centerMetres) ||
      !std::isfinite(camera.metresPerLogicalPixel) ||
      camera.metresPerLogicalPixel <= 0.0 ||
      !std::isfinite(camera.rotationRadians))
    return std::unexpected(diagnostic("desktop.sketch.invalid-camera",
                                      "sketch camera is invalid"));
  if (!std::isfinite(viewportLogical.width()) ||
      !std::isfinite(viewportLogical.height()) ||
      viewportLogical.width() <= 0.0 || viewportLogical.height() <= 0.0)
    return std::unexpected(diagnostic("desktop.sketch.invalid-viewport",
                                      "sketch viewport is invalid"));
  constexpr double maximumGpuValue = std::numeric_limits<float>::max();
  if (viewportLogical.width() > maximumGpuValue ||
      viewportLogical.height() > maximumGpuValue)
    return std::unexpected(
        diagnostic("desktop.sketch.unrepresentable-viewport",
                   "sketch viewport exceeds finite GPU range"));
  const double extent =
      std::hypot(viewportLogical.width(), viewportLogical.height()) *
      camera.metresPerLogicalPixel;
  if (!std::isfinite(extent) ||
      !std::isfinite(camera.centerMetres.x + extent) ||
      !std::isfinite(camera.centerMetres.x - extent) ||
      !std::isfinite(camera.centerMetres.y + extent) ||
      !std::isfinite(camera.centerMetres.y - extent))
    return std::unexpected(
        diagnostic("desktop.sketch.unrepresentable-view",
                   "sketch camera extent exceeds finite coordinate range"));
  return SketchViewTransform{camera, viewportLogical,
                             std::cos(camera.rotationRadians),
                             std::sin(camera.rotationRadians)};
}

QPointF SketchViewTransform::toItem(render::Point2d canonicalMetres) const {
  const double x = canonicalMetres.x - camera_.centerMetres.x;
  const double y = canonicalMetres.y - camera_.centerMetres.y;
  const double rotatedX = cosine_ * x - sine_ * y;
  const double rotatedY = sine_ * x + cosine_ * y;
  return {viewportLogical_.width() * 0.5 +
              rotatedX / camera_.metresPerLogicalPixel,
          viewportLogical_.height() * 0.5 -
              rotatedY / camera_.metresPerLogicalPixel};
}

render::Point2d SketchViewTransform::toCanonical(QPointF itemLogical) const {
  const double screenX = (itemLogical.x() - viewportLogical_.width() * 0.5) *
                         camera_.metresPerLogicalPixel;
  const double screenY = -(itemLogical.y() - viewportLogical_.height() * 0.5) *
                         camera_.metresPerLogicalPixel;
  return {camera_.centerMetres.x + cosine_ * screenX + sine_ * screenY,
          camera_.centerMetres.y - sine_ * screenX + cosine_ * screenY};
}

Result<QMatrix4x4>
SketchViewTransform::itemMatrix(render::Point2d sceneOriginMetres) const {
  const double originX = sceneOriginMetres.x - camera_.centerMetres.x;
  const double originY = sceneOriginMetres.y - camera_.centerMetres.y;
  const double inverseScale = 1.0 / camera_.metresPerLogicalPixel;
  const double translationX =
      viewportLogical_.width() * 0.5 +
      (cosine_ * originX - sine_ * originY) * inverseScale;
  const double translationY =
      viewportLogical_.height() * 0.5 -
      (sine_ * originX + cosine_ * originY) * inverseScale;
  constexpr double maximum = std::numeric_limits<float>::max();
  const std::array<double, 6> values{
      cosine_ * inverseScale,  -sine_ * inverseScale, -sine_ * inverseScale,
      -cosine_ * inverseScale, translationX,          translationY};
  if (!finite(sceneOriginMetres) ||
      std::ranges::any_of(values, [maximum](double value) {
        return !std::isfinite(value) || std::abs(value) > maximum;
      }))
    return std::unexpected(
        diagnostic("desktop.sketch.unrepresentable-matrix",
                   "sketch scene-to-item matrix exceeds finite GPU range"));
  return QMatrix4x4{static_cast<float>(values[0]),
                    static_cast<float>(values[1]),
                    0.0F,
                    static_cast<float>(values[4]),
                    static_cast<float>(values[2]),
                    static_cast<float>(values[3]),
                    0.0F,
                    static_cast<float>(values[5]),
                    0.0F,
                    0.0F,
                    1.0F,
                    0.0F,
                    0.0F,
                    0.0F,
                    0.0F,
                    1.0F};
}

Result<SketchGpuView>
SketchViewTransform::gpuView(render::Point2d sceneOriginMetres) const {
  if (!finite(sceneOriginMetres))
    return std::unexpected(diagnostic("desktop.sketch.unrepresentable-gpu-view",
                                      "sketch GPU view origin is not finite"));
  const double centerX = camera_.centerMetres.x - sceneOriginMetres.x;
  const double centerY = camera_.centerMetres.y - sceneOriginMetres.y;
  const float highX = static_cast<float>(centerX);
  const float highY = static_cast<float>(centerY);
  const float scale = static_cast<float>(camera_.metresPerLogicalPixel);
  const float cosine = static_cast<float>(cosine_);
  const float sine = static_cast<float>(sine_);
  if (!std::isfinite(centerX) || !std::isfinite(centerY) ||
      !std::isfinite(highX) || !std::isfinite(highY) || !std::isfinite(scale) ||
      scale <= 0.0F || !std::isfinite(cosine) || !std::isfinite(sine))
    return std::unexpected(
        diagnostic("desktop.sketch.unrepresentable-gpu-view",
                   "sketch GPU view exceeds finite float range"));
  const float lowX = static_cast<float>(centerX - static_cast<double>(highX));
  const float lowY = static_cast<float>(centerY - static_cast<double>(highY));
  if (!std::isfinite(lowX) || !std::isfinite(lowY))
    return std::unexpected(
        diagnostic("desktop.sketch.unrepresentable-gpu-view",
                   "sketch GPU view residual exceeds finite float range"));
  return SketchGpuView{highX, highY, lowX, lowY, scale, cosine, sine};
}

QRgb SketchScenePalette::color(render::SketchStyleRole role) const {
  switch (role) {
  case render::SketchStyleRole::Regular:
    return regular;
  case render::SketchStyleRole::Construction:
    return construction;
  case render::SketchStyleRole::Selected:
    return selected;
  case render::SketchStyleRole::Preview:
    return preview;
  case render::SketchStyleRole::Diagnostic:
    return diagnostic;
  case render::SketchStyleRole::Hovered:
    return hovered;
  }
  return regular;
}

SketchPrimitiveTessellationIndex::SketchPrimitiveTessellationIndex(
    std::vector<SketchPrimitiveTessellationEntry> entries,
    std::vector<SketchPrimitiveChunkSpan> spans, std::size_t retainedBytes)
    : entries_(std::move(entries)), spans_(std::move(spans)),
      retainedBytes_(retainedBytes) {}

const SketchPrimitiveTessellationEntry *SketchPrimitiveTessellationIndex::find(
    render::SketchPrimitiveHandle primitive) const {
  const auto found = std::ranges::lower_bound(
      entries_, primitive, {}, &SketchPrimitiveTessellationEntry::primitive);
  return found != entries_.end() && found->primitive == primitive ? &*found
                                                                  : nullptr;
}

std::span<const SketchPrimitiveChunkSpan>
SketchPrimitiveTessellationIndex::spans(
    render::SketchPrimitiveHandle primitive) const {
  const SketchPrimitiveTessellationEntry *entry = find(primitive);
  if (!entry || entry->spanCount == 0U ||
      static_cast<std::size_t>(entry->firstSpan) >= spans_.size() ||
      entry->spanCount > spans_.size() - entry->firstSpan)
    return {};
  return {spans_.data() + entry->firstSpan, entry->spanCount};
}

struct SketchSceneMeshAdapterAccess {
  static std::shared_ptr<const SketchPrimitiveTessellationIndex>
  makePrimitiveIndex(
      std::span<const SketchStrokePrimitiveSpanRecord> provenance) {
    std::vector<SketchPrimitiveTessellationEntry> entries;
    std::vector<SketchPrimitiveChunkSpan> spans;
    entries.reserve(provenance.size());
    spans.reserve(provenance.size());
    for (const SketchStrokePrimitiveSpanRecord &record : provenance) {
      if (entries.empty() ||
          entries.back().primitive.value() != record.sourceKey) {
        entries.push_back(
            {*render::SketchPrimitiveHandle::create(record.sourceKey),
             static_cast<std::uint32_t>(spans.size()), 0U, 0U});
      }
      SketchPrimitiveTessellationEntry &entry = entries.back();
      spans.push_back({record.chunk, record.firstIndex, record.indexCount});
      ++entry.spanCount;
      entry.indexCount += record.indexCount;
    }
    const std::size_t retainedBytes =
        sizeof(SketchPrimitiveTessellationIndex) +
        entries.capacity() * sizeof(SketchPrimitiveTessellationEntry) +
        spans.capacity() * sizeof(SketchPrimitiveChunkSpan);
    return std::shared_ptr<const SketchPrimitiveTessellationIndex>(
        new SketchPrimitiveTessellationIndex{std::move(entries),
                                             std::move(spans), retainedBytes});
  }
};

struct BuiltBaseStrokeMesh {
  SketchSceneMesh mesh;
  std::shared_ptr<const SketchPrimitiveTessellationIndex> provenance;
};

Result<BuiltBaseStrokeMesh>
buildBaseStrokeMesh(const render::SketchSceneSnapshot &scene,
                    SketchCurveLod lod, SketchTessellationOptions tessellation,
                    SketchUploadOptions upload,
                    std::shared_ptr<const SketchSceneMesh> reuse,
                    std::stop_token cancellation) {
  const auto primitiveAt = [](const void *opaque, std::size_t index) noexcept {
    const auto &snapshot =
        *static_cast<const render::SketchSceneSnapshot *>(opaque);
    const render::PackedSketchPrimitive &primitive =
        snapshot.primitives()[index];
    SketchStrokeSourceKind kind = SketchStrokeSourceKind::Point;
    switch (primitive.kind) {
    case render::SketchPrimitiveKind::Point:
      kind = SketchStrokeSourceKind::Point;
      break;
    case render::SketchPrimitiveKind::Line:
      kind = SketchStrokeSourceKind::Line;
      break;
    case render::SketchPrimitiveKind::Circle:
      kind = SketchStrokeSourceKind::Circle;
      break;
    case render::SketchPrimitiveKind::Arc:
      kind = SketchStrokeSourceKind::Arc;
      break;
    case render::SketchPrimitiveKind::Ellipse:
      kind = SketchStrokeSourceKind::Ellipse;
      break;
    case render::SketchPrimitiveKind::EllipticalArc:
      kind = SketchStrokeSourceKind::EllipticalArc;
      break;
    case render::SketchPrimitiveKind::HyperbolicArc:
      kind = SketchStrokeSourceKind::HyperbolicArc;
      break;
    case render::SketchPrimitiveKind::ParabolicArc:
      kind = SketchStrokeSourceKind::ParabolicArc;
      break;
    case render::SketchPrimitiveKind::BSpline:
      kind = SketchStrokeSourceKind::BSpline;
      break;
    }
    const render::Point2d first =
        primitive.kind == render::SketchPrimitiveKind::BSpline
            ? render::Point2d{}
            : snapshot.points()[primitive.firstPoint];
    const render::Point2d second =
        primitive.kind == render::SketchPrimitiveKind::Line
            ? snapshot.points()[primitive.firstPoint + 1U]
            : first;
    return SketchStrokeSourcePrimitive{
        primitive.handle.value(),
        primitive.style,
        kind,
        render::hasFlag(primitive.flags, render::SketchPrimitiveFlags::Visible),
        first,
        second,
        primitive.radius,
        primitive.startAngleRadians,
        primitive.sweepAngleRadians,
        0U,
        primitive.secondaryRadius,
        primitive.rotationAngleRadians};
  };
  const auto splineAt = [](const void *opaque,
                           std::size_t index) noexcept -> sketch::NurbsView {
    const auto &snapshot =
        *static_cast<const render::SketchSceneSnapshot *>(opaque);
    const render::PackedSketchPrimitive &primitive =
        snapshot.primitives()[index];
    if (primitive.kind != render::SketchPrimitiveKind::BSpline)
      return {};
    const render::PackedSketchSpline &spline =
        snapshot.splines()[primitive.spline];
    const std::size_t count = spline.controlPointCount;
    return {snapshot.splineControlPointCoordinates().subspan(
                static_cast<std::size_t>(spline.firstControlPoint) * 2U,
                count * 2U),
            snapshot.splineKnots().subspan(spline.firstKnot,
                                           count + spline.degree + 1U),
            snapshot.splineWeights().subspan(spline.firstWeight, count),
            spline.degree};
  };
  const auto &bounds = scene.bounds();
  SketchStrokeMeshSource source{scene.styles(),
                                &scene,
                                scene.primitives().size(),
                                primitiveAt,
                                {bounds.minimum, bounds.maximum, bounds.empty},
                                splineAt};
  auto built = SketchStrokeMeshBuildAccess::build(
      source, lod, tessellation, upload, std::move(reuse), cancellation);
  if (!built)
    return std::unexpected(std::move(built.error()));
  try {
    auto index =
        SketchSceneMeshAdapterAccess::makePrimitiveIndex(built->provenance);
    return BuiltBaseStrokeMesh{std::move(built->mesh), std::move(index)};
  } catch (const std::bad_alloc &) {
    return std::unexpected(
        diagnostic("desktop.sketch.provenance-allocation",
                   "typed sketch provenance index allocation failed"));
  } catch (const std::length_error &) {
    return std::unexpected(diagnostic(
        "desktop.sketch.provenance-budget",
        "typed sketch provenance index exceeded container capacity"));
  }
}

Result<SketchSceneMesh>
buildSketchSceneMesh(const render::SketchSceneSnapshot &scene,
                     SketchCurveLod lod, SketchTessellationOptions tessellation,
                     SketchUploadOptions upload,
                     std::shared_ptr<const SketchSceneMesh> reuse,
                     std::stop_token cancellation) {
  auto built = buildBaseStrokeMesh(scene, lod, tessellation, upload,
                                   std::move(reuse), cancellation);
  if (!built)
    return std::unexpected(std::move(built.error()));
  return std::move(built->mesh);
}

PreparedSketchScene::PreparedSketchScene(
    render::SceneStamp stamp,
    std::shared_ptr<const render::SketchSceneSnapshot> scene,
    std::shared_ptr<const render::SketchPickIndex> pickIndex,
    render::SketchPickIndexOptions pickOptions,
    std::shared_ptr<const SketchSceneMesh> mesh,
    std::shared_ptr<const SketchPrimitiveTessellationIndex>
        primitiveTessellationIndex,
    Metrics metrics, SketchCurveLod lod)
    : stamp_(std::move(stamp)), scene_(std::move(scene)),
      pickIndex_(std::move(pickIndex)), pickOptions_(pickOptions),
      mesh_(std::move(mesh)),
      primitiveTessellationIndex_(std::move(primitiveTessellationIndex)),
      metrics_(metrics), lod_(lod) {}

Result<std::shared_ptr<const PreparedSketchScene>>
prepareSketchScene(std::shared_ptr<const render::SketchSceneSnapshot> scene,
                   SketchCurveLod lod, SketchTessellationOptions tessellation,
                   render::SketchPickIndexOptions picking,
                   SketchUploadOptions upload,
                   std::shared_ptr<const PreparedSketchScene> reuse,
                   std::stop_token cancellation) {
  if (!scene)
    return std::unexpected(diagnostic("desktop.sketch.null-scene",
                                      "cannot prepare a null sketch scene"));
  if (cancellation.stop_requested())
    return std::unexpected(cancelledPreparation());
  auto mesh =
      buildBaseStrokeMesh(*scene, lod, tessellation, upload,
                          reuse ? reuse->mesh() : nullptr, cancellation);
  if (!mesh)
    return std::unexpected(std::move(mesh.error()));
  if (cancellation.stop_requested())
    return std::unexpected(cancelledPreparation());
  const auto samePickOptions =
      [](const render::SketchPickIndexOptions &first,
         const render::SketchPickIndexOptions &second) {
        return first.maximumRetainedBytes == second.maximumRetainedBytes &&
               first.maximumScratchBytes == second.maximumScratchBytes &&
               first.maximumPeakBuildBytes == second.maximumPeakBuildBytes &&
               first.maximumLeafTargets == second.maximumLeafTargets &&
               first.maximumVisitedNodesPerPass ==
                   second.maximumVisitedNodesPerPass &&
               first.maximumRefinedTargetsPerPass ==
                   second.maximumRefinedTargetsPerPass;
      };
  std::shared_ptr<const render::SketchPickIndex> pickIndex;
  if (reuse && reuse->scene() == scene &&
      samePickOptions(reuse->pickOptions(), picking)) {
    pickIndex = reuse->pickIndex();
  } else {
    auto built = render::SketchPickIndex::build(scene, picking, cancellation);
    if (!built) {
      if (built.error().code == "render.pick.cancelled")
        return std::unexpected(cancelledPreparation());
      return std::unexpected(std::move(built.error()));
    }
    pickIndex =
        std::make_shared<const render::SketchPickIndex>(std::move(*built));
  }
  if (cancellation.stop_requested())
    return std::unexpected(cancelledPreparation());
  PreparedSketchScene::Metrics metrics;
  metrics.meshRetainedBytes = mesh->mesh.metrics().retainedMeshBytes;
  metrics.provenanceRetainedBytes = mesh->provenance->retainedBytes();
  metrics.pickIndexRetainedBytes = pickIndex->retainedBytes();
  metrics.totalRetainedBytes = sizeof(PreparedSketchScene);
  if (!detail::checkedSizeAdd(metrics.totalRetainedBytes,
                              metrics.meshRetainedBytes,
                              metrics.totalRetainedBytes) ||
      !detail::checkedSizeAdd(metrics.totalRetainedBytes,
                              metrics.provenanceRetainedBytes,
                              metrics.totalRetainedBytes) ||
      !detail::checkedSizeAdd(metrics.totalRetainedBytes,
                              metrics.pickIndexRetainedBytes,
                              metrics.totalRetainedBytes))
    return std::unexpected(
        diagnostic("desktop.sketch.prepared-byte-overflow",
                   "prepared sketch retained byte accounting overflowed"));
  auto prepared =
      std::shared_ptr<const PreparedSketchScene>(new PreparedSketchScene{
          scene->stamp(), scene, std::move(pickIndex), picking,
          std::make_shared<const SketchSceneMesh>(std::move(mesh->mesh)),
          std::move(mesh->provenance), metrics, lod});
  if (cancellation.stop_requested())
    return std::unexpected(cancelledPreparation());
  return prepared;
}

ProgressiveSketchUpload::ProgressiveSketchUpload(
    std::shared_ptr<const SketchSceneMesh> mesh,
    SketchChunkSequence requiredChunks,
    std::vector<const SketchUploadChunk *> resident)
    : mesh_(std::move(mesh)), requiredChunks_(std::move(requiredChunks)),
      residentChunks_(std::move(resident)) {}

Result<ProgressiveSketchUpload> ProgressiveSketchUpload::create(
    std::shared_ptr<const PreparedSketchScene> prepared,
    std::vector<std::uint32_t> requiredChunks,
    std::span<const std::shared_ptr<const SketchUploadChunk>> residentChunks) {
  if (!prepared)
    return std::unexpected(
        diagnostic("desktop.sketch.null-progressive-scene",
                   "progressive upload requires a prepared sketch scene"));
  constexpr std::size_t coverageObjectBytes =
      sizeof(SketchPresentedChunkCoverage);
  auto sequence = SketchChunkSequence::create(
      *prepared->mesh(),
      SketchPresentedChunkCoverage::defaultMaximumRetainedBytes -
          coverageObjectBytes);
  if (!sequence)
    return std::unexpected(std::move(sequence.error()));
  for (const std::uint32_t chunk : requiredChunks) {
    auto appended = sequence->push_back(chunk);
    if (!appended)
      return std::unexpected(std::move(appended.error()));
  }
  return create(std::move(prepared), std::move(*sequence), residentChunks);
}

Result<ProgressiveSketchUpload> ProgressiveSketchUpload::create(
    std::shared_ptr<const PreparedSketchScene> prepared,
    SketchChunkSequence requiredChunks,
    std::span<const std::shared_ptr<const SketchUploadChunk>> residentChunks) {
  if (!prepared)
    return std::unexpected(
        diagnostic("desktop.sketch.null-progressive-scene",
                   "progressive upload requires a prepared sketch scene"));
  return create(prepared->mesh(), std::move(requiredChunks), residentChunks);
}

Result<ProgressiveSketchUpload> ProgressiveSketchUpload::create(
    std::shared_ptr<const SketchSceneMesh> mesh,
    SketchChunkSequence requiredChunks,
    std::span<const std::shared_ptr<const SketchUploadChunk>> residentChunks) {
  if (!mesh)
    return std::unexpected(
        diagnostic("desktop.sketch.null-progressive-mesh",
                   "progressive upload requires a prepared sketch mesh"));
  if (requiredChunks.meshIdentity_ != mesh.get())
    return std::unexpected(
        diagnostic("desktop.sketch.invalid-visible-chunks",
                   "visible sketch chunks belong to another mesh"));
  try {
    std::size_t requestedBytes = 0U;
    if (!detail::checkedSizeMultiply(residentChunks.size(),
                                     sizeof(const SketchUploadChunk *),
                                     requestedBytes) ||
        requestedBytes > maximumResidentIdentityBytes)
      return std::unexpected(diagnostic(
          "desktop.sketch.upload-resident-budget",
          "resident sketch chunk identities exceed their fixed budget"));
    std::vector<const SketchUploadChunk *> resident;
    resident.reserve(residentChunks.size());
    for (const auto &chunk : residentChunks)
      if (chunk)
        resident.push_back(chunk.get());
    std::ranges::sort(resident, std::less<const SketchUploadChunk *>{});
    resident.erase(std::ranges::unique(resident).begin(), resident.end());
    std::size_t actualBytes = 0U;
    if (!detail::checkedSizeMultiply(resident.capacity(),
                                     sizeof(const SketchUploadChunk *),
                                     actualBytes) ||
        actualBytes > maximumResidentIdentityBytes)
      return std::unexpected(diagnostic(
          "desktop.sketch.upload-resident-budget",
          "resident sketch chunk identity storage exceeds its fixed budget"));
    return ProgressiveSketchUpload{std::move(mesh), std::move(requiredChunks),
                                   std::move(resident)};
  } catch (const std::bad_alloc &) {
    return std::unexpected(
        diagnostic("desktop.sketch.upload-allocation",
                   "progressive sketch upload allocation failed"));
  } catch (const std::length_error &) {
    return std::unexpected(
        diagnostic("desktop.sketch.upload-budget",
                   "progressive sketch upload exceeded container capacity"));
  }
}

Result<SketchUploadSlice>
ProgressiveSketchUpload::takeNextSlice(std::size_t maximumBytes,
                                       std::size_t maximumChunks) {
  if (maximumBytes == 0U || maximumChunks == 0U)
    return std::unexpected(
        diagnostic("desktop.sketch.invalid-upload-slice",
                   "progressive upload slice budget is zero"));
  try {
    SketchUploadSlice slice;
    const auto chunks = mesh_->chunks();
    while (cursor_ < requiredChunks_.size() &&
           slice.entries.size() < maximumChunks) {
      const std::uint32_t index = requiredChunks_[cursor_];
      if (index >= chunks.size())
        return std::unexpected(diagnostic(
            "desktop.sketch.invalid-visible-chunks",
            "visible sketch upload chunks are not unique mesh indices"));
      const bool reuse =
          std::ranges::binary_search(residentChunks_, chunks[index].get(),
                                     std::less<const SketchUploadChunk *>{});
      const SketchUploadSliceEntry entry{index, reuse};
      const std::size_t bytes = reuse ? 0U : chunks[index]->payloadBytes();
      if (bytes > maximumBytes)
        return std::unexpected(
            diagnostic("desktop.sketch.upload-chunk-too-large",
                       "prepared upload chunk exceeds the per-frame ceiling"));
      if (!slice.entries.empty() && bytes > maximumBytes - slice.bytes)
        break;
      slice.entries.push_back(entry);
      slice.bytes += bytes;
      ++cursor_;
    }
    return slice;
  } catch (const std::bad_alloc &) {
    return std::unexpected(
        diagnostic("desktop.sketch.upload-allocation",
                   "progressive sketch upload slice allocation failed"));
  } catch (const std::length_error &) {
    return std::unexpected(
        diagnostic("desktop.sketch.upload-budget",
                   "progressive sketch upload slice exceeded capacity"));
  }
}

SketchChunkSequence ProgressiveSketchUpload::releaseRequiredChunks() {
  return std::move(requiredChunks_);
}

SynchronizedSketchScene::SynchronizedSketchScene(
    std::shared_ptr<const PreparedSketchProducts> products,
    SketchViewTransform transform, SketchPickCoveragePolicy pickCoverage,
    std::shared_ptr<const SketchPresentedChunkCoverage> presentedChunks)
    : products_(std::move(products)), transform_(std::move(transform)),
      pickCoverage_(pickCoverage),
      presentedChunks_(std::move(presentedChunks)) {}

const std::shared_ptr<const PreparedSketchProducts> &
SynchronizedSketchScene::products() const {
  return products_;
}

const std::shared_ptr<const PreparedSketchScene> &
SynchronizedSketchScene::prepared() const {
  return products_->base();
}

const std::shared_ptr<const render::SketchSceneSnapshot> &
SynchronizedSketchScene::scene() const {
  return products_->base()->scene();
}

const std::shared_ptr<const render::SketchPickIndex> &
SynchronizedSketchScene::pickIndex() const {
  return products_->base()->pickIndex();
}

const std::shared_ptr<const SketchSceneMesh> &
SynchronizedSketchScene::mesh() const {
  return products_->base()->mesh();
}

SketchCurveLod SynchronizedSketchScene::lod() const { return products_->lod(); }

PresentedSketchFrame::PresentedSketchFrame(
    std::shared_ptr<const SynchronizedSketchScene> synchronized,
    SketchPresentedProductCoverage productCoverage,
    SketchPresentationEvidence evidence)
    : synchronized_(std::move(synchronized)),
      productCoverage_(std::move(productCoverage)),
      evidence_(std::move(evidence)) {}

SketchScenePresenter::SketchScenePresenter() = default;

void SketchScenePresenter::retarget(render::SceneTarget desired) {
  std::scoped_lock lock{stateMutex_};
  desired_ = std::move(desired);
  latestAcceptedScene_.reset();
  pending_.reset();
}

Result<PreparedSketchSceneOffer> SketchScenePresenter::publish(
    std::shared_ptr<const PreparedSketchProducts> prepared) {
  if (!prepared)
    return std::unexpected(
        diagnostic("desktop.sketch.null-prepared-products",
                   "cannot publish null prepared sketch products"));
  std::scoped_lock lock{stateMutex_};
  if (!desired_)
    return std::unexpected(
        diagnostic("desktop.sketch.missing-target",
                   "sketch scene target must be set before publication"));
  if (prepared->stamp().target != *desired_)
    return PreparedSketchSceneOffer{PreparedSketchSceneDecision::StaleTarget,
                                    false};
  const SketchCurveLod requested =
      SketchCurveLod::forMetresPerLogicalPixel(camera_.metresPerLogicalPixel);
  if (prepared->lod() != requested)
    return PreparedSketchSceneOffer{PreparedSketchSceneDecision::StaleLod,
                                    false};

  std::shared_ptr<const PreparedSketchProducts> installed;
  if (pending_) {
    installed = pending_;
  } else {
    auto current = current_.load(std::memory_order_acquire);
    if (current && current->products()->stamp().target == *desired_)
      installed = current->products();
  }
  if (installed) {
    if (prepared->stamp().generation < installed->stamp().generation)
      return PreparedSketchSceneOffer{
          PreparedSketchSceneDecision::StaleGeneration, false};
    if (prepared->stamp().generation == installed->stamp().generation) {
      if (prepared->stamp() != installed->stamp())
        return PreparedSketchSceneOffer{
            PreparedSketchSceneDecision::GenerationConflict, false};
      if (!sameSketchSceneProductComponents(*prepared->source(),
                                            *installed->source()))
        return PreparedSketchSceneOffer{
            PreparedSketchSceneDecision::GenerationConflict, false};
      if (prepared->lod() == installed->lod()) {
        latestAcceptedScene_ = prepared->base()->stamp();
        return PreparedSketchSceneOffer{PreparedSketchSceneDecision::Duplicate,
                                        false};
      }
    }
    const auto &nextScene = prepared->base()->stamp();
    const auto &installedScene = installed->base()->stamp();
    if (nextScene.generation < installedScene.generation)
      return PreparedSketchSceneOffer{
          PreparedSketchSceneDecision::StaleGeneration, false};
    if (nextScene.generation == installedScene.generation &&
        (nextScene != installedScene ||
         prepared->base()->scene() != installed->base()->scene()))
      return PreparedSketchSceneOffer{
          PreparedSketchSceneDecision::GenerationConflict, false};
  }
  const bool replaced = static_cast<bool>(pending_);
  latestAcceptedScene_ = prepared->base()->stamp();
  pending_ = std::move(prepared);
  return PreparedSketchSceneOffer{PreparedSketchSceneDecision::Accepted,
                                  replaced};
}

Result<SketchCameraDecision>
SketchScenePresenter::publishCamera(SketchCamera2d camera) {
  if (camera.generation == 0U || !finite(camera.centerMetres) ||
      !std::isfinite(camera.metresPerLogicalPixel) ||
      camera.metresPerLogicalPixel <= 0.0 ||
      !std::isfinite(camera.rotationRadians))
    return std::unexpected(diagnostic("desktop.sketch.invalid-camera",
                                      "sketch camera is invalid"));
  std::scoped_lock lock{stateMutex_};
  if (camera.generation < camera_.generation)
    return SketchCameraDecision::StaleGeneration;
  if (camera.generation == camera_.generation)
    return camera == camera_ ? SketchCameraDecision::Duplicate
                             : SketchCameraDecision::GenerationConflict;
  const SketchCurveLod previousLod =
      SketchCurveLod::forMetresPerLogicalPixel(camera_.metresPerLogicalPixel);
  const SketchCurveLod nextLod =
      SketchCurveLod::forMetresPerLogicalPixel(camera.metresPerLogicalPixel);
  camera_ = camera;
  if (nextLod != previousLod && pending_ && pending_->lod() != nextLod)
    pending_.reset();
  return SketchCameraDecision::Accepted;
}

Result<SketchPickCoverageDecision>
SketchScenePresenter::publishPickCoverage(SketchPickCoveragePolicy policy) {
  if (!validPickCoverage(policy))
    return std::unexpected(
        diagnostic("desktop.sketch.invalid-pick-coverage",
                   "sketch pick coverage policy is invalid"));
  std::scoped_lock lock{stateMutex_};
  if (policy.generation < pickCoverage_.generation)
    return SketchPickCoverageDecision::StaleGeneration;
  if (policy.generation == pickCoverage_.generation)
    return policy == pickCoverage_
               ? SketchPickCoverageDecision::Duplicate
               : SketchPickCoverageDecision::GenerationConflict;
  pickCoverage_ = policy;
  return SketchPickCoverageDecision::Accepted;
}

SketchCurveLod SketchScenePresenter::requestedLod() const {
  std::scoped_lock lock{stateMutex_};
  return SketchCurveLod::forMetresPerLogicalPixel(
      camera_.metresPerLogicalPixel);
}

Result<std::shared_ptr<const SynchronizedSketchScene>>
SketchScenePresenter::synchronize(QSizeF viewportLogical) {
  std::shared_ptr<const PreparedSketchProducts> prepared;
  bool installsPreparedPacket = false;
  SketchCamera2d camera;
  SketchPickCoveragePolicy pickCoverage;
  {
    std::scoped_lock lock{stateMutex_};
    ++synchronizationMetrics_.calls;
    if (!desired_)
      return std::unexpected(
          diagnostic("desktop.sketch.missing-target",
                     "sketch scene target must be set before synchronization"));
    camera = camera_;
    pickCoverage = pickCoverage_;
    if (pending_) {
      prepared = std::exchange(pending_, {});
      installsPreparedPacket = true;
    } else {
      auto previous = current_.load(std::memory_order_acquire);
      if (previous)
        prepared = previous->products();
    }
  }
  if (!prepared)
    return std::unexpected(
        diagnostic("desktop.sketch.missing-prepared-scene",
                   "no prepared sketch scene has been published"));
  auto transform = SketchViewTransform::create(camera, viewportLogical);
  if (!transform) {
    if (installsPreparedPacket) {
      std::scoped_lock lock{stateMutex_};
      const SketchCurveLod requested = SketchCurveLod::forMetresPerLogicalPixel(
          camera_.metresPerLogicalPixel);
      if (!pending_ && desired_ && prepared->stamp().target == *desired_ &&
          prepared->lod() == requested)
        pending_ = prepared;
    }
    return std::unexpected(std::move(transform.error()));
  }

  auto previous = current_.load(std::memory_order_acquire);
  if (previous && previous->products() == prepared &&
      previous->transform().camera() == camera &&
      previous->transform().viewportLogical() == viewportLogical &&
      previous->pickCoverage() == pickCoverage)
    return previous;

  auto synchronized = std::make_shared<const SynchronizedSketchScene>(
      std::move(prepared), std::move(*transform), pickCoverage);
  current_.store(synchronized, std::memory_order_release);
  if (installsPreparedPacket) {
    std::scoped_lock lock{stateMutex_};
    ++synchronizationMetrics_.preparedPacketInstalls;
  }
  return synchronized;
}

Result<SketchItemPickEvidence>
SketchScenePresenter::pick(QPointF itemLogical, double toleranceLogicalPixels,
                           render::SketchPickTargets targets) const {
  return pick(current_.load(std::memory_order_acquire), itemLogical,
              toleranceLogicalPixels, targets);
}

Result<SketchItemPickEvidence>
SketchScenePresenter::pick(std::shared_ptr<const SynchronizedSketchScene> frame,
                           QPointF itemLogical, double toleranceLogicalPixels,
                           render::SketchPickTargets targets) const {
  if (!frame)
    return std::unexpected(diagnostic("desktop.sketch.missing-frame",
                                      "no sketch frame has been synchronized"));
  if (!finite(itemLogical) || !std::isfinite(toleranceLogicalPixels) ||
      toleranceLogicalPixels < 0.0 ||
      toleranceLogicalPixels >
          frame->pickCoverage().maximumToleranceLogicalPixels)
    return std::unexpected(diagnostic("desktop.sketch.invalid-pick",
                                      "sketch item pick is invalid"));
  const render::Point2d canonical = frame->transform().toCanonical(itemLogical);
  const double metresPerLogicalPixel =
      frame->transform().camera().metresPerLogicalPixel;
  const double tolerance = toleranceLogicalPixels * metresPerLogicalPixel;
  if (!finite(canonical) || !std::isfinite(tolerance))
    return std::unexpected(
        diagnostic("desktop.sketch.unrepresentable-pick",
                   "sketch item pick exceeds finite coordinate range"));
  std::optional<render::SketchPickResult> picked;
  std::optional<double> displayedDistanceLogicalPixels;
  render::SketchPickMetrics analyticMetrics;
  DisplayedPickEligibilityContext context;
  if (frame->presentedChunks()) {
    const double deviationLogicalPixels =
        frame->presentedChunks()->maximumAnalyticDeviationMetres() /
        metresPerLogicalPixel;
    const double searchToleranceLogicalPixels =
        toleranceLogicalPixels +
        frame->presentedChunks()->maximumExtrusionLogicalPixels() +
        deviationLogicalPixels;
    const double searchTolerance =
        searchToleranceLogicalPixels * metresPerLogicalPixel;
    auto gpuView = frame->transform().gpuView(frame->mesh()->originMetres());
    if (!std::isfinite(deviationLogicalPixels) ||
        !std::isfinite(searchToleranceLogicalPixels) ||
        !std::isfinite(searchTolerance) || !gpuView)
      return std::unexpected(
          diagnostic("desktop.sketch.unrepresentable-pick",
                     "rendered sketch pick exceeds finite coordinate range"));
    if (auto phase = frame->mesh()->validatePatternedPhase(*gpuView); !phase)
      return std::unexpected(std::move(phase.error()));
    std::array<std::uint32_t,
               render::SketchPickIndex::recommendedQueryStackCapacity>
        nodeStack{};
    context = {frame.get(), itemLogical, *gpuView, toleranceLogicalPixels};
    const render::SketchPickOutcome outcome = frame->pickIndex()->query(
        {canonical, searchTolerance, targets}, {nodeStack},
        {&context, displayedPickEligible});
    switch (outcome.status) {
    case render::SketchPickStatus::Hit:
    case render::SketchPickStatus::Miss:
      picked = outcome.result;
      analyticMetrics = outcome.metrics;
      if (outcome.rankingDistance)
        displayedDistanceLogicalPixels =
            *outcome.rankingDistance / metresPerLogicalPixel;
      break;
    case render::SketchPickStatus::WorkBudgetExceeded:
      return std::unexpected(
          diagnostic("desktop.sketch.rendered-pick-budget",
                     "rendered sketch pick exceeded its bounded workspace"));
    case render::SketchPickStatus::InvalidQuery:
      return std::unexpected(diagnostic("desktop.sketch.invalid-pick",
                                        "rendered sketch pick is invalid"));
    case render::SketchPickStatus::NonFiniteArithmetic:
      return std::unexpected(
          diagnostic("desktop.sketch.unrepresentable-pick",
                     "rendered sketch pick produced non-finite arithmetic"));
    }
  } else {
    const render::SketchPickOutcome outcome =
        frame->pickIndex()->query({canonical, tolerance, targets});
    analyticMetrics = outcome.metrics;
    switch (outcome.status) {
    case render::SketchPickStatus::Hit:
    case render::SketchPickStatus::Miss:
      picked = outcome.result;
      break;
    case render::SketchPickStatus::WorkBudgetExceeded:
      return std::unexpected(diagnostic("render.pick.query-budget",
                                        "pick query exceeded its work budget"));
    case render::SketchPickStatus::InvalidQuery:
      return std::unexpected(diagnostic("render.pick.invalid-query",
                                        "sketch pick query is invalid"));
    case render::SketchPickStatus::NonFiniteArithmetic:
      return std::unexpected(
          diagnostic("render.pick.non-finite-arithmetic",
                     "pick query produced non-finite arithmetic"));
    }
  }
  std::optional<render::SceneStamp> latestAccepted;
  {
    std::scoped_lock lock{stateMutex_};
    latestAccepted = latestAcceptedScene_;
  }
  return SketchItemPickEvidence{frame->scene()->stamp(),
                                frame->products()->stamp(),
                                frame->products()->source(),
                                latestAccepted,
                                frame->transform().camera().generation,
                                frame->transform().viewportLogical(),
                                frame->pickCoverage(),
                                canonical,
                                tolerance,
                                latestAccepted &&
                                    frame->scene()->stamp() == *latestAccepted,
                                std::move(picked),
                                displayedDistanceLogicalPixels,
                                analyticMetrics,
                                context.spanProbes,
                                context.triangleTests,
                                context.patternIntervals,
                                {}};
}

std::shared_ptr<const SynchronizedSketchScene>
SketchScenePresenter::current() const {
  return current_.load(std::memory_order_acquire);
}

namespace {
struct SketchPresenterRetirementOwner {
  std::shared_ptr<const PreparedSketchProducts> pending;
  std::shared_ptr<const SynchronizedSketchScene> current;
};
} // namespace

Result<std::shared_ptr<const void>>
SketchScenePresenter::retirementOwner() const {
  std::scoped_lock lock{stateMutex_};
  auto current = current_.load(std::memory_order_acquire);
  if (!pending_ && !current)
    return std::shared_ptr<const void>{};
  try {
    return std::static_pointer_cast<const void>(
        std::make_shared<const SketchPresenterRetirementOwner>(
            SketchPresenterRetirementOwner{pending_, std::move(current)}));
  } catch (const std::bad_alloc &) {
    return std::unexpected(
        diagnostic("desktop.sketch.retirement-allocation",
                   "sketch presenter retirement allocation failed"));
  }
}

void SketchScenePresenter::clear() {
  std::scoped_lock lock{stateMutex_};
  desired_.reset();
  latestAcceptedScene_.reset();
  pending_.reset();
  current_.store({}, std::memory_order_release);
}

SketchSynchronizationMetrics
SketchScenePresenter::synchronizationMetrics() const {
  std::scoped_lock lock{stateMutex_};
  return synchronizationMetrics_;
}

std::size_t SketchScenePresenter::pendingCount() const {
  std::scoped_lock lock{stateMutex_};
  return pending_ ? 1U : 0U;
}

} // namespace kearne::ui
