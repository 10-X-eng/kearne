#include "sketch_stroke_mesh.hpp"
#include "sketch_projection_support.hpp"
#include "sketch_stroke_mesh_build.hpp"
#include "sketch_stroke_pattern.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <functional>
#include <initializer_list>
#include <limits>
#include <numbers>
#include <numeric>
#include <ranges>
#include <stdexcept>
#include <string>
#include <utility>

namespace kearne::ui {
namespace {

constexpr double fullTurn = 2.0 * std::numbers::pi;
using detail::CancellationPoller;

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

struct PreparationPayloadBudget {
  explicit PreparationPayloadBudget(SketchUploadOptions requestedLimits)
      : limits(requestedLimits) {}

  struct Rejected {
    Diagnostic failure;
  };

  [[nodiscard]] std::size_t bytes(std::size_t count, std::size_t width) const {
    std::size_t result = 0U;
    if (!detail::checkedSizeMultiply(count, width, result))
      reject("desktop.sketch.mesh-byte-overflow",
             "sketch stroke mesh byte accounting overflowed");
    return result;
  }

  template <typename Value>
  [[nodiscard]] std::size_t
  capacityBytes(const std::vector<Value> &values) const {
    return bytes(values.capacity(), sizeof(Value));
  }

  [[nodiscard]] std::size_t
  sum(std::initializer_list<std::size_t> values) const {
    std::size_t result = 0U;
    for (const std::size_t value : values)
      if (!detail::checkedSizeAdd(result, value, result))
        reject("desktop.sketch.mesh-byte-overflow",
               "sketch stroke mesh byte accounting overflowed");
    return result;
  }

  void observe(std::size_t retainedBytes, std::size_t scratchBytes) {
    const std::size_t peakBytes = sum({retainedBytes, scratchBytes});
    if (retainedBytes > limits.maximumRetainedMeshBytes)
      reject("desktop.sketch.mesh-retained-budget",
             "sketch mesh output exceeded its retained byte budget");
    if (scratchBytes > limits.maximumPreparationScratchBytes)
      reject("desktop.sketch.mesh-scratch-budget",
             "sketch mesh exceeded its preparation scratch budget");
    if (peakBytes > limits.maximumPreparationPeakBytes)
      reject("desktop.sketch.mesh-peak-budget",
             "sketch mesh exceeded its preparation peak budget");
    maximumScratchBytes = std::max(maximumScratchBytes, scratchBytes);
    maximumPeakBytes = std::max(maximumPeakBytes, peakBytes);
  }

  [[noreturn]] static void reject(std::string code, std::string message) {
    throw Rejected{diagnostic(std::move(code), std::move(message))};
  }

  SketchUploadOptions limits;
  std::size_t maximumScratchBytes = 0U;
  std::size_t maximumPeakBytes = 0U;
};

struct MeshBuilder {
  MeshBuilder(SketchTessellationOptions requestedOptions,
              CancellationPoller &requestedCancellation,
              PreparationPayloadBudget &requestedBudget)
      : options(requestedOptions), cancellation(&requestedCancellation),
        budget(&requestedBudget) {}

  std::vector<SketchMeshBatch> batches;
  SketchMeshMetrics metrics;
  SketchTessellationOptions options;
  CancellationPoller *cancellation = nullptr;
  PreparationPayloadBudget *budget = nullptr;
  std::size_t retainedFloorBytes = 0U;
  std::size_t fixedScratchBytes = 0U;
  std::size_t payloadBytes = 0U;
  bool exhausted = false;
  bool invalidSegment = false;

  template <typename Value>
  static std::size_t growthCapacity(const std::vector<Value> &values,
                                    std::size_t required, std::size_t ceiling,
                                    std::size_t initialCapacity = 256U) {
    if (required <= values.capacity())
      return values.capacity();
    const std::size_t capacity = values.capacity();
    return capacity == 0U
               ? std::min(std::max(required, initialCapacity), ceiling)
           : capacity <= ceiling - capacity / 2U
               ? std::min(std::max(required, capacity + capacity / 2U), ceiling)
               : ceiling;
  }

  void observe(std::size_t nextPayloadBytes,
               std::size_t temporaryScratchBytes = 0U) const {
    budget->observe(retainedFloorBytes,
                    budget->sum({fixedScratchBytes, nextPayloadBytes,
                                 temporaryScratchBytes}));
  }

  void observeTemporary(std::size_t temporaryScratchBytes) const {
    observe(payloadBytes, temporaryScratchBytes);
  }

  bool allocate(SketchMeshBatch &batch, std::size_t vertices,
                std::size_t indices, std::size_t temporaryScratchBytes = 0U) {
    const std::size_t triangles = indices / 3U;
    if (vertices > options.maximumVertices - metrics.vertices ||
        indices > options.maximumIndices - metrics.indices ||
        batch.vertices.size() >
            std::numeric_limits<std::uint32_t>::max() - vertices ||
        batch.triangleSources.size() >
            options.maximumIndices / 3U - triangles) {
      exhausted = true;
      return false;
    }
    const std::array<std::size_t, 4> prior{
        batch.vertices.capacity(), batch.indices.capacity(),
        batch.triangleSources.capacity(),
        batch.triangleAnalyticDeviationsMetres.capacity()};
    const std::array<std::size_t, 4> requested{
        growthCapacity(batch.vertices, batch.vertices.size() + vertices,
                       options.maximumVertices),
        growthCapacity(batch.indices, batch.indices.size() + indices,
                       options.maximumIndices),
        growthCapacity(batch.triangleSources,
                       batch.triangleSources.size() + triangles,
                       options.maximumIndices / 3U),
        growthCapacity(batch.triangleAnalyticDeviationsMetres,
                       batch.triangleAnalyticDeviationsMetres.size() +
                           triangles,
                       options.maximumIndices / 3U)};
    constexpr std::array<std::size_t, 4> widths{
        sizeof(SketchMeshVertex), sizeof(std::uint32_t), sizeof(std::uint32_t),
        sizeof(double)};
    if (requested != prior) {
      std::size_t projectedPayloadBytes = payloadBytes;
      for (std::size_t index = 0U; index < requested.size(); ++index) {
        const std::size_t delta =
            budget->bytes(requested[index] - prior[index], widths[index]);
        projectedPayloadBytes = budget->sum({projectedPayloadBytes, delta});
      }
      observe(projectedPayloadBytes, temporaryScratchBytes);
      batch.vertices.reserve(requested[0]);
      batch.indices.reserve(requested[1]);
      batch.triangleSources.reserve(requested[2]);
      batch.triangleAnalyticDeviationsMetres.reserve(requested[3]);
      const std::array<std::size_t, 4> actual{
          batch.vertices.capacity(), batch.indices.capacity(),
          batch.triangleSources.capacity(),
          batch.triangleAnalyticDeviationsMetres.capacity()};
      std::size_t actualPayloadBytes = payloadBytes;
      for (std::size_t index = 0U; index < actual.size(); ++index) {
        const std::size_t delta =
            budget->bytes(actual[index] - prior[index], widths[index]);
        actualPayloadBytes = budget->sum({actualPayloadBytes, delta});
      }
      observe(actualPayloadBytes, temporaryScratchBytes);
      payloadBytes = actualPayloadBytes;
    }
    metrics.vertices += vertices;
    metrics.indices += indices;
    return true;
  }

  SketchMeshVertex vertex(QPointF point, QPointF extrusion,
                          double pathDistanceMetres,
                          double coverageDistancePixels,
                          double coverageRadiusPixels,
                          SketchStrokePattern pattern = {}) {
    constexpr double maximum = std::numeric_limits<float>::max();
    if (!finite(point) || !finite(extrusion) ||
        !std::isfinite(pathDistanceMetres) ||
        !std::isfinite(coverageDistancePixels) ||
        !std::isfinite(coverageRadiusPixels) ||
        !std::isfinite(pattern.onLogicalPixels) ||
        !std::isfinite(pattern.periodLogicalPixels) ||
        std::abs(point.x()) > maximum ||
        std::abs(point.y()) > maximum || std::abs(extrusion.x()) > maximum ||
        std::abs(extrusion.y()) > maximum || pathDistanceMetres > maximum ||
        std::abs(coverageDistancePixels) > maximum ||
        coverageRadiusPixels < 0.0 || coverageRadiusPixels > maximum ||
        pattern.onLogicalPixels < 0.0F ||
        pattern.periodLogicalPixels < pattern.onLogicalPixels) {
      exhausted = true;
      return {};
    }
    const float x = static_cast<float>(point.x());
    const float y = static_cast<float>(point.y());
    return {x,
            y,
            static_cast<float>(point.x() - static_cast<double>(x)),
            static_cast<float>(point.y() - static_cast<double>(y)),
            static_cast<float>(extrusion.x()),
            static_cast<float>(extrusion.y()),
            static_cast<float>(pathDistanceMetres),
            static_cast<float>(coverageDistancePixels),
            static_cast<float>(coverageRadiusPixels),
            pattern.onLogicalPixels,
            pattern.periodLogicalPixels};
  }

  void polyline(SketchMeshBatch &batch, std::uint32_t source,
                std::span<const QPointF> points, double width,
                SketchStrokePattern pattern, bool closedShape,
                double chordDeviationMetres,
                std::size_t temporaryScratchBytes = 0U) {
    if (points.size() < 2)
      return;
    for (std::size_t index = 1; index < points.size(); ++index) {
      cancellation->checkpoint();
      const double length =
          std::hypot(points[index].x() - points[index - 1U].x(),
                     points[index].y() - points[index - 1U].y());
      if (!std::isfinite(length) || length <= 0.0) {
        invalidSegment = true;
        return;
      }
    }
    const std::size_t segmentCount = points.size() - 1U;
    if (!allocate(batch, points.size() * 2U, segmentCount * 6U,
                  temporaryScratchBytes))
      return;
    const std::uint32_t base =
        static_cast<std::uint32_t>(batch.vertices.size());
    const auto direction = [&](std::size_t first, std::size_t second) {
      const double dx = points[second].x() - points[first].x();
      const double dy = points[second].y() - points[first].y();
      const double length = std::hypot(dx, dy);
      return QPointF{dx / length, dy / length};
    };
    constexpr double edgeGuardPixels = 1.0;
    const double halfWidth = width * 0.5;
    const double outerHalfWidth = halfWidth + edgeGuardPixels;
    double pathDistance = 0.0;
    for (std::size_t index = 0; index < points.size(); ++index) {
      cancellation->checkpoint();
      if (index != 0U)
        pathDistance += std::hypot(points[index].x() - points[index - 1U].x(),
                                   points[index].y() - points[index - 1U].y());
      const bool first = index == 0;
      const bool last = index + 1U == points.size();
      const QPointF previousDirection = first && closedShape
                                            ? direction(points.size() - 2U, 0)
                                        : first ? direction(0, 1)
                                                : direction(index - 1U, index);
      const QPointF nextDirection =
          last && closedShape ? direction(points.size() - 1U, 1U)
          : last ? direction(points.size() - 2U, points.size() - 1U)
                 : direction(index, index + 1U);
      const QPointF previousNormal{-previousDirection.y(),
                                   previousDirection.x()};
      const QPointF nextNormal{-nextDirection.y(), nextDirection.x()};
      QPointF miter{previousNormal.x() + nextNormal.x(),
                    previousNormal.y() + nextNormal.y()};
      const double miterLength = std::hypot(miter.x(), miter.y());
      double offset = outerHalfWidth;
      if (miterLength > 1.0e-12) {
        miter /= miterLength;
        const double denominator =
            miter.x() * nextNormal.x() + miter.y() * nextNormal.y();
        offset = std::clamp(outerHalfWidth / std::max(denominator, 0.25),
                            outerHalfWidth, outerHalfWidth * 4.0);
      } else {
        miter = nextNormal;
      }
      const QPointF extrusion = miter * offset;
      batch.vertices.push_back(vertex(points[index], extrusion, pathDistance,
                                      outerHalfWidth, halfWidth, pattern));
      batch.vertices.push_back(vertex(points[index], -extrusion, pathDistance,
                                      -outerHalfWidth, halfWidth, pattern));
    }
    for (std::size_t index = 0; index < segmentCount; ++index) {
      cancellation->checkpoint();
      const std::uint32_t first = base + static_cast<std::uint32_t>(index * 2U);
      const std::uint32_t second =
          base + static_cast<std::uint32_t>((index + 1U) * 2U);
      const auto packingError = [&](std::size_t point) {
        const SketchMeshVertex &packed = batch.vertices[base + point * 2U];
        return std::hypot(
            static_cast<double>(packed.x) + packed.xLow - points[point].x(),
            static_cast<double>(packed.y) + packed.yLow - points[point].y());
      };
      const double maximumPackingError =
          std::max(packingError(index), packingError(index + 1U));
      if (!std::isfinite(chordDeviationMetres) ||
          !std::isfinite(maximumPackingError) || chordDeviationMetres < 0.0 ||
          maximumPackingError >
              std::numeric_limits<double>::max() - chordDeviationMetres) {
        exhausted = true;
        return;
      }
      const double analyticDeviation =
          std::nextafter(chordDeviationMetres + maximumPackingError,
                         std::numeric_limits<double>::infinity());
      batch.indices.insert(
          batch.indices.end(),
          {first, first + 1U, second, second, first + 1U, second + 1U});
      batch.triangleSources.push_back(source);
      batch.triangleSources.push_back(source);
      batch.triangleAnalyticDeviationsMetres.push_back(analyticDeviation);
      batch.triangleAnalyticDeviationsMetres.push_back(analyticDeviation);
    }
  }

  void point(SketchMeshBatch &batch, std::uint32_t source, QPointF center,
             double diameter) {
    constexpr std::size_t segments = 12;
    if (!allocate(batch, segments + 1U, segments * 3U))
      return;
    const std::uint32_t base =
        static_cast<std::uint32_t>(batch.vertices.size());
    const double radius = diameter * 0.5;
    constexpr double edgeGuardPixels = 1.0;
    const double outerRadius = radius + edgeGuardPixels;
    batch.vertices.push_back(vertex(center, {}, 0.0, 0.0, radius));
    const SketchMeshVertex &packedCenter = batch.vertices.back();
    const double analyticDeviation =
        std::nextafter(std::hypot(static_cast<double>(packedCenter.x) +
                                      packedCenter.xLow - center.x(),
                                  static_cast<double>(packedCenter.y) +
                                      packedCenter.yLow - center.y()),
                       std::numeric_limits<double>::infinity());
    for (std::size_t index = 0; index < segments; ++index) {
      cancellation->checkpoint();
      const double angle =
          fullTurn * static_cast<double>(index) / static_cast<double>(segments);
      batch.vertices.push_back(
          vertex(center,
                 {outerRadius * std::cos(angle), outerRadius * std::sin(angle)},
                 0.0, outerRadius, radius));
    }
    for (std::size_t index = 0; index < segments; ++index) {
      cancellation->checkpoint();
      batch.indices.insert(
          batch.indices.end(),
          {base, base + 1U + static_cast<std::uint32_t>(index),
           base + 1U + static_cast<std::uint32_t>((index + 1U) % segments)});
      batch.triangleSources.push_back(source);
      batch.triangleAnalyticDeviationsMetres.push_back(analyticDeviation);
    }
  }

  void screenLine(SketchMeshBatch &batch, std::uint32_t source, QPointF center,
                  QPointF first, QPointF second, double width) {
    const QPointF direction = second - first;
    const double length = std::hypot(direction.x(), direction.y());
    if (!std::isfinite(length) || length <= 0.0) {
      invalidSegment = true;
      return;
    }
    if (!allocate(batch, 4U, 6U))
      return;
    constexpr double edgeGuardPixels = 1.0;
    const double halfWidth = width * 0.5;
    const double outerHalfWidth = halfWidth + edgeGuardPixels;
    const QPointF normal{-direction.y() / length * outerHalfWidth,
                         direction.x() / length * outerHalfWidth};
    const std::uint32_t base =
        static_cast<std::uint32_t>(batch.vertices.size());
    batch.vertices.push_back(
        vertex(center, first + normal, 0.0, outerHalfWidth, halfWidth));
    batch.vertices.push_back(
        vertex(center, first - normal, 0.0, -outerHalfWidth, halfWidth));
    batch.vertices.push_back(
        vertex(center, second + normal, 0.0, outerHalfWidth, halfWidth));
    batch.vertices.push_back(
        vertex(center, second - normal, 0.0, -outerHalfWidth, halfWidth));
    batch.indices.insert(
        batch.indices.end(),
        {base, base + 1U, base + 2U, base + 2U, base + 1U, base + 3U});
    const SketchMeshVertex &packedCenter = batch.vertices[base];
    const double analyticDeviation =
        std::nextafter(std::hypot(static_cast<double>(packedCenter.x) +
                                      packedCenter.xLow - center.x(),
                                  static_cast<double>(packedCenter.y) +
                                      packedCenter.yLow - center.y()),
                       std::numeric_limits<double>::infinity());
    batch.triangleSources.push_back(source);
    batch.triangleSources.push_back(source);
    batch.triangleAnalyticDeviationsMetres.push_back(analyticDeviation);
    batch.triangleAnalyticDeviationsMetres.push_back(analyticDeviation);
  }

  void glyph(SketchMeshBatch &batch, std::uint32_t source, QPointF center,
             std::uint16_t glyphCode, double diameter) {
    const double radius = diameter * 0.5;
    const double width = std::max(1.25, diameter * 0.12);
    const auto line = [&](double x1, double y1, double x2, double y2) {
      screenLine(batch, source, center, {x1 * radius, y1 * radius},
                 {x2 * radius, y2 * radius}, width);
    };
    const auto diamond = [&](double scale) {
      line(0.0, -scale, scale, 0.0);
      line(scale, 0.0, 0.0, scale);
      line(0.0, scale, -scale, 0.0);
      line(-scale, 0.0, 0.0, -scale);
    };
    using Kind = render::SketchMarkerKind;
    switch (static_cast<Kind>(glyphCode)) {
    case Kind::HorizontalConstraint:
    case Kind::HorizontalInference:
    case Kind::HorizontalDistanceDimension:
      line(-1.0, 0.0, 1.0, 0.0);
      line(-0.75, -0.45, -0.75, 0.45);
      line(0.75, -0.45, 0.75, 0.45);
      break;
    case Kind::VerticalConstraint:
    case Kind::VerticalInference:
    case Kind::VerticalDistanceDimension:
      line(0.0, -1.0, 0.0, 1.0);
      line(-0.45, -0.75, 0.45, -0.75);
      line(-0.45, 0.75, 0.45, 0.75);
      break;
    case Kind::ParallelConstraint:
    case Kind::ParallelInference:
      line(-0.8, 0.6, 0.2, -0.8);
      line(-0.15, 0.8, 0.85, -0.6);
      break;
    case Kind::PerpendicularConstraint:
    case Kind::PerpendicularInference:
      line(-0.8, -0.8, -0.8, 0.65);
      line(-0.8, 0.65, 0.8, 0.65);
      break;
    case Kind::TangentConstraint:
    case Kind::TangentInference:
      line(-1.0, 0.55, 1.0, 0.55);
      line(-0.75, 0.2, -0.35, -0.45);
      line(-0.35, -0.45, 0.35, -0.45);
      line(0.35, -0.45, 0.75, 0.2);
      break;
    case Kind::EqualConstraint:
      line(-0.8, -0.35, 0.8, -0.35);
      line(-0.8, 0.35, 0.8, 0.35);
      break;
    case Kind::ConcentricConstraint:
      diamond(1.0);
      diamond(0.48);
      break;
    case Kind::MidpointConstraint:
    case Kind::MidpointSnap:
      line(0.0, -0.9, 0.9, 0.7);
      line(0.9, 0.7, -0.9, 0.7);
      line(-0.9, 0.7, 0.0, -0.9);
      break;
    case Kind::FixedConstraint:
      diamond(0.85);
      line(-0.55, -0.55, 0.55, 0.55);
      line(-0.55, 0.55, 0.55, -0.55);
      break;
    case Kind::CollinearConstraint:
    case Kind::CollinearInference:
      line(-1.0, 0.0, 1.0, 0.0);
      line(-0.55, -0.28, -0.55, 0.28);
      line(0.55, -0.28, 0.55, 0.28);
      break;
    case Kind::RotationDegreeOfFreedom:
    case Kind::AngleDimension:
      diamond(0.8);
      line(0.0, 0.0, 0.85, -0.35);
      break;
    case Kind::DistanceDimension:
      line(-1.0, 0.0, 1.0, 0.0);
      line(-1.0, 0.0, -0.55, -0.35);
      line(-1.0, 0.0, -0.55, 0.35);
      line(1.0, 0.0, 0.55, -0.35);
      line(1.0, 0.0, 0.55, 0.35);
      break;
    case Kind::RadiusDimension:
      line(-0.8, 0.8, 0.8, -0.8);
      line(0.8, -0.8, 0.25, -0.65);
      line(0.8, -0.8, 0.65, -0.25);
      break;
    case Kind::DiameterDimension:
      diamond(0.9);
      line(-0.7, 0.7, 0.7, -0.7);
      break;
    case Kind::TranslationDegreeOfFreedom:
    case Kind::IntersectionSnap:
      line(-1.0, 0.0, 1.0, 0.0);
      line(0.0, -1.0, 0.0, 1.0);
      break;
    case Kind::CenterSnap:
    case Kind::QuadrantSnap:
      diamond(0.9);
      line(-0.35, 0.0, 0.35, 0.0);
      line(0.0, -0.35, 0.0, 0.35);
      break;
    case Kind::GridSnap:
      line(-0.8, -0.8, -0.8, 0.8);
      line(0.0, -0.8, 0.0, 0.8);
      line(0.8, -0.8, 0.8, 0.8);
      line(-0.8, -0.8, 0.8, -0.8);
      line(-0.8, 0.0, 0.8, 0.0);
      line(-0.8, 0.8, 0.8, 0.8);
      break;
    case Kind::CoincidentConstraint:
    case Kind::EndpointSnap:
    default:
      line(-0.85, -0.85, 0.85, 0.85);
      line(-0.85, 0.85, 0.85, -0.85);
      break;
    }
  }
};

std::size_t curveSegments(double radius, double sweep, SketchCurveLod lod,
                          const SketchTessellationOptions &options) {
  const double proportionalMinimum =
      static_cast<double>(options.minimumCircleSegments) * std::abs(sweep) /
      fullTurn;
  const double relativeError =
      std::clamp(lod.maximumChordErrorMetres() / radius, 0.0, 2.0);
  const double chordStep = 2.0 * std::acos(1.0 - relativeError);
  const double angularStep =
      chordStep > 0.0 ? std::min(options.maximumArcStepRadians, chordStep)
                      : options.maximumArcStepRadians;
  const double byAngle = std::abs(sweep) / angularStep;
  const double requested =
      std::ceil(std::max(2.0, std::max(proportionalMinimum, byAngle)));
  return static_cast<std::size_t>(
      std::min(requested, static_cast<double>(options.maximumCurveSegments)));
}

render::Point2d conicPoint(const SketchStrokeSourcePrimitive &primitive,
                           double parameter) {
  const double cosine = std::cos(primitive.rotationAngleRadians);
  const double sine = std::sin(primitive.rotationAngleRadians);
  double localX = 0.0;
  double localY = parameter;
  if (primitive.kind == SketchStrokeSourceKind::HyperbolicArc) {
    localX = primitive.radius * std::cosh(parameter);
    localY = primitive.secondaryRadius * std::sinh(parameter);
  } else {
    localX = parameter * parameter / (4.0 * primitive.radius);
  }
  return {primitive.first.x + cosine * localX - sine * localY,
          primitive.first.y + sine * localX + cosine * localY};
}

double
conicMaximumSecondDerivative(const SketchStrokeSourcePrimitive &primitive) {
  if (primitive.kind == SketchStrokeSourceKind::ParabolicArc)
    return 1.0 / (2.0 * primitive.radius);
  const double end = primitive.startAngleRadians + primitive.sweepAngleRadians;
  const double parameter =
      std::max(std::abs(primitive.startAngleRadians), std::abs(end));
  return std::hypot(primitive.radius * std::cosh(parameter),
                    primitive.secondaryRadius * std::sinh(parameter));
}

std::size_t conicSegments(const SketchStrokeSourcePrimitive &primitive,
                          SketchCurveLod lod,
                          const SketchTessellationOptions &options) {
  const double sweep = std::abs(primitive.sweepAngleRadians);
  const double secondDerivative = conicMaximumSecondDerivative(primitive);
  const double byError =
      sweep *
      std::sqrt(secondDerivative / (8.0 * lod.maximumChordErrorMetres()));
  const double byStep = primitive.kind == SketchStrokeSourceKind::HyperbolicArc
                            ? sweep / options.maximumArcStepRadians
                            : 0.0;
  const double requested = std::ceil(std::max({2.0, byError, byStep}));
  return static_cast<std::size_t>(
      std::min(requested, static_cast<double>(options.maximumCurveSegments)));
}

double conicDeviation(const SketchStrokeSourcePrimitive &primitive,
                      std::size_t segments) {
  const long double step =
      std::abs(static_cast<long double>(primitive.sweepAngleRadians)) /
      static_cast<long double>(segments);
  const long double deviation =
      static_cast<long double>(conicMaximumSecondDerivative(primitive)) * step *
      step / 8.0L;
  return deviation > std::numeric_limits<double>::max()
             ? std::numeric_limits<double>::infinity()
             : std::nextafter(static_cast<double>(deviation),
                              std::numeric_limits<double>::infinity());
}

Result<void> validateOptions(const SketchTessellationOptions &options) {
  if (!std::isfinite(options.maximumArcStepRadians) ||
      options.maximumArcStepRadians <= 0.0 ||
      options.maximumArcStepRadians > fullTurn ||
      options.minimumCircleSegments < 3U ||
      options.maximumCurveSegments < options.minimumCircleSegments ||
      options.maximumVertices == 0U || options.maximumIndices == 0U ||
      options.maximumVertices > std::numeric_limits<std::uint32_t>::max() ||
      options.maximumIndices > std::numeric_limits<std::uint32_t>::max())
    return std::unexpected(
        diagnostic("desktop.sketch.invalid-tessellation-options",
                   "sketch tessellation options are invalid"));
  return {};
}

SketchChunkBounds merged(SketchChunkBounds first,
                         const SketchChunkBounds &second) {
  if (first.empty)
    return second;
  if (second.empty)
    return first;
  first.minimumX = std::min(first.minimumX, second.minimumX);
  first.minimumY = std::min(first.minimumY, second.minimumY);
  first.maximumX = std::max(first.maximumX, second.maximumX);
  first.maximumY = std::max(first.maximumY, second.maximumY);
  first.maximumExtrusionLogicalPixels =
      std::max(first.maximumExtrusionLogicalPixels,
               second.maximumExtrusionLogicalPixels);
  first.maximumAnalyticDeviationMetres =
      std::max(first.maximumAnalyticDeviationMetres,
               second.maximumAnalyticDeviationMetres);
  first.maximumPathDistanceMetres = std::max(first.maximumPathDistanceMetres,
                                             second.maximumPathDistanceMetres);
  first.maximumPatternedPathDistanceMetres =
      std::max(first.maximumPatternedPathDistanceMetres,
               second.maximumPatternedPathDistanceMetres);
  return first;
}

void include(SketchChunkBounds &bounds, const SketchMeshVertex &vertex) {
  const double x = static_cast<double>(vertex.x) + vertex.xLow;
  const double y = static_cast<double>(vertex.y) + vertex.yLow;
  const double extrusion = std::hypot(static_cast<double>(vertex.extrusionX),
                                      static_cast<double>(vertex.extrusionY));
  const double conservativeExtrusion =
      std::nextafter(extrusion, std::numeric_limits<double>::infinity());
  if (bounds.empty) {
    bounds = {x, y, x, y, false};
    bounds.maximumExtrusionLogicalPixels = conservativeExtrusion;
    bounds.maximumPathDistanceMetres = vertex.pathDistanceMetres;
    return;
  }
  bounds.minimumX = std::min(bounds.minimumX, x);
  bounds.minimumY = std::min(bounds.minimumY, y);
  bounds.maximumX = std::max(bounds.maximumX, x);
  bounds.maximumY = std::max(bounds.maximumY, y);
  bounds.maximumExtrusionLogicalPixels =
      std::max(bounds.maximumExtrusionLogicalPixels, conservativeExtrusion);
  bounds.maximumPathDistanceMetres =
      std::max(bounds.maximumPathDistanceMetres,
               static_cast<double>(vertex.pathDistanceMetres));
}
void hashWord(std::uint64_t &hash, std::uint64_t value) {
  constexpr std::uint64_t prime = 1'099'511'628'211ULL;
  for (std::size_t byte = 0; byte < sizeof(value); ++byte) {
    hash ^= (value >> (byte * 8U)) & 0xffU;
    hash *= prime;
  }
}

std::uint64_t primitiveFingerprint(const SketchStrokeSourcePrimitive &primitive,
                                   sketch::NurbsView spline = {}) {
  std::uint64_t hash = 14'695'981'039'346'656'037ULL;
  hashWord(hash, primitive.sourceKey);
  hashWord(hash, primitive.style);
  hashWord(hash, static_cast<std::uint8_t>(primitive.kind));
  hashWord(hash, primitive.visible ? 1U : 0U);
  hashWord(hash, std::bit_cast<std::uint64_t>(primitive.first.x));
  hashWord(hash, std::bit_cast<std::uint64_t>(primitive.first.y));
  hashWord(hash, std::bit_cast<std::uint64_t>(primitive.second.x));
  hashWord(hash, std::bit_cast<std::uint64_t>(primitive.second.y));
  hashWord(hash, std::bit_cast<std::uint64_t>(primitive.radius));
  hashWord(hash, std::bit_cast<std::uint64_t>(primitive.startAngleRadians));
  hashWord(hash, std::bit_cast<std::uint64_t>(primitive.sweepAngleRadians));
  hashWord(hash, std::bit_cast<std::uint64_t>(primitive.secondaryRadius));
  hashWord(hash, std::bit_cast<std::uint64_t>(primitive.rotationAngleRadians));
  hashWord(hash, primitive.glyph);
  if (primitive.kind == SketchStrokeSourceKind::BSpline) {
    hashWord(hash, spline.degree);
    hashWord(hash, spline.controlPointCoordinates.size());
    for (double value : spline.controlPointCoordinates)
      hashWord(hash, std::bit_cast<std::uint64_t>(value));
    hashWord(hash, spline.knots.size());
    for (double value : spline.knots)
      hashWord(hash, std::bit_cast<std::uint64_t>(value));
    hashWord(hash, spline.weights.size());
    for (double value : spline.weights)
      hashWord(hash, std::bit_cast<std::uint64_t>(value));
  }
  return hash;
}

std::uint64_t chunkHash(std::uint16_t style, std::uint16_t layer,
                        std::span<const SketchMeshVertex> vertices,
                        std::span<const std::uint32_t> indices,
                        const SketchChunkBounds &bounds,
                        CancellationPoller &cancellation) {
  std::uint64_t hash = 14'695'981'039'346'656'037ULL;
  hashWord(hash, style);
  hashWord(hash, layer);
  hashWord(hash,
           std::bit_cast<std::uint64_t>(bounds.maximumAnalyticDeviationMetres));
  hashWord(hash, std::bit_cast<std::uint64_t>(
                     bounds.maximumPatternedPathDistanceMetres));
  hashWord(hash, vertices.size());
  for (const SketchMeshVertex &vertex : vertices) {
    cancellation.checkpoint();
    hashWord(hash, std::bit_cast<std::uint32_t>(vertex.x));
    hashWord(hash, std::bit_cast<std::uint32_t>(vertex.y));
    hashWord(hash, std::bit_cast<std::uint32_t>(vertex.xLow));
    hashWord(hash, std::bit_cast<std::uint32_t>(vertex.yLow));
    hashWord(hash, std::bit_cast<std::uint32_t>(vertex.extrusionX));
    hashWord(hash, std::bit_cast<std::uint32_t>(vertex.extrusionY));
    hashWord(hash, std::bit_cast<std::uint32_t>(vertex.pathDistanceMetres));
    hashWord(hash,
             std::bit_cast<std::uint32_t>(vertex.patternOnLogicalPixels));
    hashWord(hash,
             std::bit_cast<std::uint32_t>(vertex.patternPeriodLogicalPixels));
  }
  hashWord(hash, indices.size());
  for (const std::uint32_t index : indices) {
    cancellation.checkpoint();
    hashWord(hash, index);
  }
  return hash;
}

bool validCoverage(SketchPickCoveragePolicy policy) {
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

void includePoint(SketchChunkBounds &bounds, double x, double y) {
  if (bounds.empty) {
    bounds = {x, y, x, y, false};
    return;
  }
  bounds.minimumX = std::min(bounds.minimumX, x);
  bounds.minimumY = std::min(bounds.minimumY, y);
  bounds.maximumX = std::max(bounds.maximumX, x);
  bounds.maximumY = std::max(bounds.maximumY, y);
}

Result<SketchChunkBounds>
viewportBounds(render::Point2d originMetres,
               const SketchViewTransform &transform,
               SketchPickCoveragePolicy pickCoverage) {
  if (!validCoverage(pickCoverage))
    return std::unexpected(
        diagnostic("desktop.sketch.invalid-pick-coverage",
                   "sketch pick coverage policy is invalid"));
  SketchChunkBounds visible;
  const QSizeF viewport = transform.viewportLogical();
  for (const QPointF corner :
       std::array{QPointF{0.0, 0.0}, QPointF{viewport.width(), 0.0},
                  QPointF{0.0, viewport.height()},
                  QPointF{viewport.width(), viewport.height()}}) {
    const render::Point2d canonical = transform.toCanonical(corner);
    const double x = canonical.x - originMetres.x;
    const double y = canonical.y - originMetres.y;
    if (!std::isfinite(x) || !std::isfinite(y))
      return std::unexpected(
          diagnostic("desktop.sketch.unrepresentable-visible-bounds",
                     "sketch visible bounds exceed finite coordinate range"));
    includePoint(visible, x, y);
  }
  const double margin = pickCoverage.maximumToleranceLogicalPixels *
                        transform.camera().metresPerLogicalPixel;
  if (!std::isfinite(margin))
    return std::unexpected(
        diagnostic("desktop.sketch.unrepresentable-visible-bounds",
                   "sketch visible margin exceeds finite coordinate range"));
  visible.minimumX -= margin;
  visible.minimumY -= margin;
  visible.maximumX += margin;
  visible.maximumY += margin;
  if (!std::isfinite(visible.minimumX) || !std::isfinite(visible.minimumY) ||
      !std::isfinite(visible.maximumX) || !std::isfinite(visible.maximumY))
    return std::unexpected(
        diagnostic("desktop.sketch.unrepresentable-visible-bounds",
                   "sketch expanded visible bounds exceed finite range"));
  return visible;
}

} // namespace

Result<SketchStrokeSourceBounds>
sketchStrokePrimitiveBounds(const SketchStrokeSourcePrimitive &primitive) {
  return sketchStrokePrimitiveBounds(primitive, {});
}

Result<SketchStrokeSourceBounds>
sketchStrokePrimitiveBounds(const SketchStrokeSourcePrimitive &primitive,
                            sketch::NurbsView spline) {
  SketchStrokeSourceBounds result;
  const auto include = [&result](render::Point2d point) {
    if (!finite(point))
      return false;
    if (result.empty) {
      result = {point, point, false};
      return true;
    }
    result.minimum.x = std::min(result.minimum.x, point.x);
    result.minimum.y = std::min(result.minimum.y, point.y);
    result.maximum.x = std::max(result.maximum.x, point.x);
    result.maximum.y = std::max(result.maximum.y, point.y);
    return true;
  };
  const auto positiveAngle = [](double angle) {
    angle = std::fmod(angle, fullTurn);
    return angle < 0.0 ? angle + fullTurn : angle;
  };
  const auto onSweep = [&](double parameter) {
    return primitive.sweepAngleRadians > 0.0
               ? positiveAngle(parameter - primitive.startAngleRadians) <=
                     primitive.sweepAngleRadians
               : positiveAngle(primitive.startAngleRadians - parameter) <=
                     -primitive.sweepAngleRadians;
  };
  const auto rotatedPoint = [&](double localX, double localY) {
    const double cosine = std::cos(primitive.rotationAngleRadians);
    const double sine = std::sin(primitive.rotationAngleRadians);
    return render::Point2d{primitive.first.x + cosine * localX - sine * localY,
                           primitive.first.y + sine * localX + cosine * localY};
  };
  switch (primitive.kind) {
  case SketchStrokeSourceKind::Point:
  case SketchStrokeSourceKind::Glyph:
    static_cast<void>(include(primitive.first));
    break;
  case SketchStrokeSourceKind::Line:
    static_cast<void>(include(primitive.first));
    static_cast<void>(include(primitive.second));
    break;
  case SketchStrokeSourceKind::Circle:
    static_cast<void>(include({primitive.first.x - primitive.radius,
                               primitive.first.y - primitive.radius}));
    static_cast<void>(include({primitive.first.x + primitive.radius,
                               primitive.first.y + primitive.radius}));
    break;
  case SketchStrokeSourceKind::Arc: {
    const auto radial = [&](double angle) {
      return render::Point2d{
          primitive.first.x + primitive.radius * std::cos(angle),
          primitive.first.y + primitive.radius * std::sin(angle)};
    };
    static_cast<void>(include(radial(primitive.startAngleRadians)));
    static_cast<void>(include(
        radial(primitive.startAngleRadians + primitive.sweepAngleRadians)));
    for (const double cardinal : {0.0, std::numbers::pi / 2.0, std::numbers::pi,
                                  3.0 * std::numbers::pi / 2.0})
      if (onSweep(cardinal))
        static_cast<void>(include(radial(cardinal)));
    break;
  }
  case SketchStrokeSourceKind::Ellipse: {
    const double cosine = std::cos(primitive.rotationAngleRadians);
    const double sine = std::sin(primitive.rotationAngleRadians);
    const double extentX =
        std::hypot(primitive.radius * cosine, primitive.secondaryRadius * sine);
    const double extentY =
        std::hypot(primitive.radius * sine, primitive.secondaryRadius * cosine);
    static_cast<void>(
        include({primitive.first.x - extentX, primitive.first.y - extentY}));
    static_cast<void>(
        include({primitive.first.x + extentX, primitive.first.y + extentY}));
    break;
  }
  case SketchStrokeSourceKind::EllipticalArc: {
    const auto pointAt = [&](double parameter) {
      return rotatedPoint(primitive.radius * std::cos(parameter),
                          primitive.secondaryRadius * std::sin(parameter));
    };
    static_cast<void>(include(pointAt(primitive.startAngleRadians)));
    static_cast<void>(include(
        pointAt(primitive.startAngleRadians + primitive.sweepAngleRadians)));
    const double rotation = primitive.rotationAngleRadians;
    const std::array extrema{
        std::atan2(-primitive.secondaryRadius * std::sin(rotation),
                   primitive.radius * std::cos(rotation)),
        std::atan2(-primitive.secondaryRadius * std::sin(rotation),
                   primitive.radius * std::cos(rotation)) +
            std::numbers::pi,
        std::atan2(primitive.secondaryRadius * std::cos(rotation),
                   primitive.radius * std::sin(rotation)),
        std::atan2(primitive.secondaryRadius * std::cos(rotation),
                   primitive.radius * std::sin(rotation)) +
            std::numbers::pi};
    for (const double parameter : extrema)
      if (onSweep(parameter))
        static_cast<void>(include(pointAt(parameter)));
    break;
  }
  case SketchStrokeSourceKind::HyperbolicArc:
  case SketchStrokeSourceKind::ParabolicArc: {
    const double start = primitive.startAngleRadians;
    const double end = start + primitive.sweepAngleRadians;
    const auto onRange = [&](double parameter) {
      return std::isfinite(parameter) && parameter >= std::min(start, end) &&
             parameter <= std::max(start, end);
    };
    static_cast<void>(include(conicPoint(primitive, start)));
    static_cast<void>(include(conicPoint(primitive, end)));
    const auto includeParameter = [&](double parameter) {
      if (onRange(parameter))
        static_cast<void>(include(conicPoint(primitive, parameter)));
    };
    const double cosine = std::cos(primitive.rotationAngleRadians);
    const double sine = std::sin(primitive.rotationAngleRadians);
    if (primitive.kind == SketchStrokeSourceKind::HyperbolicArc) {
      const double xRatio =
          sine * primitive.secondaryRadius / (cosine * primitive.radius);
      const double yRatio =
          -cosine * primitive.secondaryRadius / (sine * primitive.radius);
      if (std::abs(xRatio) < 1.0)
        includeParameter(std::atanh(xRatio));
      if (std::abs(yRatio) < 1.0)
        includeParameter(std::atanh(yRatio));
    } else {
      if (cosine != 0.0)
        includeParameter(2.0 * primitive.radius * sine / cosine);
      if (sine != 0.0)
        includeParameter(-2.0 * primitive.radius * cosine / sine);
    }
    break;
  }
  case SketchStrokeSourceKind::BSpline:
    if (spline.controlPointCoordinates.size() != spline.weights.size() * 2U)
      break;
    for (std::size_t index = 0U; index < spline.weights.size(); ++index)
      static_cast<void>(
          include({spline.controlPointCoordinates[index * 2U],
                   spline.controlPointCoordinates[index * 2U + 1U]}));
    break;
  }
  if (result.empty || !finite(result.minimum) || !finite(result.maximum))
    return std::unexpected(
        diagnostic("desktop.sketch.unrepresentable-primitive-bounds",
                   "sketch primitive bounds are not finite"));
  return result;
}

bool SketchChunkBounds::intersects(const SketchChunkBounds &other,
                                   double extrusionMetres) const {
  if (empty || other.empty || !std::isfinite(extrusionMetres) ||
      extrusionMetres < 0.0)
    return false;
  const long double margin = extrusionMetres;
  return static_cast<long double>(maximumX) + margin >= other.minimumX &&
         other.maximumX + margin >= static_cast<long double>(minimumX) &&
         static_cast<long double>(maximumY) + margin >= other.minimumY &&
         other.maximumY + margin >= static_cast<long double>(minimumY);
}

SketchUploadChunk::SketchUploadChunk(std::uint16_t style, std::uint16_t layer,
                                     std::vector<SketchMeshVertex> vertices,
                                     std::vector<std::uint32_t> indices,
                                     SketchChunkBounds bounds,
                                     std::size_t payloadBytes,
                                     std::uint64_t contentHash)
    : style_(style), layer_(layer), vertices_(std::move(vertices)),
      indices_(std::move(indices)), bounds_(bounds),
      payloadBytes_(payloadBytes), contentHash_(contentHash) {}

SketchSceneMesh::SketchSceneMesh(
    render::Point2d originMetres, SketchCurveLod lod,
    std::vector<render::SketchStyle> styles,
    std::vector<std::shared_ptr<const SketchUploadChunk>> chunks,
    std::vector<SpatialNode> spatialIndex, std::uint32_t spatialRoot,
    std::size_t maximumChunkBytes, double spatialTileSizeMetres,
    SketchMeshMetrics metrics)
    : originMetres_(originMetres), lod_(lod), styles_(std::move(styles)),
      chunks_(std::move(chunks)), spatialIndex_(std::move(spatialIndex)),
      spatialRoot_(spatialRoot), maximumChunkBytes_(maximumChunkBytes),
      spatialTileSizeMetres_(spatialTileSizeMetres), metrics_(metrics) {}

SketchCurveLod
SketchCurveLod::forMetresPerLogicalPixel(double metresPerLogicalPixel) {
  constexpr int minimumExponent = -120;
  constexpr int maximumExponent = 120;
  return {std::clamp(std::ilogb(metresPerLogicalPixel), minimumExponent,
                     maximumExponent)};
}

double SketchCurveLod::maximumChordErrorMetres() const {
  return std::scalbn(0.25, scaleExponent);
}

Result<SketchVisibleChunkSelection>
SketchSceneMesh::visibleChunks(const SketchViewTransform &transform,
                               SketchPickCoveragePolicy pickCoverage) const {
  auto visible = viewportBounds(originMetres_, transform, pickCoverage);
  if (!visible)
    return std::unexpected(std::move(visible.error()));
  if (chunks_.empty())
    return SketchVisibleChunkSelection{};
  try {
    std::vector<std::uint32_t> selected;
    std::vector<std::uint32_t> stack{spatialRoot_};
    std::size_t nodesVisited = 0U;
    while (!stack.empty()) {
      const std::uint32_t index = stack.back();
      stack.pop_back();
      ++nodesVisited;
      const SpatialNode &node = spatialIndex_[index];
      const double extrusionMetres = node.bounds.maximumExtrusionLogicalPixels *
                                     transform.camera().metresPerLogicalPixel;
      if (!std::isfinite(extrusionMetres))
        return std::unexpected(diagnostic(
            "desktop.sketch.unrepresentable-visible-bounds",
            "sketch chunk extrusion exceeds finite coordinate range"));
      if (!node.bounds.intersects(*visible, extrusionMetres))
        continue;
      if (node.leaf) {
        selected.push_back(node.first);
      } else {
        stack.push_back(node.second);
        stack.push_back(node.first);
      }
    }
    std::ranges::sort(selected);
    return SketchVisibleChunkSelection{std::move(selected), nodesVisited};
  } catch (const std::bad_alloc &) {
    return std::unexpected(diagnostic("desktop.sketch.visibility-allocation",
                                      "sketch visibility allocation failed"));
  } catch (const std::length_error &) {
    return std::unexpected(
        diagnostic("desktop.sketch.visibility-budget",
                   "sketch visibility exceeded container capacity"));
  }
}

Result<void>
SketchSceneMesh::validatePatternedPhase(const SketchGpuView &view) const {
  if (chunks_.empty())
    return {};
  const float pathDistance = static_cast<float>(
      spatialIndex_[spatialRoot_].bounds.maximumPatternedPathDistanceMetres);
  if (pathDistance <= 0.0F)
    return {};
  const float logicalPath = pathDistance / view.metresPerLogicalPixel;
  const float phaseUlp =
      std::nextafter(logicalPath, std::numeric_limits<float>::infinity()) -
      logicalPath;
  constexpr float maximumPhaseUlpLogicalPixels = 0.25F;
  if (!std::isfinite(logicalPath) || !std::isfinite(phaseUlp) ||
      phaseUlp > maximumPhaseUlpLogicalPixels)
    return std::unexpected(
        diagnostic("desktop.sketch.unrepresentable-pattern-phase",
                   "patterned sketch phase exceeds shader float precision"));
  return {};
}

SketchPresentedChunkCoverage::SketchPresentedChunkCoverage(
    std::vector<std::uint64_t> membershipWords, std::size_t size,
    double maximumExtrusionLogicalPixels, double maximumAnalyticDeviationMetres,
    double maximumPatternedPathDistanceMetres, std::size_t retainedBytes)
    : membershipWords_(std::move(membershipWords)), size_(size),
      maximumExtrusionLogicalPixels_(maximumExtrusionLogicalPixels),
      maximumAnalyticDeviationMetres_(maximumAnalyticDeviationMetres),
      maximumPatternedPathDistanceMetres_(maximumPatternedPathDistanceMetres),
      retainedBytes_(retainedBytes) {}

Result<std::shared_ptr<const SketchPresentedChunkCoverage>>
SketchPresentedChunkCoverage::create(const SketchSceneMesh &mesh,
                                     std::vector<std::uint32_t> chunks,
                                     std::size_t maximumRetainedBytes) {
  const std::size_t wordCount =
      mesh.chunks().size() / 64U + (mesh.chunks().size() % 64U != 0U);
  std::size_t membershipBytes = 0U;
  std::size_t requestedBytes = 0U;
  if (!detail::checkedSizeMultiply(wordCount, sizeof(std::uint64_t),
                                   membershipBytes) ||
      !detail::checkedSizeAdd(sizeof(SketchPresentedChunkCoverage),
                              membershipBytes, requestedBytes) ||
      requestedBytes > maximumRetainedBytes)
    return std::unexpected(
        diagnostic("desktop.sketch.pick-coverage-budget",
                   "presented sketch chunk coverage exceeds its budget"));
  auto sequence = SketchChunkSequence::create(mesh, membershipBytes);
  if (!sequence)
    return std::unexpected(std::move(sequence.error()));
  for (const std::uint32_t chunk : chunks) {
    auto appended = sequence->push_back(chunk);
    if (!appended &&
        appended.error().code == "desktop.sketch.invalid-visible-chunks")
      return std::unexpected(
          diagnostic("desktop.sketch.invalid-presented-chunks",
                     "presented sketch chunks are not unique mesh indices"));
    if (!appended)
      return std::unexpected(std::move(appended.error()));
  }
  return create(mesh, *sequence, maximumRetainedBytes);
}

Result<std::shared_ptr<const SketchPresentedChunkCoverage>>
SketchPresentedChunkCoverage::create(const SketchSceneMesh &mesh,
                                     SketchChunkSequence &chunks,
                                     std::size_t maximumRetainedBytes) {
  if (chunks.meshIdentity_ != &mesh)
    return std::unexpected(
        diagnostic("desktop.sketch.invalid-presented-chunks",
                   "presented sketch coverage belongs to another mesh"));
  std::size_t membershipBytes = 0U;
  std::size_t retainedBytes = 0U;
  if (!detail::checkedSizeMultiply(chunks.membershipWords_.capacity(),
                                   sizeof(std::uint64_t), membershipBytes) ||
      !detail::checkedSizeAdd(sizeof(SketchPresentedChunkCoverage),
                              membershipBytes, retainedBytes) ||
      retainedBytes > maximumRetainedBytes)
    return std::unexpected(
        diagnostic("desktop.sketch.pick-coverage-budget",
                   "presented sketch chunk coverage exceeds its budget"));
  try {
    auto membership = std::move(chunks.membershipWords_);
    chunks.meshIdentity_ = nullptr;
    return std::shared_ptr<const SketchPresentedChunkCoverage>(
        new SketchPresentedChunkCoverage{
            std::move(membership), chunks.size_,
            chunks.maximumExtrusionLogicalPixels_,
            chunks.maximumAnalyticDeviationMetres_,
            chunks.maximumPatternedPathDistanceMetres_, retainedBytes});
  } catch (const std::bad_alloc &) {
    return std::unexpected(
        diagnostic("desktop.sketch.pick-coverage-allocation",
                   "presented sketch chunk coverage allocation failed"));
  } catch (const std::length_error &) {
    return std::unexpected(
        diagnostic("desktop.sketch.pick-coverage-budget",
                   "presented sketch chunk coverage exceeds vector capacity"));
  }
}

bool SketchPresentedChunkCoverage::contains(std::uint32_t chunk) const {
  const std::size_t word = static_cast<std::size_t>(chunk) / 64U;
  return word < membershipWords_.size() &&
         (membershipWords_[word] &
          (std::uint64_t{1} << (static_cast<std::size_t>(chunk) % 64U))) != 0U;
}

Result<SketchChunkSequence>
SketchChunkSequence::create(const SketchSceneMesh &mesh,
                            std::size_t maximumMembershipBytes) {
  SketchChunkSequence sequence;
  sequence.maximumSize_ = mesh.chunks().size();
  sequence.meshIdentity_ = &mesh;
  const std::size_t wordCount =
      sequence.maximumSize_ / 64U + (sequence.maximumSize_ % 64U != 0U);
  std::size_t membershipBytes = 0U;
  if (!detail::checkedSizeMultiply(wordCount, sizeof(std::uint64_t),
                                   membershipBytes) ||
      membershipBytes > maximumMembershipBytes)
    return std::unexpected(
        diagnostic("desktop.sketch.chunk-sequence-budget",
                   "sketch chunk membership exceeds its fixed budget"));
  try {
    sequence.membershipWords_.assign(wordCount, 0U);
    std::size_t actualBytes = 0U;
    if (!detail::checkedSizeMultiply(sequence.membershipWords_.capacity(),
                                     sizeof(std::uint64_t), actualBytes) ||
        actualBytes > maximumMembershipBytes)
      return std::unexpected(
          diagnostic("desktop.sketch.chunk-sequence-budget",
                     "sketch chunk membership exceeds its fixed budget"));
    return sequence;
  } catch (const std::bad_alloc &) {
    return std::unexpected(
        diagnostic("desktop.sketch.chunk-sequence-allocation",
                   "sketch chunk membership allocation failed"));
  } catch (const std::length_error &) {
    return std::unexpected(
        diagnostic("desktop.sketch.chunk-sequence-budget",
                   "sketch chunk membership exceeds vector capacity"));
  }
}

Result<void> SketchChunkSequence::push_back(std::uint32_t chunk) {
  if (!meshIdentity_ || chunk >= maximumSize_ || size_ == maximumSize_)
    return std::unexpected(
        diagnostic("desktop.sketch.invalid-visible-chunks",
                   "visible sketch chunk is outside its bounded sequence"));
  const std::size_t word = static_cast<std::size_t>(chunk) / 64U;
  const std::uint64_t bit = std::uint64_t{1}
                            << (static_cast<std::size_t>(chunk) % 64U);
  if ((membershipWords_[word] & bit) != 0U)
    return std::unexpected(diagnostic("desktop.sketch.invalid-visible-chunks",
                                      "visible sketch chunks are not unique"));
  if (size_ == capacity_) {
    const std::size_t remaining = maximumSize_ - capacity_;
    const std::size_t geometric =
        capacity_ > std::numeric_limits<std::size_t>::max() - firstBlockSize
            ? remaining
            : capacity_ + firstBlockSize;
    const std::size_t blockSize = std::min(remaining, geometric);
    if (blockSize == 0U)
      return std::unexpected(
          diagnostic("desktop.sketch.chunk-sequence-budget",
                     "sketch chunk sequence exhausted its fixed capacity"));
    try {
      blocks_.push_back(
          std::make_unique_for_overwrite<std::uint32_t[]>(blockSize));
    } catch (const std::bad_alloc &) {
      return std::unexpected(
          diagnostic("desktop.sketch.chunk-sequence-allocation",
                     "sketch chunk sequence allocation failed"));
    } catch (const std::length_error &) {
      return std::unexpected(
          diagnostic("desktop.sketch.chunk-sequence-budget",
                     "sketch chunk sequence exceeds vector capacity"));
    }
    capacity_ += blockSize;
  }
  const std::size_t scaled = size_ / firstBlockSize + 1U;
  const auto width = std::bit_width(scaled);
  const std::size_t block =
      width == 0 ? 0U : static_cast<std::size_t>(width - 1);
  const std::size_t preceding =
      firstBlockSize * ((std::size_t{1} << block) - 1U);
  blocks_[block][size_ - preceding] = chunk;
  ++size_;
  membershipWords_[word] |= bit;
  const SketchChunkBounds &bounds = meshIdentity_->chunks()[chunk]->bounds();
  maximumExtrusionLogicalPixels_ = std::max(
      maximumExtrusionLogicalPixels_, bounds.maximumExtrusionLogicalPixels);
  maximumAnalyticDeviationMetres_ = std::max(
      maximumAnalyticDeviationMetres_, bounds.maximumAnalyticDeviationMetres);
  maximumPatternedPathDistanceMetres_ =
      std::max(maximumPatternedPathDistanceMetres_,
               bounds.maximumPatternedPathDistanceMetres);
  return {};
}

std::uint32_t SketchChunkSequence::operator[](std::size_t index) const {
  const std::size_t scaled = index / firstBlockSize + 1U;
  const auto width = std::bit_width(scaled);
  const std::size_t block =
      width == 0 ? 0U : static_cast<std::size_t>(width - 1);
  const std::size_t preceding =
      firstBlockSize * ((std::size_t{1} << block) - 1U);
  return blocks_[block][index - preceding];
}

std::size_t SketchChunkSequence::retainedOrderBytes() const {
  std::size_t orderBytes = 0U;
  std::size_t blockBytes = 0U;
  std::size_t membershipBytes = 0U;
  std::size_t retainedBytes = 0U;
  if (!detail::checkedSizeMultiply(capacity_, sizeof(std::uint32_t),
                                   orderBytes) ||
      !detail::checkedSizeMultiply(blocks_.capacity(),
                                   sizeof(std::unique_ptr<std::uint32_t[]>),
                                   blockBytes) ||
      !detail::checkedSizeMultiply(membershipWords_.capacity(),
                                   sizeof(std::uint64_t), membershipBytes) ||
      !detail::checkedSizeAdd(orderBytes, blockBytes, retainedBytes) ||
      !detail::checkedSizeAdd(retainedBytes, membershipBytes, retainedBytes))
    return std::numeric_limits<std::size_t>::max();
  return retainedBytes;
}

ProgressiveSketchVisibility::ProgressiveSketchVisibility(
    std::shared_ptr<const SketchSceneMesh> mesh, SketchChunkBounds visible,
    std::vector<std::uint32_t> pendingNodes, SketchChunkSequence selectedChunks,
    double transformMetresPerLogicalPixel)
    : mesh_(std::move(mesh)), visible_(visible),
      pendingNodes_(std::move(pendingNodes)),
      selectedChunks_(std::move(selectedChunks)),
      transformMetresPerLogicalPixel_(transformMetresPerLogicalPixel) {}

Result<ProgressiveSketchVisibility>
ProgressiveSketchVisibility::create(std::shared_ptr<const SketchSceneMesh> mesh,
                                    const SketchViewTransform &transform,
                                    SketchPickCoveragePolicy pickCoverage) {
  if (!mesh)
    return std::unexpected(
        diagnostic("desktop.sketch.null-visibility-mesh",
                   "progressive visibility requires a sketch mesh"));
  auto visible = viewportBounds(mesh->originMetres_, transform, pickCoverage);
  if (!visible)
    return std::unexpected(std::move(visible.error()));
  try {
    std::vector<std::uint32_t> pending;
    if (!mesh->chunks_.empty())
      pending.push_back(mesh->spatialRoot_);
    constexpr std::size_t coverageObjectBytes =
        sizeof(SketchPresentedChunkCoverage);
    auto selected = SketchChunkSequence::create(
        *mesh, SketchPresentedChunkCoverage::defaultMaximumRetainedBytes -
                   coverageObjectBytes);
    if (!selected)
      return std::unexpected(std::move(selected.error()));
    return ProgressiveSketchVisibility{
        std::move(mesh), *visible, std::move(pending), std::move(*selected),
        transform.camera().metresPerLogicalPixel};
  } catch (const std::bad_alloc &) {
    return std::unexpected(
        diagnostic("desktop.sketch.visibility-allocation",
                   "progressive sketch visibility allocation failed"));
  } catch (const std::length_error &) {
    return std::unexpected(
        diagnostic("desktop.sketch.visibility-budget",
                   "progressive sketch visibility exceeded vector capacity"));
  }
}

Result<SketchVisibilitySlice>
ProgressiveSketchVisibility::takeNextSlice(std::size_t maximumSpatialNodes,
                                           std::size_t maximumVisibleChunks) {
  if (maximumSpatialNodes == 0U || maximumVisibleChunks == 0U)
    return std::unexpected(
        diagnostic("desktop.sketch.invalid-visibility-slice",
                   "progressive visibility slice budget is zero"));
  try {
    SketchVisibilitySlice slice;
    slice.chunks.reserve(std::min(maximumVisibleChunks, pendingNodes_.size()));
    while (!pendingNodes_.empty() &&
           slice.spatialNodesVisited < maximumSpatialNodes &&
           slice.chunks.size() < maximumVisibleChunks) {
      const std::uint32_t index = pendingNodes_.back();
      pendingNodes_.pop_back();
      ++slice.spatialNodesVisited;
      const SketchSceneMesh::SpatialNode &node = mesh_->spatialIndex_[index];
      const double extrusionMetres = node.bounds.maximumExtrusionLogicalPixels *
                                     transformMetresPerLogicalPixel_;
      if (!std::isfinite(extrusionMetres))
        return std::unexpected(diagnostic(
            "desktop.sketch.unrepresentable-visible-bounds",
            "sketch chunk extrusion exceeds finite coordinate range"));
      if (!node.bounds.intersects(visible_, extrusionMetres))
        continue;
      if (node.leaf) {
        auto appended = selectedChunks_.push_back(node.first);
        if (!appended)
          return std::unexpected(std::move(appended.error()));
        slice.chunks.push_back(node.first);
      } else {
        pendingNodes_.push_back(node.second);
        pendingNodes_.push_back(node.first);
      }
    }
    spatialNodesVisited_ += slice.spatialNodesVisited;
    return slice;
  } catch (const std::bad_alloc &) {
    return std::unexpected(
        diagnostic("desktop.sketch.visibility-allocation",
                   "progressive sketch visibility allocation failed"));
  } catch (const std::length_error &) {
    return std::unexpected(
        diagnostic("desktop.sketch.visibility-budget",
                   "progressive sketch visibility exceeded vector capacity"));
  }
}

SketchChunkSequence ProgressiveSketchVisibility::releaseSelectedChunks() {
  return std::move(selectedChunks_);
}

Result<SketchStrokeMeshBuildOutput> SketchStrokeMeshBuildAccess::build(
    const SketchStrokeMeshSource &source, SketchCurveLod lod,
    SketchTessellationOptions tessellation, SketchUploadOptions upload,
    std::shared_ptr<const SketchSceneMesh> reuse,
    std::stop_token cancellationToken) {
  CancellationPoller cancellation{cancellationToken};
  try {
    cancellation.checkpointNow();
    if (auto valid = validateOptions(tessellation); !valid)
      return std::unexpected(std::move(valid.error()));
    constexpr std::size_t minimumChunkBytes =
        3U * sizeof(SketchMeshVertex) + 3U * sizeof(std::uint32_t);
    if (upload.maximumChunkBytes < minimumChunkBytes ||
        upload.maximumChunks == 0U ||
        upload.maximumChunks > std::numeric_limits<std::uint32_t>::max() ||
        !std::isfinite(upload.spatialTileLogicalPixels) ||
        upload.spatialTileLogicalPixels <= 0.0 ||
        upload.maximumRetainedMeshBytes == 0U ||
        upload.maximumRetainedMeshBytes >
            SketchUploadOptions::maximumConfigurableRetainedMeshBytes ||
        upload.maximumPreparationScratchBytes == 0U ||
        upload.maximumPreparationScratchBytes >
            SketchUploadOptions::maximumConfigurablePreparationScratchBytes ||
        upload.maximumPreparationPeakBytes == 0U ||
        upload.maximumPreparationPeakBytes >
            SketchUploadOptions::maximumConfigurablePreparationPeakBytes)
      return std::unexpected(
          diagnostic("desktop.sketch.invalid-upload-options",
                     "sketch upload chunk options are invalid"));
    if (!std::isfinite(lod.maximumChordErrorMetres()) ||
        lod.maximumChordErrorMetres() <= 0.0)
      return std::unexpected(diagnostic("desktop.sketch.invalid-curve-lod",
                                        "sketch curve LOD is invalid"));
    PreparationPayloadBudget memoryBudget{upload};
    MeshBuilder builder{tessellation, cancellation, memoryBudget};
    if (source.primitiveCount != 0U &&
        (!source.primitiveAt || !source.primitiveContext))
      return std::unexpected(
          diagnostic("desktop.sketch.invalid-mesh-source",
                     "sketch stroke mesh source has no primitive projection"));
    constexpr std::size_t maximumStyleCount =
        static_cast<std::size_t>(std::numeric_limits<std::uint16_t>::max()) +
        1U;
    if (source.styles.size() > maximumStyleCount ||
        (source.primitiveCount != 0U && source.styles.empty()))
      return std::unexpected(
          diagnostic("desktop.sketch.invalid-mesh-source",
                     "sketch stroke mesh source has an invalid style table"));
    for (const render::SketchStyle &style : source.styles)
      if (!std::isfinite(style.strokeWidthPixels) ||
          style.strokeWidthPixels <= 0.0F ||
          !std::isfinite(style.pointDiameterPixels) ||
          style.pointDiameterPixels <= 0.0F)
        return std::unexpected(
            diagnostic("desktop.sketch.invalid-mesh-source",
                       "sketch stroke mesh source contains an invalid style"));
    const bool finiteBounds =
        finite(source.bounds.minimum) && finite(source.bounds.maximum) &&
        source.bounds.minimum.x <= source.bounds.maximum.x &&
        source.bounds.minimum.y <= source.bounds.maximum.y;
    if ((source.primitiveCount == 0U) != source.bounds.empty ||
        (!source.bounds.empty && !finiteBounds))
      return std::unexpected(
          diagnostic("desktop.sketch.invalid-mesh-source",
                     "sketch stroke mesh source has invalid declared bounds"));
    const auto contained = [&](render::Point2d point) {
      return finite(point) &&
             point.x >=
                 std::nextafter(source.bounds.minimum.x,
                                -std::numeric_limits<double>::infinity()) &&
             point.x <=
                 std::nextafter(source.bounds.maximum.x,
                                std::numeric_limits<double>::infinity()) &&
             point.y >=
                 std::nextafter(source.bounds.minimum.y,
                                -std::numeric_limits<double>::infinity()) &&
             point.y <= std::nextafter(source.bounds.maximum.y,
                                       std::numeric_limits<double>::infinity());
    };
    const auto validatePrimitive =
        [&](const SketchStrokeSourcePrimitive &primitive,
            sketch::NurbsView spline) -> Result<void> {
      if (primitive.sourceKey == 0U ||
          primitive.style >= source.styles.size() || !finite(primitive.first) ||
          !finite(primitive.second) || !std::isfinite(primitive.radius) ||
          primitive.radius < 0.0 ||
          !std::isfinite(primitive.startAngleRadians) ||
          !std::isfinite(primitive.sweepAngleRadians) ||
          !std::isfinite(primitive.secondaryRadius) ||
          !std::isfinite(primitive.rotationAngleRadians))
        return std::unexpected(diagnostic(
            "desktop.sketch.invalid-mesh-source",
            "sketch stroke mesh source contains an invalid primitive"));
      switch (primitive.kind) {
      case SketchStrokeSourceKind::Point:
        if (!contained(primitive.first))
          return std::unexpected(diagnostic(
              "desktop.sketch.invalid-mesh-source",
              "sketch stroke mesh point escapes its declared bounds"));
        break;
      case SketchStrokeSourceKind::Glyph:
        if (!contained(primitive.first) || primitive.glyph == 0U)
          return std::unexpected(
              diagnostic("desktop.sketch.invalid-mesh-source",
                         "sketch stroke glyph escapes its declared bounds"));
        break;
      case SketchStrokeSourceKind::Line:
        if (!contained(primitive.first) || !contained(primitive.second))
          return std::unexpected(diagnostic(
              "desktop.sketch.invalid-mesh-source",
              "sketch stroke mesh line escapes its declared bounds"));
        break;
      case SketchStrokeSourceKind::Circle: {
        if (primitive.radius <= 0.0)
          return std::unexpected(diagnostic(
              "desktop.sketch.invalid-mesh-source",
              "sketch stroke mesh source contains an invalid curve"));
        const render::Point2d minimum{primitive.first.x - primitive.radius,
                                      primitive.first.y - primitive.radius};
        const render::Point2d maximum{primitive.first.x + primitive.radius,
                                      primitive.first.y + primitive.radius};
        if (!contained(minimum) || !contained(maximum))
          return std::unexpected(diagnostic(
              "desktop.sketch.invalid-mesh-source",
              "sketch stroke mesh curve escapes its declared bounds"));
        break;
      }
      case SketchStrokeSourceKind::Arc: {
        if (primitive.radius <= 0.0 || primitive.sweepAngleRadians == 0.0 ||
            std::abs(primitive.sweepAngleRadians) > fullTurn)
          return std::unexpected(
              diagnostic("desktop.sketch.invalid-mesh-source",
                         "sketch stroke mesh source contains an invalid arc"));
        const auto radial = [&](double angle) {
          return render::Point2d{
              primitive.first.x + primitive.radius * std::cos(angle),
              primitive.first.y + primitive.radius * std::sin(angle)};
        };
        const auto positiveAngle = [](double angle) {
          angle = std::fmod(angle, fullTurn);
          return angle < 0.0 ? angle + fullTurn : angle;
        };
        const auto angleOnArc = [&](double angle) {
          return primitive.sweepAngleRadians > 0.0
                     ? positiveAngle(angle - primitive.startAngleRadians) <=
                           primitive.sweepAngleRadians
                     : positiveAngle(primitive.startAngleRadians - angle) <=
                           -primitive.sweepAngleRadians;
        };
        if (!contained(radial(primitive.startAngleRadians)) ||
            !contained(radial(primitive.startAngleRadians +
                              primitive.sweepAngleRadians)))
          return std::unexpected(
              diagnostic("desktop.sketch.invalid-mesh-source",
                         "sketch stroke mesh arc escapes its declared bounds"));
        for (const double cardinal :
             {0.0, std::numbers::pi / 2.0, std::numbers::pi,
              3.0 * std::numbers::pi / 2.0})
          if (angleOnArc(cardinal) && !contained(radial(cardinal)))
            return std::unexpected(diagnostic(
                "desktop.sketch.invalid-mesh-source",
                "sketch stroke mesh arc escapes its declared bounds"));
        break;
      }
      case SketchStrokeSourceKind::Ellipse:
      case SketchStrokeSourceKind::EllipticalArc: {
        const bool full = primitive.kind == SketchStrokeSourceKind::Ellipse;
        if (primitive.radius <= 0.0 || primitive.secondaryRadius <= 0.0 ||
            primitive.secondaryRadius > primitive.radius ||
            (!full && (primitive.sweepAngleRadians == 0.0 ||
                       std::abs(primitive.sweepAngleRadians) > fullTurn)) ||
            (full && (primitive.startAngleRadians != 0.0 ||
                      primitive.sweepAngleRadians != 0.0)))
          return std::unexpected(diagnostic(
              "desktop.sketch.invalid-mesh-source",
              "sketch stroke mesh source contains an invalid ellipse"));
        const auto pointAt = [&](double parameter) {
          const double cosine = std::cos(primitive.rotationAngleRadians);
          const double sine = std::sin(primitive.rotationAngleRadians);
          const double localX = primitive.radius * std::cos(parameter);
          const double localY = primitive.secondaryRadius * std::sin(parameter);
          return render::Point2d{
              primitive.first.x + cosine * localX - sine * localY,
              primitive.first.y + sine * localX + cosine * localY};
        };
        if (full) {
          const double cosine = std::cos(primitive.rotationAngleRadians);
          const double sine = std::sin(primitive.rotationAngleRadians);
          const double extentX = std::hypot(primitive.radius * cosine,
                                            primitive.secondaryRadius * sine);
          const double extentY = std::hypot(primitive.radius * sine,
                                            primitive.secondaryRadius * cosine);
          if (!contained(
                  {primitive.first.x - extentX, primitive.first.y - extentY}) ||
              !contained(
                  {primitive.first.x + extentX, primitive.first.y + extentY}))
            return std::unexpected(diagnostic(
                "desktop.sketch.invalid-mesh-source",
                "sketch stroke ellipse escapes its declared bounds"));
          break;
        }
        const auto positiveAngle = [](double angle) {
          angle = std::fmod(angle, fullTurn);
          return angle < 0.0 ? angle + fullTurn : angle;
        };
        const auto onArc = [&](double parameter) {
          return primitive.sweepAngleRadians > 0.0
                     ? positiveAngle(parameter - primitive.startAngleRadians) <=
                           primitive.sweepAngleRadians
                     : positiveAngle(primitive.startAngleRadians - parameter) <=
                           -primitive.sweepAngleRadians;
        };
        if (!contained(pointAt(primitive.startAngleRadians)) ||
            !contained(pointAt(primitive.startAngleRadians +
                               primitive.sweepAngleRadians)))
          return std::unexpected(diagnostic(
              "desktop.sketch.invalid-mesh-source",
              "sketch stroke elliptical arc escapes its declared bounds"));
        const double rotation = primitive.rotationAngleRadians;
        const std::array extrema{
            std::atan2(-primitive.secondaryRadius * std::sin(rotation),
                       primitive.radius * std::cos(rotation)),
            std::atan2(-primitive.secondaryRadius * std::sin(rotation),
                       primitive.radius * std::cos(rotation)) +
                std::numbers::pi,
            std::atan2(primitive.secondaryRadius * std::cos(rotation),
                       primitive.radius * std::sin(rotation)),
            std::atan2(primitive.secondaryRadius * std::cos(rotation),
                       primitive.radius * std::sin(rotation)) +
                std::numbers::pi};
        for (const double parameter : extrema)
          if (onArc(parameter) && !contained(pointAt(parameter)))
            return std::unexpected(diagnostic(
                "desktop.sketch.invalid-mesh-source",
                "sketch stroke elliptical arc escapes its declared bounds"));
        break;
      }
      case SketchStrokeSourceKind::HyperbolicArc:
      case SketchStrokeSourceKind::ParabolicArc: {
        const bool hyperbolic =
            primitive.kind == SketchStrokeSourceKind::HyperbolicArc;
        if (primitive.radius <= 0.0 || primitive.sweepAngleRadians == 0.0 ||
            (hyperbolic && primitive.secondaryRadius <= 0.0) ||
            (!hyperbolic && primitive.secondaryRadius != 0.0))
          return std::unexpected(diagnostic(
              "desktop.sketch.invalid-mesh-source",
              "sketch stroke mesh source contains an invalid conic arc"));
        auto bounds = sketchStrokePrimitiveBounds(primitive);
        if (!bounds || !contained(bounds->minimum) ||
            !contained(bounds->maximum))
          return std::unexpected(diagnostic(
              "desktop.sketch.invalid-mesh-source",
              "sketch stroke conic arc escapes its declared bounds"));
        break;
      }
      case SketchStrokeSourceKind::BSpline: {
        const std::size_t count = spline.weights.size();
        if (!source.splineAt || count < 2U || count > 1'024U ||
            spline.degree == 0U || spline.degree > 25U ||
            spline.degree >= count ||
            spline.controlPointCoordinates.size() != count * 2U ||
            spline.knots.size() != count + spline.degree + 1U ||
            !std::ranges::all_of(
                spline.controlPointCoordinates,
                [](double value) { return std::isfinite(value); }) ||
            !std::ranges::all_of(
                spline.knots,
                [](double value) { return std::isfinite(value); }) ||
            !std::ranges::is_sorted(spline.knots) ||
            !(spline.knots[spline.degree] < spline.knots[count]) ||
            !std::ranges::all_of(spline.weights, [](double value) {
              return std::isfinite(value) && value >= 1.0e-12 &&
                     value <= 1.0e12;
            }))
          return std::unexpected(diagnostic(
              "desktop.sketch.invalid-mesh-source",
              "sketch stroke mesh source contains an invalid B-spline"));
        auto bounds = sketchStrokePrimitiveBounds(primitive, spline);
        if (!bounds || !contained(bounds->minimum) ||
            !contained(bounds->maximum))
          return std::unexpected(
              diagnostic("desktop.sketch.invalid-mesh-source",
                         "sketch stroke B-spline escapes its declared bounds"));
        break;
      }
      default:
        return std::unexpected(diagnostic(
            "desktop.sketch.invalid-mesh-source",
            "sketch stroke mesh source contains an unknown primitive kind"));
      }
      return {};
    };
    struct SourceFingerprint {
      std::uint32_t sourceKey = 0U;
      std::uint64_t fingerprint = 0U;
    };
    std::size_t styleBytes = 0U;
    std::size_t sourceFingerprintBytes = 0U;
    std::size_t sortedSourceKeyBytes = 0U;
    std::size_t sourceIdentityScratchBytes = 0U;
    std::size_t minimumRetainedBytes = 0U;
    std::size_t minimumScratchBytes = 0U;
    std::size_t minimumPeakBytes = 0U;
    if (!detail::checkedSizeMultiply(source.styles.size(),
                                     sizeof(render::SketchStyle), styleBytes) ||
        !detail::checkedSizeMultiply(source.primitiveCount,
                                     sizeof(SourceFingerprint),
                                     sourceFingerprintBytes) ||
        !detail::checkedSizeMultiply(source.primitiveCount,
                                     sizeof(std::uint32_t),
                                     sortedSourceKeyBytes) ||
        !detail::checkedSizeAdd(sourceFingerprintBytes, sortedSourceKeyBytes,
                                sourceIdentityScratchBytes) ||
        !detail::checkedSizeAdd(sizeof(SketchSceneMesh), styleBytes,
                                minimumRetainedBytes) ||
        !detail::checkedSizeMultiply(source.styles.size(),
                                     sizeof(SketchMeshBatch),
                                     minimumScratchBytes) ||
        !detail::checkedSizeAdd(minimumScratchBytes, sourceIdentityScratchBytes,
                                minimumScratchBytes) ||
        !detail::checkedSizeAdd(minimumRetainedBytes, minimumScratchBytes,
                                minimumPeakBytes) ||
        minimumRetainedBytes > upload.maximumRetainedMeshBytes ||
        minimumScratchBytes > upload.maximumPreparationScratchBytes ||
        minimumPeakBytes > upload.maximumPreparationPeakBytes)
      return std::unexpected(diagnostic(
          "desktop.sketch.mesh-budget",
          "sketch stroke mesh minimum storage exceeds its byte budgets"));
    memoryBudget.observe(minimumRetainedBytes, minimumScratchBytes);
    std::vector<SourceFingerprint> sourceFingerprints;
    sourceFingerprints.reserve(source.primitiveCount);
    std::vector<std::uint32_t> sortedSourceKeys;
    sortedSourceKeys.reserve(source.primitiveCount);
    for (std::size_t primitiveIndex = 0U;
         primitiveIndex < source.primitiveCount; ++primitiveIndex) {
      cancellation.checkpoint();
      const SketchStrokeSourcePrimitive primitive =
          source.primitiveAt(source.primitiveContext, primitiveIndex);
      const sketch::NurbsView spline =
          primitive.kind == SketchStrokeSourceKind::BSpline && source.splineAt
              ? source.splineAt(source.primitiveContext, primitiveIndex)
              : sketch::NurbsView{};
      if (auto valid = validatePrimitive(primitive, spline); !valid)
        return std::unexpected(std::move(valid.error()));
      sourceFingerprints.push_back(
          {primitive.sourceKey, primitiveFingerprint(primitive, spline)});
      sortedSourceKeys.push_back(primitive.sourceKey);
    }
    std::ranges::sort(sortedSourceKeys,
                      [&](std::uint32_t first, std::uint32_t second) {
                        cancellation.checkpoint();
                        return first < second;
                      });
    if (std::ranges::adjacent_find(sortedSourceKeys) != sortedSourceKeys.end())
      return std::unexpected(
          diagnostic("desktop.sketch.duplicate-source-key",
                     "sketch stroke primitives have duplicate source keys"));
    builder.metrics.inputPrimitives = source.primitiveCount;
    builder.batches.reserve(source.styles.size());
    for (std::size_t index = 0; index < source.styles.size(); ++index) {
      cancellation.checkpoint();
      const render::SketchStyle &style = source.styles[index];
      builder.batches.push_back(
          {static_cast<std::uint16_t>(index), style.layer, {}, {}, {}, {}});
    }
    builder.fixedScratchBytes =
        memoryBudget.sum({memoryBudget.capacityBytes(sourceFingerprints),
                          memoryBudget.capacityBytes(sortedSourceKeys)});
    builder.retainedFloorBytes = minimumRetainedBytes;
    builder.payloadBytes = memoryBudget.capacityBytes(builder.batches);
    builder.observe(builder.payloadBytes);

    render::Point2d origin =
        source.bounds.empty
            ? render::Point2d{}
            : render::Point2d{std::midpoint(source.bounds.minimum.x,
                                            source.bounds.maximum.x),
                              std::midpoint(source.bounds.minimum.y,
                                            source.bounds.maximum.y)};
    if (reuse && reuse->lod() == lod && !source.bounds.empty) {
      constexpr double compatibleRange =
          static_cast<double>(std::numeric_limits<float>::max()) * 0.25;
      const render::Point2d prior = reuse->originMetres();
      const bool compatible =
          std::abs(source.bounds.minimum.x - prior.x) <= compatibleRange &&
          std::abs(source.bounds.maximum.x - prior.x) <= compatibleRange &&
          std::abs(source.bounds.minimum.y - prior.y) <= compatibleRange &&
          std::abs(source.bounds.maximum.y - prior.y) <= compatibleRange;
      if (compatible)
        origin = prior;
    }
    const auto local = [origin](render::Point2d point) {
      return QPointF{point.x - origin.x, point.y - origin.y};
    };
    for (std::size_t primitiveIndex = 0U;
         primitiveIndex < source.primitiveCount; ++primitiveIndex) {
      cancellation.checkpoint();
      const SketchStrokeSourcePrimitive primitive =
          source.primitiveAt(source.primitiveContext, primitiveIndex);
      const sketch::NurbsView spline =
          primitive.kind == SketchStrokeSourceKind::BSpline && source.splineAt
              ? source.splineAt(source.primitiveContext, primitiveIndex)
              : sketch::NurbsView{};
      if (auto valid = validatePrimitive(primitive, spline); !valid)
        return std::unexpected(std::move(valid.error()));
      if (primitive.sourceKey != sourceFingerprints[primitiveIndex].sourceKey ||
          primitiveFingerprint(primitive, spline) !=
              sourceFingerprints[primitiveIndex].fingerprint)
        return std::unexpected(
            diagnostic("desktop.sketch.changed-mesh-source",
                       "sketch stroke source changed during preparation"));
      if (!primitive.visible)
        continue;
      const render::SketchStyle &style = source.styles[primitive.style];
      SketchMeshBatch &batch = builder.batches[primitive.style];
      const QPointF first = local(primitive.first);
      if (primitive.kind == SketchStrokeSourceKind::Point) {
        builder.point(batch, primitive.sourceKey, first,
                      style.pointDiameterPixels);
        ++builder.metrics.visiblePrimitives;
        continue;
      }
      if (primitive.kind == SketchStrokeSourceKind::Glyph) {
        builder.glyph(batch, primitive.sourceKey, first, primitive.glyph,
                      style.pointDiameterPixels);
        ++builder.metrics.visiblePrimitives;
        continue;
      }
      if (primitive.kind == SketchStrokeSourceKind::Line) {
        const QPointF second = local(primitive.second);
        const std::array<QPointF, 2> line{first, second};
        builder.polyline(batch, primitive.sourceKey, line,
                         style.strokeWidthPixels, strokePattern(style), false,
                         0.0);
        ++builder.metrics.visiblePrimitives;
        continue;
      }
      if (primitive.kind == SketchStrokeSourceKind::BSpline) {
        auto tessellated = sketch::tessellateNurbs(
            spline, lod.maximumChordErrorMetres(),
            tessellation.maximumCurveSegments, cancellationToken);
        if (!tessellated) {
          if (tessellated.error().code == "sketch.nurbs.cancelled")
            throw detail::SketchProjectionCancelled{};
          return std::unexpected(std::move(tessellated.error()));
        }
        const std::size_t requestedPointBytes =
            memoryBudget.bytes(tessellated->points.size(), sizeof(QPointF));
        builder.observeTemporary(
            memoryBudget.sum({tessellated->peakBytes, requestedPointBytes}));
        std::vector<QPointF> points;
        points.reserve(tessellated->points.size());
        const std::size_t pointBytes = memoryBudget.capacityBytes(points);
        builder.observeTemporary(
            memoryBudget.sum({tessellated->retainedBytes, pointBytes}));
        for (const sketch::NurbsPoint point : tessellated->points) {
          cancellation.checkpoint();
          points.push_back(local({point.x, point.y}));
        }
        const bool closed =
            std::hypot(points.back().x() - points.front().x(),
                       points.back().y() - points.front().y()) <=
            lod.maximumChordErrorMetres();
        builder.polyline(
            batch, primitive.sourceKey, points, style.strokeWidthPixels,
            strokePattern(style), closed, tessellated->maximumCertifiedDeviation,
            memoryBudget.sum({tessellated->retainedBytes, pointBytes}));
        ++builder.metrics.visiblePrimitives;
        continue;
      }

      const bool closed = primitive.kind == SketchStrokeSourceKind::Circle ||
                          primitive.kind == SketchStrokeSourceKind::Ellipse;
      const bool ellipse =
          primitive.kind == SketchStrokeSourceKind::Ellipse ||
          primitive.kind == SketchStrokeSourceKind::EllipticalArc;
      const bool conic =
          primitive.kind == SketchStrokeSourceKind::HyperbolicArc ||
          primitive.kind == SketchStrokeSourceKind::ParabolicArc;
      const double start = closed ? 0.0 : primitive.startAngleRadians;
      const double sweep = closed ? fullTurn : primitive.sweepAngleRadians;
      const std::size_t segments =
          conic ? conicSegments(primitive, lod, tessellation)
                : curveSegments(primitive.radius, sweep, lod, tessellation);
      const std::size_t pointCount = segments + 1U;
      const std::size_t requestedPointBytes =
          memoryBudget.bytes(pointCount, sizeof(QPointF));
      builder.observeTemporary(requestedPointBytes);
      std::vector<QPointF> points;
      points.reserve(pointCount);
      const std::size_t pointBytes = memoryBudget.capacityBytes(points);
      builder.observeTemporary(pointBytes);
      for (std::size_t index = 0; index < pointCount; ++index) {
        cancellation.checkpoint();
        const double amount =
            static_cast<double>(index) / static_cast<double>(segments);
        const double angle = start + sweep * amount;
        if (conic) {
          const render::Point2d point = conicPoint(primitive, angle);
          points.push_back(local(point));
        } else if (ellipse) {
          const double cosine = std::cos(primitive.rotationAngleRadians);
          const double sine = std::sin(primitive.rotationAngleRadians);
          const double localX = primitive.radius * std::cos(angle);
          const double localY = primitive.secondaryRadius * std::sin(angle);
          points.push_back({first.x() + cosine * localX - sine * localY,
                            first.y() + sine * localX + cosine * localY});
        } else {
          points.push_back({first.x() + primitive.radius * std::cos(angle),
                            first.y() + primitive.radius * std::sin(angle)});
        }
      }
      const long double quarterStep =
          std::abs(static_cast<long double>(sweep)) /
          (4.0L * static_cast<long double>(segments));
      const long double sine = std::sin(quarterStep);
      const long double radialDeviation =
          2.0L * static_cast<long double>(primitive.radius) * sine * sine;
      const double deviation =
          conic ? conicDeviation(primitive, segments)
                : std::nextafter(static_cast<double>(radialDeviation),
                                 std::numeric_limits<double>::infinity());
      if (!std::isfinite(deviation) ||
          radialDeviation > std::numeric_limits<double>::max())
        return std::unexpected(
            diagnostic("desktop.sketch.unrepresentable-segment",
                       "sketch curve deviation exceeds finite range"));
      builder.polyline(batch, primitive.sourceKey, points,
                       style.strokeWidthPixels, strokePattern(style), closed,
                       deviation, pointBytes);
      ++builder.metrics.visiblePrimitives;
    }

    if (builder.invalidSegment)
      return std::unexpected(diagnostic(
          "desktop.sketch.unrepresentable-segment",
          "sketch projection produced a zero or non-finite segment"));
    if (builder.exhausted)
      return std::unexpected(
          diagnostic("desktop.sketch.mesh-budget",
                     "sketch projection exceeded its vertex or index budget"));
    for (const SketchMeshBatch &batch : builder.batches) {
      cancellation.checkpoint();
      if (batch.indices.size() / 3U != batch.triangleSources.size() ||
          batch.triangleSources.size() !=
              batch.triangleAnalyticDeviationsMetres.size())
        return std::unexpected(
            diagnostic("desktop.sketch.primitive-index-invariant",
                       "sketch triangle provenance is incomplete"));
    }
    const double spatialTileSizeMetres =
        std::scalbn(upload.spatialTileLogicalPixels, lod.scaleExponent);
    if (!std::isfinite(spatialTileSizeMetres) || spatialTileSizeMetres <= 0.0)
      return std::unexpected(
          diagnostic("desktop.sketch.invalid-upload-options",
                     "sketch spatial tile size is invalid at this LOD"));
    struct TaggedTriangle {
      std::int64_t tileY = 0;
      std::int64_t tileX = 0;
      std::uint32_t firstIndex = 0U;
      std::uint16_t style = 0U;
    };
    const auto observeScratchAppend = [&](std::size_t appendBytes) {
      memoryBudget.observe(
          minimumRetainedBytes,
          memoryBudget.sum(
              {builder.fixedScratchBytes, builder.payloadBytes, appendBytes}));
    };
    std::vector<TaggedTriangle> routedTriangles;
    const std::size_t requestedRoutedBytes = memoryBudget.bytes(
        builder.metrics.indices / 3U, sizeof(TaggedTriangle));
    observeScratchAppend(requestedRoutedBytes);
    routedTriangles.reserve(builder.metrics.indices / 3U);
    const std::size_t routedCapacityBytes =
        memoryBudget.capacityBytes(routedTriangles);
    observeScratchAppend(routedCapacityBytes);
    constexpr long double minimumTile =
        static_cast<long double>(std::numeric_limits<std::int64_t>::min());
    constexpr long double maximumTile =
        static_cast<long double>(std::numeric_limits<std::int64_t>::max());
    for (const SketchMeshBatch &batch : builder.batches) {
      cancellation.checkpoint();
      for (std::size_t triangle = 0; triangle < batch.indices.size();
           triangle += 3U) {
        cancellation.checkpoint();
        long double centerX = 0.0L;
        long double centerY = 0.0L;
        std::array<std::uint32_t, 3> triangleVertices{};
        for (std::size_t corner = 0; corner < triangleVertices.size();
             ++corner) {
          triangleVertices[corner] = batch.indices[triangle + corner];
          const SketchMeshVertex &vertex =
              batch.vertices[triangleVertices[corner]];
          centerX +=
              static_cast<long double>(origin.x) + vertex.x + vertex.xLow;
          centerY +=
              static_cast<long double>(origin.y) + vertex.y + vertex.yLow;
        }
        centerX /= 3.0L;
        centerY /= 3.0L;
        const long double tileX = std::floor(centerX / spatialTileSizeMetres);
        const long double tileY = std::floor(centerY / spatialTileSizeMetres);
        if (!std::isfinite(tileX) || !std::isfinite(tileY) ||
            tileX < minimumTile || tileX > maximumTile || tileY < minimumTile ||
            tileY > maximumTile)
          return std::unexpected(
              diagnostic("desktop.sketch.unrepresentable-spatial-tile",
                         "sketch geometry exceeds the spatial tile range"));
        routedTriangles.push_back(
            {static_cast<std::int64_t>(tileY), static_cast<std::int64_t>(tileX),
             static_cast<std::uint32_t>(triangle), batch.style});
      }
    }
    const auto sameTile = [](const TaggedTriangle &left,
                             const TaggedTriangle &right) {
      return left.style == right.style && left.tileY == right.tileY &&
             left.tileX == right.tileX;
    };
    std::ranges::sort(routedTriangles, [&](const TaggedTriangle &left,
                                           const TaggedTriangle &right) {
      cancellation.checkpoint();
      const std::uint16_t leftLayer = source.styles[left.style].layer;
      const std::uint16_t rightLayer = source.styles[right.style].layer;
      if (leftLayer != rightLayer)
        return leftLayer < rightLayer;
      if (left.style != right.style)
        return left.style < right.style;
      if (left.tileY != right.tileY)
        return left.tileY < right.tileY;
      return left.tileX != right.tileX ? left.tileX < right.tileX
                                       : left.firstIndex < right.firstIndex;
    });
    std::size_t tileCount = 0U;
    for (std::size_t index = 0U; index < routedTriangles.size(); ++index) {
      cancellation.checkpoint();
      if (index == 0U ||
          !sameTile(routedTriangles[index - 1U], routedTriangles[index]))
        ++tileCount;
    }
    if (tileCount > upload.maximumChunks)
      return std::unexpected(
          diagnostic("desktop.sketch.chunk-budget",
                     "sketch projection exceeded its spatial tile budget"));
    struct ReusableChunk {
      std::uint64_t hash = 0U;
      std::shared_ptr<const SketchUploadChunk> chunk;
      std::size_t ordinal = 0U;
      bool consumed = false;
    };
    std::vector<ReusableChunk> reusable;
    if (reuse) {
      const std::size_t requestedReusableBytes =
          memoryBudget.bytes(reuse->chunks().size(), sizeof(ReusableChunk));
      observeScratchAppend(
          memoryBudget.sum({routedCapacityBytes, requestedReusableBytes}));
      reusable.reserve(reuse->chunks().size());
      for (const auto &chunk : reuse->chunks()) {
        cancellation.checkpoint();
        reusable.push_back(
            {chunk->contentHash(), chunk, reusable.size(), false});
      }
      std::ranges::sort(
          reusable, [](const ReusableChunk &left, const ReusableChunk &right) {
            return left.hash != right.hash ? left.hash < right.hash
                                           : left.ordinal < right.ordinal;
          });
    }

    const std::size_t routedAndReusableBytes = memoryBudget.sum(
        {routedCapacityBytes, memoryBudget.capacityBytes(reusable)});
    observeScratchAppend(routedAndReusableBytes);

    std::vector<std::shared_ptr<const SketchUploadChunk>> chunks;
    const std::size_t requestedChunkPointerBytes = memoryBudget.bytes(
        tileCount, sizeof(std::shared_ptr<const SketchUploadChunk>));
    const std::size_t requestedPrimitiveSpanBytes =
        memoryBudget.bytes(builder.metrics.visiblePrimitives,
                           sizeof(SketchStrokePrimitiveSpanRecord));
    memoryBudget.observe(
        memoryBudget.sum({minimumRetainedBytes, requestedChunkPointerBytes}),
        memoryBudget.sum({builder.fixedScratchBytes, builder.payloadBytes,
                          routedAndReusableBytes,
                          requestedPrimitiveSpanBytes}));
    chunks.reserve(tileCount);
    std::vector<SketchStrokePrimitiveSpanRecord> primitiveSpans;
    primitiveSpans.reserve(builder.metrics.visiblePrimitives);
    std::vector<std::uint32_t> remapValues;
    std::vector<std::uint32_t> remapGenerations;
    std::size_t remapCapacityBytes = 0U;
    std::size_t retainedChunkBytesDuringBuild = 0U;
    const auto observeConstruction = [&](std::size_t extraRetainedBytes,
                                         std::size_t extraScratchBytes) {
      memoryBudget.observe(
          memoryBudget.sum({minimumRetainedBytes,
                            memoryBudget.capacityBytes(chunks),
                            retainedChunkBytesDuringBuild, extraRetainedBytes}),
          memoryBudget.sum({builder.fixedScratchBytes, builder.payloadBytes,
                            routedAndReusableBytes,
                            memoryBudget.capacityBytes(primitiveSpans),
                            remapCapacityBytes, extraScratchBytes}));
    };
    observeConstruction(0U, 0U);
    std::size_t maximumChunkSourceCapacity = 0U;
    SketchMeshMetrics metrics{builder.metrics.inputPrimitives,
                              builder.metrics.visiblePrimitives};
    auto finishChunk = [&](const SketchMeshBatch &batch,
                           std::vector<SketchMeshVertex> &vertices,
                           std::vector<std::uint32_t> &indices,
                           std::vector<std::uint32_t> &sources,
                           SketchChunkBounds &bounds) -> Result<void> {
      if (indices.empty())
        return {};
      maximumChunkSourceCapacity =
          std::max(maximumChunkSourceCapacity, sources.capacity());
      if (indices.size() / 3U != sources.size())
        return std::unexpected(
            diagnostic("desktop.sketch.primitive-index-invariant",
                       "sketch chunk triangle provenance is incomplete"));
      if (chunks.size() >= upload.maximumChunks)
        return std::unexpected(
            diagnostic("desktop.sketch.chunk-budget",
                       "sketch projection exceeded its upload chunk budget"));
      if (source.styles[batch.style].linePattern !=
          render::SketchLinePattern::Solid)
        bounds.maximumPatternedPathDistanceMetres =
            bounds.maximumPathDistanceMetres;
      const std::size_t bytes = memoryBudget.sum(
          {memoryBudget.bytes(vertices.size(), sizeof(SketchMeshVertex)),
           memoryBudget.bytes(indices.size(), sizeof(std::uint32_t))});
      const std::uint32_t chunkIndex =
          static_cast<std::uint32_t>(chunks.size());
      std::size_t firstTriangle = 0U;
      while (firstTriangle < sources.size()) {
        cancellation.checkpoint();
        std::size_t endTriangle = firstTriangle + 1U;
        while (endTriangle < sources.size() &&
               sources[endTriangle] == sources[firstTriangle]) {
          cancellation.checkpoint();
          ++endTriangle;
        }
        const std::size_t firstIndex = firstTriangle * 3U;
        const std::size_t indexCount = (endTriangle - firstTriangle) * 3U;
        if (firstIndex > std::numeric_limits<std::uint32_t>::max() ||
            indexCount > std::numeric_limits<std::uint32_t>::max())
          return std::unexpected(diagnostic(
              "desktop.sketch.primitive-index-limit",
              "sketch primitive chunk range exceeds its packed index range"));
        if (primitiveSpans.size() == primitiveSpans.capacity()) {
          const std::size_t nextCapacity = MeshBuilder::growthCapacity(
              primitiveSpans, primitiveSpans.size() + 1U,
              tessellation.maximumIndices / 3U);
          const std::size_t chunkScratchBytes = memoryBudget.sum(
              {memoryBudget.capacityBytes(vertices),
               memoryBudget.capacityBytes(indices),
               memoryBudget.capacityBytes(sources),
               memoryBudget.bytes(nextCapacity - primitiveSpans.capacity(),
                                  sizeof(SketchStrokePrimitiveSpanRecord))});
          observeConstruction(0U, chunkScratchBytes);
          primitiveSpans.reserve(nextCapacity);
        }
        primitiveSpans.push_back({sources[firstTriangle], chunkIndex,
                                  static_cast<std::uint32_t>(firstIndex),
                                  static_cast<std::uint32_t>(indexCount)});
        firstTriangle = endTriangle;
      }
      const std::uint64_t hash = chunkHash(batch.style, batch.layer, vertices,
                                           indices, bounds, cancellation);
      const std::size_t candidateVertexBytes =
          memoryBudget.capacityBytes(vertices);
      const std::size_t candidateIndexBytes =
          memoryBudget.capacityBytes(indices);
      const std::size_t sourceScratchBytes =
          memoryBudget.capacityBytes(sources);
      const std::size_t candidateAsScratch = memoryBudget.sum(
          {sourceScratchBytes, candidateVertexBytes, candidateIndexBytes});
      observeConstruction(0U, candidateAsScratch);
      std::shared_ptr<const SketchUploadChunk> candidate;
      auto firstMatch =
          std::ranges::lower_bound(reusable, hash, {}, &ReusableChunk::hash);
      for (auto match = firstMatch;
           match != reusable.end() && match->hash == hash; ++match) {
        if (match->consumed)
          continue;
        const auto &prior = match->chunk;
        const bool exact = [&] {
          cancellation.checkpoint();
          if (batch.style != prior->style() || batch.layer != prior->layer() ||
              bounds.maximumAnalyticDeviationMetres !=
                  prior->bounds().maximumAnalyticDeviationMetres ||
              bounds.maximumPatternedPathDistanceMetres !=
                  prior->bounds().maximumPatternedPathDistanceMetres ||
              vertices.size() != prior->vertices().size() ||
              indices.size() != prior->indices().size())
            return false;
          for (std::size_t index = 0; index < vertices.size(); ++index) {
            cancellation.checkpoint();
            if (vertices[index] != prior->vertices()[index])
              return false;
          }
          for (std::size_t index = 0; index < indices.size(); ++index) {
            cancellation.checkpoint();
            if (indices[index] != prior->indices()[index])
              return false;
          }
          return true;
        }();
        if (exact) {
          candidate = prior;
          match->consumed = true;
          break;
        }
      }
      if (!candidate)
        candidate = std::shared_ptr<const SketchUploadChunk>(
            new SketchUploadChunk{batch.style, batch.layer, std::move(vertices),
                                  std::move(indices), bounds, bytes, hash});
      else {
        std::vector<SketchMeshVertex>{}.swap(vertices);
        std::vector<std::uint32_t>{}.swap(indices);
      }
      const std::size_t selectedRetainedBytes =
          memoryBudget.sum({sizeof(SketchUploadChunk),
                            memoryBudget.capacityBytes(candidate->vertices_),
                            memoryBudget.capacityBytes(candidate->indices_)});
      observeConstruction(selectedRetainedBytes, sourceScratchBytes);
      if (chunks.size() == chunks.capacity()) {
        const std::size_t nextCapacity = MeshBuilder::growthCapacity(
            chunks, chunks.size() + 1U, upload.maximumChunks);
        const std::size_t candidateRetainedBytes = memoryBudget.sum(
            {selectedRetainedBytes,
             memoryBudget.bytes(
                 nextCapacity - chunks.capacity(),
                 sizeof(std::shared_ptr<const SketchUploadChunk>))});
        observeConstruction(candidateRetainedBytes, sourceScratchBytes);
        chunks.reserve(nextCapacity);
      }
      if (candidate->vertices().size() >
              tessellation.maximumVertices - metrics.vertices ||
          candidate->indices().size() >
              tessellation.maximumIndices - metrics.indices)
        return std::unexpected(
            diagnostic("desktop.sketch.mesh-budget",
                       "upload chunk boundaries exceeded the mesh budget"));
      metrics.vertices += candidate->vertices().size();
      metrics.indices += candidate->indices().size();
      metrics.bytes += candidate->payloadBytes();
      retainedChunkBytesDuringBuild = memoryBudget.sum(
          {retainedChunkBytesDuringBuild, selectedRetainedBytes});
      chunks.push_back(std::move(candidate));
      observeConstruction(0U, sourceScratchBytes);
      vertices.clear();
      indices.clear();
      sources.clear();
      bounds = {};
      return {};
    };

    std::uint32_t remapGeneration = 0U;
    const auto nextRemapGeneration = [&] {
      if (remapGeneration == std::numeric_limits<std::uint32_t>::max()) {
        std::ranges::fill(remapGenerations, 0U);
        remapGeneration = 1U;
      } else {
        ++remapGeneration;
      }
    };
    std::size_t tileBegin = 0U;
    while (tileBegin < routedTriangles.size()) {
      cancellation.checkpoint();
      std::size_t tileEnd = tileBegin + 1U;
      while (tileEnd < routedTriangles.size() &&
             sameTile(routedTriangles[tileEnd], routedTriangles[tileBegin]))
        ++tileEnd;
      const SketchMeshBatch &batch =
          builder.batches[routedTriangles[tileBegin].style];
      if (remapValues.size() < batch.vertices.size()) {
        const std::size_t nextValueCapacity = MeshBuilder::growthCapacity(
            remapValues, batch.vertices.size(), tessellation.maximumVertices);
        const std::size_t nextGenerationCapacity =
            MeshBuilder::growthCapacity(remapGenerations, batch.vertices.size(),
                                        tessellation.maximumVertices);
        const std::size_t nextRemapBytes = memoryBudget.sum(
            {memoryBudget.bytes(nextValueCapacity, sizeof(std::uint32_t)),
             memoryBudget.bytes(nextGenerationCapacity,
                                sizeof(std::uint32_t))});
        const std::size_t remapGrowthBytes =
            nextRemapBytes - remapCapacityBytes;
        observeConstruction(0U, remapGrowthBytes);
        remapValues.reserve(nextValueCapacity);
        remapGenerations.reserve(nextGenerationCapacity);
        remapValues.resize(batch.vertices.size());
        remapGenerations.resize(batch.vertices.size(), 0U);
        remapCapacityBytes =
            memoryBudget.sum({memoryBudget.capacityBytes(remapValues),
                              memoryBudget.capacityBytes(remapGenerations)});
        observeConstruction(0U, 0U);
      }
      nextRemapGeneration();
      std::vector<SketchMeshVertex> vertices;
      std::vector<std::uint32_t> indices;
      std::vector<std::uint32_t> sources;
      SketchChunkBounds bounds;
      for (std::size_t triangleIndex = tileBegin; triangleIndex < tileEnd;
           ++triangleIndex) {
        cancellation.checkpoint();
        const TaggedTriangle &triangle = routedTriangles[triangleIndex];
        std::size_t addedVertices = 0;
        for (std::size_t corner = 0; corner < 3U; ++corner)
          if (remapGenerations[batch.indices[triangle.firstIndex + corner]] !=
              remapGeneration)
            ++addedVertices;
        const std::size_t nextVertexCount =
            memoryBudget.sum({vertices.size(), addedVertices});
        const std::size_t nextIndexCount =
            memoryBudget.sum({indices.size(), 3U});
        const std::size_t nextBytes = memoryBudget.sum(
            {memoryBudget.bytes(nextVertexCount, sizeof(SketchMeshVertex)),
             memoryBudget.bytes(nextIndexCount, sizeof(std::uint32_t))});
        if (!indices.empty() && nextBytes > upload.maximumChunkBytes) {
          auto finished =
              finishChunk(batch, vertices, indices, sources, bounds);
          if (!finished)
            return std::unexpected(std::move(finished.error()));
          nextRemapGeneration();
        }
        std::size_t requiredAddedVertices = 0U;
        for (std::size_t corner = 0U; corner < 3U; ++corner) {
          const std::uint32_t sourceVertex =
              batch.indices[triangle.firstIndex + corner];
          if (remapGenerations[sourceVertex] == remapGeneration)
            continue;
          bool repeated = false;
          for (std::size_t prior = 0U; prior < corner; ++prior)
            repeated = repeated || batch.indices[triangle.firstIndex + prior] ==
                                       sourceVertex;
          if (!repeated)
            ++requiredAddedVertices;
        }
        const std::size_t requiredVertexCount =
            memoryBudget.sum({vertices.size(), requiredAddedVertices});
        const std::size_t requiredIndexCount =
            memoryBudget.sum({indices.size(), 3U});
        const std::size_t requiredSourceCount =
            memoryBudget.sum({sources.size(), 1U});
        const std::size_t nextVertexCapacity = MeshBuilder::growthCapacity(
            vertices, requiredVertexCount, tessellation.maximumVertices, 8U);
        const std::size_t nextIndexCapacity = MeshBuilder::growthCapacity(
            indices, requiredIndexCount, tessellation.maximumIndices, 8U);
        const std::size_t nextSourceCapacity = MeshBuilder::growthCapacity(
            sources, requiredSourceCount, tessellation.maximumIndices / 3U, 8U);
        if (nextVertexCapacity != vertices.capacity() ||
            nextIndexCapacity != indices.capacity() ||
            nextSourceCapacity != sources.capacity()) {
          const std::size_t nextChunkScratchBytes = memoryBudget.sum(
              {memoryBudget.bytes(nextVertexCapacity, sizeof(SketchMeshVertex)),
               memoryBudget.bytes(nextIndexCapacity, sizeof(std::uint32_t)),
               memoryBudget.bytes(nextSourceCapacity, sizeof(std::uint32_t))});
          observeConstruction(0U, nextChunkScratchBytes);
          vertices.reserve(nextVertexCapacity);
          indices.reserve(nextIndexCapacity);
          sources.reserve(nextSourceCapacity);
          const std::size_t actualChunkScratchBytes =
              memoryBudget.sum({memoryBudget.capacityBytes(vertices),
                                memoryBudget.capacityBytes(indices),
                                memoryBudget.capacityBytes(sources)});
          observeConstruction(0U, actualChunkScratchBytes);
        }
        for (std::size_t corner = 0; corner < 3U; ++corner) {
          cancellation.checkpoint();
          const std::uint32_t sourceVertex =
              batch.indices[triangle.firstIndex + corner];
          if (remapGenerations[sourceVertex] != remapGeneration) {
            remapGenerations[sourceVertex] = remapGeneration;
            remapValues[sourceVertex] =
                static_cast<std::uint32_t>(vertices.size());
            vertices.push_back(batch.vertices[sourceVertex]);
            include(bounds, vertices.back());
          }
          indices.push_back(remapValues[sourceVertex]);
        }
        sources.push_back(batch.triangleSources[triangle.firstIndex / 3U]);
        bounds.maximumAnalyticDeviationMetres = std::max(
            bounds.maximumAnalyticDeviationMetres,
            batch.triangleAnalyticDeviationsMetres[triangle.firstIndex / 3U]);
      }
      auto finished = finishChunk(batch, vertices, indices, sources, bounds);
      if (!finished)
        return std::unexpected(std::move(finished.error()));
      tileBegin = tileEnd;
    }
    metrics.batches = chunks.size();

    std::ranges::sort(primitiveSpans,
                      [&](const SketchStrokePrimitiveSpanRecord &first,
                          const SketchStrokePrimitiveSpanRecord &second) {
                        cancellation.checkpoint();
                        if (first.sourceKey != second.sourceKey)
                          return first.sourceKey < second.sourceKey;
                        if (first.chunk != second.chunk)
                          return first.chunk < second.chunk;
                        return first.firstIndex < second.firstIndex;
                      });
    std::vector<SketchStrokePrimitiveSpanRecord> canonicalProvenance;
    const std::size_t requestedProvenanceBytes = memoryBudget.bytes(
        primitiveSpans.size(), sizeof(SketchStrokePrimitiveSpanRecord));
    observeConstruction(requestedProvenanceBytes, 0U);
    canonicalProvenance.reserve(primitiveSpans.size());
    const std::size_t provenanceCapacityBytesDuringBuild =
        memoryBudget.capacityBytes(canonicalProvenance);
    observeConstruction(provenanceCapacityBytesDuringBuild, 0U);
    std::uint32_t currentSource = 0U;
    std::uint32_t currentSpanCount = 0U;
    std::uint32_t currentIndexCount = 0U;
    std::size_t indexedPrimitives = 0U;
    for (const SketchStrokePrimitiveSpanRecord &record : primitiveSpans) {
      cancellation.checkpoint();
      if (indexedPrimitives == 0U || currentSource != record.sourceKey) {
        currentSource = record.sourceKey;
        currentSpanCount = 0U;
        currentIndexCount = 0U;
        ++indexedPrimitives;
      }
      if (!canonicalProvenance.empty() && currentSpanCount != 0U) {
        SketchStrokePrimitiveSpanRecord &previous = canonicalProvenance.back();
        if (previous.chunk == record.chunk &&
            previous.firstIndex + previous.indexCount == record.firstIndex) {
          if (record.indexCount > std::numeric_limits<std::uint32_t>::max() -
                                      previous.indexCount ||
              record.indexCount >
                  std::numeric_limits<std::uint32_t>::max() - currentIndexCount)
            return std::unexpected(diagnostic(
                "desktop.sketch.primitive-index-limit",
                "sketch primitive range exceeds its packed index range"));
          previous.indexCount += record.indexCount;
          currentIndexCount += record.indexCount;
          continue;
        }
      }
      if (currentSpanCount == std::numeric_limits<std::uint32_t>::max() ||
          record.indexCount >
              std::numeric_limits<std::uint32_t>::max() - currentIndexCount)
        return std::unexpected(diagnostic(
            "desktop.sketch.primitive-index-limit",
            "sketch primitive range exceeds its packed index range"));
      canonicalProvenance.push_back(record);
      ++currentSpanCount;
      currentIndexCount += record.indexCount;
    }
    std::vector<SketchSceneMesh::SpatialNode> spatialIndex;
    std::size_t spatialOrderCapacity = 0U;
    std::uint32_t spatialRoot = 0;
    if (!chunks.empty()) {
      constexpr std::uint64_t maximumSpatialChunks =
          (static_cast<std::uint64_t>(
               std::numeric_limits<std::uint32_t>::max()) +
           1U) /
          2U;
      if (chunks.size() > maximumSpatialChunks)
        return std::unexpected(
            diagnostic("desktop.sketch.chunk-budget",
                       "sketch spatial index exceeds its packed node range"));
      const std::size_t spatialNodeCount =
          memoryBudget.sum({chunks.size(), chunks.size()}) - 1U;
      const std::size_t requestedOrderBytes =
          memoryBudget.bytes(chunks.size(), sizeof(std::uint32_t));
      const std::size_t requestedSpatialRetainedBytes = memoryBudget.sum(
          {provenanceCapacityBytesDuringBuild,
           memoryBudget.bytes(spatialNodeCount,
                              sizeof(SketchSceneMesh::SpatialNode))});
      observeConstruction(requestedSpatialRetainedBytes, requestedOrderBytes);
      std::vector<std::uint32_t> order(chunks.size());
      spatialOrderCapacity = order.capacity();
      for (std::size_t index = 0; index < order.size(); ++index) {
        cancellation.checkpoint();
        order[index] = static_cast<std::uint32_t>(index);
      }
      spatialIndex.reserve(spatialNodeCount);
      observeConstruction(
          memoryBudget.sum({provenanceCapacityBytesDuringBuild,
                            memoryBudget.capacityBytes(spatialIndex)}),
          memoryBudget.capacityBytes(order));
      const std::function<std::uint32_t(std::size_t, std::size_t)> buildIndex =
          [&](std::size_t begin, std::size_t end) -> std::uint32_t {
        cancellation.checkpoint();
        SketchChunkBounds bounds;
        for (std::size_t index = begin; index < end; ++index) {
          cancellation.checkpoint();
          bounds = merged(bounds, chunks[order[index]]->bounds());
        }
        const std::uint32_t node =
            static_cast<std::uint32_t>(spatialIndex.size());
        spatialIndex.push_back({bounds});
        if (end - begin == 1U) {
          spatialIndex[node].first = order[begin];
          spatialIndex[node].leaf = true;
          return node;
        }
        const bool splitX =
            static_cast<long double>(bounds.maximumX) - bounds.minimumX >=
            static_cast<long double>(bounds.maximumY) - bounds.minimumY;
        const std::size_t middle = begin + (end - begin) / 2U;
        auto compare = [&](std::uint32_t first, std::uint32_t second) {
          cancellation.checkpoint();
          const SketchChunkBounds &left = chunks[first]->bounds();
          const SketchChunkBounds &right = chunks[second]->bounds();
          const double leftCenter =
              splitX ? std::midpoint(left.minimumX, left.maximumX)
                     : std::midpoint(left.minimumY, left.maximumY);
          const double rightCenter =
              splitX ? std::midpoint(right.minimumX, right.maximumX)
                     : std::midpoint(right.minimumY, right.maximumY);
          return leftCenter != rightCenter ? leftCenter < rightCenter
                                           : first < second;
        };
        std::ranges::nth_element(
            order.begin() + static_cast<std::ptrdiff_t>(begin),
            order.begin() + static_cast<std::ptrdiff_t>(middle),
            order.begin() + static_cast<std::ptrdiff_t>(end), compare);
        spatialIndex[node].first = buildIndex(begin, middle);
        spatialIndex[node].second = buildIndex(middle, end);
        return node;
      };
      spatialRoot = buildIndex(0U, order.size());
    }
    std::size_t retainedChunkBytes = 0U;
    for (const auto &chunk : chunks)
      retainedChunkBytes =
          memoryBudget.sum({retainedChunkBytes, sizeof(SketchUploadChunk),
                            memoryBudget.capacityBytes(chunk->vertices_),
                            memoryBudget.capacityBytes(chunk->indices_)});
    std::vector<render::SketchStyle> ownedStyles(source.styles.begin(),
                                                 source.styles.end());
    std::size_t sourceBatchScratchBytes =
        memoryBudget.capacityBytes(builder.batches);
    for (const SketchMeshBatch &batch : builder.batches)
      sourceBatchScratchBytes = memoryBudget.sum(
          {sourceBatchScratchBytes, memoryBudget.capacityBytes(batch.vertices),
           memoryBudget.capacityBytes(batch.indices),
           memoryBudget.capacityBytes(batch.triangleSources),
           memoryBudget.capacityBytes(batch.triangleAnalyticDeviationsMetres)});
    const std::size_t provenanceBytes =
        memoryBudget.capacityBytes(canonicalProvenance);
    metrics.retainedMeshBytes =
        memoryBudget.sum({sizeof(SketchSceneMesh), retainedChunkBytes,
                          memoryBudget.capacityBytes(ownedStyles),
                          memoryBudget.capacityBytes(chunks),
                          memoryBudget.capacityBytes(spatialIndex)});
    metrics.preparationScratchBytes = memoryBudget.sum(
        {sourceBatchScratchBytes, memoryBudget.capacityBytes(routedTriangles),
         memoryBudget.capacityBytes(reusable),
         memoryBudget.capacityBytes(remapValues),
         memoryBudget.capacityBytes(remapGenerations),
         memoryBudget.bytes(maximumChunkSourceCapacity, sizeof(std::uint32_t)),
         memoryBudget.capacityBytes(primitiveSpans),
         memoryBudget.bytes(spatialOrderCapacity, sizeof(std::uint32_t)),
         memoryBudget.capacityBytes(sourceFingerprints),
         memoryBudget.capacityBytes(sortedSourceKeys)});
    metrics.preparationScratchBytes = std::max(
        metrics.preparationScratchBytes, memoryBudget.maximumScratchBytes);
    const std::size_t retainedOutputBytes =
        memoryBudget.sum({metrics.retainedMeshBytes, provenanceBytes});
    metrics.peakPreparationMeshBytes = memoryBudget.sum(
        {retainedOutputBytes, metrics.preparationScratchBytes});
    memoryBudget.observe(retainedOutputBytes, metrics.preparationScratchBytes);
    cancellation.checkpointNow();
    const std::size_t provenanceSpanCount = canonicalProvenance.size();
    return SketchStrokeMeshBuildOutput{
        SketchSceneMesh{origin, lod, std::move(ownedStyles), std::move(chunks),
                        std::move(spatialIndex), spatialRoot,
                        upload.maximumChunkBytes, spatialTileSizeMetres,
                        metrics},
        std::move(canonicalProvenance),
        indexedPrimitives,
        provenanceSpanCount,
        provenanceBytes,
        retainedOutputBytes,
        metrics.preparationScratchBytes,
        metrics.peakPreparationMeshBytes};
  } catch (PreparationPayloadBudget::Rejected &rejected) {
    return std::unexpected(std::move(rejected.failure));
  } catch (const detail::SketchProjectionCancelled &) {
    return std::unexpected(cancelledPreparation());
  } catch (const std::bad_alloc &) {
    return std::unexpected(diagnostic("desktop.sketch.mesh-allocation",
                                      "sketch mesh allocation failed"));
  } catch (const std::length_error &) {
    return std::unexpected(
        diagnostic("desktop.sketch.mesh-budget",
                   "sketch mesh preparation exceeded container capacity"));
  }
}
} // namespace kearne::ui
