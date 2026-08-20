#include "engineering_grid_item.hpp"

#include <kearne/testkit/property.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <new>
#include <numbers>
#include <stdexcept>

namespace {

thread_local bool measureAllocations = false;
thread_local std::size_t measuredAllocations = 0;

#if defined(__GNUC__) || defined(__clang__)
[[gnu::noinline]]
#endif
void *allocateMemory(std::size_t size) {
  return std::malloc(size == 0U ? 1U : size);
}

#if defined(__GNUC__) || defined(__clang__)
[[gnu::noinline]]
#endif
void releaseMemory(void *memory) {
  std::free(memory);
}

} // namespace

void *operator new(std::size_t size) {
  if (measureAllocations)
    ++measuredAllocations;
  if (void *memory = allocateMemory(size))
    return memory;
  throw std::bad_alloc{};
}

void *operator new[](std::size_t size) { return ::operator new(size); }
void operator delete(void *memory) noexcept { releaseMemory(memory); }
void operator delete[](void *memory) noexcept { releaseMemory(memory); }
void operator delete(void *memory, std::size_t) noexcept {
  releaseMemory(memory);
}
void operator delete[](void *memory, std::size_t) noexcept {
  releaseMemory(memory);
}

namespace {

using namespace kearne::ui;

constexpr std::size_t layerCapacity =
    EngineeringGridPolicy::maximumLayerVertices;
constexpr std::size_t axisCapacity = EngineeringGridPolicy::maximumAxisVertices;

struct Buffers {
  std::array<QSGGeometry::Point2D, layerCapacity> minor{};
  std::array<QSGGeometry::Point2D, layerCapacity> major{};
  std::array<QSGGeometry::Point2D, axisCapacity> axisX{};
  std::array<QSGGeometry::Point2D, axisCapacity> axisY{};

  EngineeringGridLineBuffers spans() { return {minor, major, axisX, axisY}; }
};

void require(bool condition, const char *message) {
  if (!condition)
    throw std::runtime_error(message);
}

bool close(double first, double second, double tolerance = 1.0e-9) {
  return std::abs(first - second) <=
         tolerance * std::max({1.0, std::abs(first), std::abs(second)});
}

bool isOneTwoFive(double multiplier) {
  if (!std::isfinite(multiplier) || multiplier < 1.0)
    return false;
  const double exponent = std::floor(std::log10(multiplier));
  const double normalized = multiplier / std::pow(10.0, exponent);
  return close(normalized, 1.0) || close(normalized, 2.0) ||
         close(normalized, 5.0) || close(normalized, 10.0);
}

template <std::size_t Size>
void requireFiniteBounded(
    const std::array<QSGGeometry::Point2D, Size> &vertices, std::size_t count,
    const EngineeringGridProjection &projection) {
  const double margin = std::max({projection.minorLineWidthPixels,
                                  projection.majorLineWidthPixels,
                                  projection.axisLineWidthPixels}) +
                        1.0;
  for (std::size_t index = 0; index < count; ++index) {
    const auto point = vertices[index];
    require(std::isfinite(point.x) && std::isfinite(point.y),
            "grid emitted a non-finite vertex");
    require(point.x >= -margin &&
                point.x <= projection.viewportLogicalPixels.width() + margin &&
                point.y >= -margin &&
                point.y <= projection.viewportLogicalPixels.height() + margin,
            "grid vertex escaped its clipped viewport");
  }
  require(count % 3U == 0U, "grid emitted an incomplete triangle");
  for (std::size_t index = 0; index < count; index += 3U) {
    const auto &first = vertices[index];
    const auto &second = vertices[index + 1U];
    const auto &third = vertices[index + 2U];
    const double twiceArea = (static_cast<double>(second.x) - first.x) *
                                 (static_cast<double>(third.y) - first.y) -
                             (static_cast<double>(second.y) - first.y) *
                                 (static_cast<double>(third.x) - first.x);
    require(std::isfinite(twiceArea) && twiceArea != 0.0,
            "grid emitted a degenerate GPU triangle");
  }
}

void requireContract(const EngineeringGridProjection &projection,
                     const EngineeringGridMetrics &metrics,
                     const Buffers &buffers) {
  require(metrics.status == EngineeringGridBuildStatus::Built,
          "valid grid projection did not build");
  require(
      metrics.xFamilyLines <= EngineeringGridPolicy::maximumLinesPerFamily &&
          metrics.yFamilyLines <= EngineeringGridPolicy::maximumLinesPerFamily,
      "grid family exceeded its fixed line budget");
  require(metrics.xFamilyLines + metrics.yFamilyLines ==
                  metrics.minorLines + metrics.majorLines &&
              metrics.minorVertices ==
                  metrics.minorLines * EngineeringGridPolicy::verticesPerLine &&
              metrics.majorVertices ==
                  metrics.majorLines * EngineeringGridPolicy::verticesPerLine &&
              metrics.axisXVertices <= axisCapacity &&
              metrics.axisYVertices <= axisCapacity &&
              metrics.axisLines * EngineeringGridPolicy::verticesPerLine ==
                  metrics.axisXVertices + metrics.axisYVertices,
          "grid metrics disagree with emitted geometry");
  const double basePixels =
      projection.minorSpacingMetres * projection.pixelsPerMetre;
  require(metrics.displayedMinorSpacingPixels + 1.0e-10 >=
                  projection.minimumLineSpacingPixels &&
              close(metrics.displayedMinorSpacingPixels,
                    metrics.displayedMinorSpacingMetres *
                        projection.pixelsPerMetre) &&
              isOneTwoFive(metrics.displayedMinorSpacingPixels / basePixels),
          "grid violated its exact 1-2-5 density progression");
  requireFiniteBounded(buffers.minor, metrics.minorVertices, projection);
  requireFiniteBounded(buffers.major, metrics.majorVertices, projection);
  requireFiniteBounded(buffers.axisX, metrics.axisXVertices, projection);
  requireFiniteBounded(buffers.axisY, metrics.axisYVertices, projection);
}

QPointF lineMidpoint(const QSGGeometry::Point2D *vertices) {
  return {(static_cast<double>(vertices[0].x) + vertices[5].x + vertices[1].x +
           vertices[2].x) *
              0.25,
          (static_cast<double>(vertices[0].y) + vertices[5].y + vertices[1].y +
           vertices[2].y) *
              0.25};
}

void requireLayerSemantics(std::span<const QSGGeometry::Point2D> vertices,
                           bool expectedMajor,
                           const EngineeringGridProjection &projection,
                           const EngineeringGridMetrics &metrics) {
  if (vertices.empty())
    return;
  const std::size_t lineCount =
      vertices.size() / EngineeringGridPolicy::verticesPerLine;
  const QSGGeometry::Point2D *line = nullptr;
  double longestSquared = 0.0;
  for (std::size_t index = 0; index < lineCount; ++index) {
    const auto *candidate =
        vertices.data() + index * EngineeringGridPolicy::verticesPerLine;
    const QPointF start{
        (static_cast<double>(candidate[0].x) + candidate[5].x) * 0.5,
        (static_cast<double>(candidate[0].y) + candidate[5].y) * 0.5};
    const QPointF end{
        (static_cast<double>(candidate[1].x) + candidate[2].x) * 0.5,
        (static_cast<double>(candidate[1].y) + candidate[2].y) * 0.5};
    const QPointF direction = end - start;
    const double lengthSquared = QPointF::dotProduct(direction, direction);
    if (lengthSquared > longestSquared) {
      longestSquared = lengthSquared;
      line = candidate;
    }
  }
  require(line != nullptr && longestSquared > 1.0e-12,
          "grid layer retained only degenerate lines");
  const QPointF start{(static_cast<double>(line[0].x) + line[5].x) * 0.5,
                      (static_cast<double>(line[0].y) + line[5].y) * 0.5};
  const QPointF end{(static_cast<double>(line[1].x) + line[2].x) * 0.5,
                    (static_cast<double>(line[1].y) + line[2].y) * 0.5};
  const QPointF direction = end - start;
  const QPointF midpoint = lineMidpoint(line);
  const double cosine = std::cos(projection.rotationRadians);
  const double sine = std::sin(projection.rotationRadians);
  const QPointF worldX{cosine, -sine};
  const QPointF worldY{-sine, -cosine};
  const double alongX = std::abs(QPointF::dotProduct(direction, worldX));
  const double alongY = std::abs(QPointF::dotProduct(direction, worldY));

  const bool constantX = alongY > alongX;
  const QPointF normal = constantX ? worldX : worldY;
  const QPointF viewportCenter{projection.viewportLogicalPixels.width() * 0.5,
                               projection.viewportLogicalPixels.height() * 0.5};
  const double screenOffset =
      QPointF::dotProduct(midpoint - viewportCenter, normal);
  const double centerDelta =
      constantX
          ? projection.viewCenterMetres.x() - projection.gridOriginMetres.x()
          : projection.viewCenterMetres.y() - projection.gridOriginMetres.y();
  const double coordinate =
      centerDelta + screenOffset / projection.pixelsPerMetre;
  const double lineIndex =
      std::nearbyint(coordinate / metrics.displayedMinorSpacingMetres);
  const double residual =
      coordinate - lineIndex * metrics.displayedMinorSpacingMetres;
  const double tolerance =
      std::max(0.002 / projection.pixelsPerMetre,
               64.0 * std::numeric_limits<double>::epsilon() *
                   std::max({1.0, std::abs(coordinate),
                             std::abs(lineIndex *
                                      metrics.displayedMinorSpacingMetres)}));
  require(std::abs(residual) <= tolerance,
          "grid line left its canonical SI lattice");
  const bool actualMajor =
      std::abs(std::remainder(
          lineIndex, static_cast<double>(projection.majorInterval))) < 0.25;
  require(actualMajor == expectedMajor,
          "grid line style disagrees with its canonical lattice index");
}

void requireAxisSemantics(std::span<const QSGGeometry::Point2D> vertices,
                          bool xAxis,
                          const EngineeringGridProjection &projection) {
  if (vertices.empty())
    return;
  const QPointF midpoint = lineMidpoint(vertices.data());
  const double cosine = std::cos(projection.rotationRadians);
  const double sine = std::sin(projection.rotationRadians);
  const QPointF normal =
      xAxis ? QPointF{-sine, -cosine} : QPointF{cosine, -sine};
  const QPointF viewportCenter{projection.viewportLogicalPixels.width() * 0.5,
                               projection.viewportLogicalPixels.height() * 0.5};
  const double centerDelta =
      xAxis ? projection.viewCenterMetres.y() - projection.gridOriginMetres.y()
            : projection.viewCenterMetres.x() - projection.gridOriginMetres.x();
  const double residual =
      centerDelta + QPointF::dotProduct(midpoint - viewportCenter, normal) /
                        projection.pixelsPerMetre;
  require(std::abs(residual) <= 0.002 / projection.pixelsPerMetre,
          "grid axis left its canonical origin");
}

void requireCanonicalSemantics(const EngineeringGridProjection &projection,
                               const EngineeringGridMetrics &metrics,
                               const Buffers &buffers) {
  requireLayerSemantics(
      std::span<const QSGGeometry::Point2D>{buffers.minor}.first(
          metrics.minorVertices),
      false, projection, metrics);
  requireLayerSemantics(
      std::span<const QSGGeometry::Point2D>{buffers.major}.first(
          metrics.majorVertices),
      true, projection, metrics);
  requireAxisSemantics(
      std::span<const QSGGeometry::Point2D>{buffers.axisX}.first(
          metrics.axisXVertices),
      true, projection);
  requireAxisSemantics(
      std::span<const QSGGeometry::Point2D>{buffers.axisY}.first(
          metrics.axisYVertices),
      false, projection);
}

void verifyCanonicalGrid() {
  EngineeringGridProjection projection;
  projection.viewportLogicalPixels = {801.0, 601.0};
  projection.pixelsPerMetre = 20'000.0;
  projection.minorSpacingMetres = 0.001;
  projection.majorInterval = 5;
  Buffers buffers;
  const EngineeringGridMetrics metrics =
      buildEngineeringGrid(projection, buffers.spans());
  requireContract(projection, metrics, buffers);
  require(metrics.axisLines == 2U && metrics.axisXVertices == axisCapacity &&
              metrics.axisYVertices == axisCapacity,
          "canonical origin did not produce two independent axes");
  require(metrics.displayedMinorSpacingMetres == projection.minorSpacingMetres,
          "canonical grid changed already-readable spacing");
}

void verifyInvalidInputsClearGeometry() {
  Buffers buffers;
  EngineeringGridProjection projection;
  projection.viewportLogicalPixels = {640.0, 480.0};
  projection.minorSpacingMetres = 0.0;
  EngineeringGridMetrics metrics =
      buildEngineeringGrid(projection, buffers.spans());
  require(metrics.status == EngineeringGridBuildStatus::InvalidProjection &&
              metrics.minorVertices == 0U && metrics.majorVertices == 0U &&
              metrics.axisXVertices == 0U && metrics.axisYVertices == 0U,
          "invalid spacing retained grid geometry");

  projection.minorSpacingMetres = 0.01;
  projection.viewportLogicalPixels = {};
  metrics = buildEngineeringGrid(projection, buffers.spans());
  require(metrics.status == EngineeringGridBuildStatus::EmptyViewport &&
              metrics.minorVertices == 0U && metrics.majorVertices == 0U,
          "empty viewport produced grid geometry");

  projection.viewportLogicalPixels = {640.0, 480.0};
  EngineeringGridLineBuffers insufficient{
      std::span<QSGGeometry::Point2D>{buffers.minor}.first(12), buffers.major,
      buffers.axisX, buffers.axisY};
  metrics = buildEngineeringGrid(projection, insufficient);
  require(metrics.status == EngineeringGridBuildStatus::CapacityExceeded &&
              metrics.minorVertices == 0U && metrics.majorVertices == 0U,
          "undersized retained buffer was not rejected before writing");
}

void verifyPeriodicCameraContinuity() {
  EngineeringGridProjection projection;
  projection.viewportLogicalPixels = {750.0, 460.0};
  projection.viewCenterMetres = {20.123, -17.456};
  projection.gridOriginMetres = {};
  projection.pixelsPerMetre = 8'000.0;
  projection.rotationRadians = 0.31;
  projection.minorSpacingMetres = 0.001;
  projection.majorInterval = 5;
  Buffers firstBuffers;
  const EngineeringGridMetrics first =
      buildEngineeringGrid(projection, firstBuffers.spans());
  requireContract(projection, first, firstBuffers);

  projection.viewCenterMetres +=
      QPointF{first.displayedMinorSpacingMetres * projection.majorInterval,
              -first.displayedMinorSpacingMetres * projection.majorInterval};
  Buffers secondBuffers;
  const EngineeringGridMetrics second =
      buildEngineeringGrid(projection, secondBuffers.spans());
  requireContract(projection, second, secondBuffers);
  require(first.minorVertices == second.minorVertices &&
              first.majorVertices == second.majorVertices &&
              first.axisLines == 0U && second.axisLines == 0U,
          "periodic camera translation changed regular grid topology");
  for (std::size_t index = 0; index < first.minorVertices; ++index) {
    require(close(firstBuffers.minor[index].x, secondBuffers.minor[index].x,
                  1.0e-5) &&
                close(firstBuffers.minor[index].y, secondBuffers.minor[index].y,
                      1.0e-5),
            "periodic camera translation moved minor grid geometry");
  }
  for (std::size_t index = 0; index < first.majorVertices; ++index) {
    require(close(firstBuffers.major[index].x, secondBuffers.major[index].x,
                  1.0e-5) &&
                close(firstBuffers.major[index].y, secondBuffers.major[index].y,
                      1.0e-5),
            "periodic camera translation moved major grid geometry");
  }
}

void verifyWorldExtentDoesNotSetWork() {
  EngineeringGridProjection projection;
  projection.viewportLogicalPixels = {1'920.0, 1'080.0};
  projection.viewCenterMetres = {1.0e200, -1.0e200};
  projection.pixelsPerMetre = 1'000.0;
  projection.minorSpacingMetres = 0.001;
  projection.majorInterval = std::numeric_limits<int>::max();
  projection.minorLineWidthPixels = 256.0;
  projection.majorLineWidthPixels = 384.0;
  projection.axisLineWidthPixels = 512.0;
  Buffers buffers;
  const EngineeringGridMetrics metrics =
      buildEngineeringGrid(projection, buffers.spans());
  requireContract(projection, metrics, buffers);
  require(metrics.axisLines == 0U,
          "offscreen axes consumed work at extreme world coordinates");
}

void verifyFloatCollapsedWidthsAreSkippedAtomically() {
  EngineeringGridProjection projection;
  projection.viewportLogicalPixels = {1.0e20, 1.0e20};
  projection.viewCenterMetres = {1.23e17, 1.23e17};
  projection.pixelsPerMetre = 1.0;
  projection.minorSpacingMetres = 1.0;
  projection.minorLineWidthPixels = 1.0;
  projection.majorLineWidthPixels = 1.5;
  projection.axisLineWidthPixels = 2.0;
  Buffers buffers;
  constexpr float sentinelX = -12'345.0F;
  constexpr float sentinelY = 67'890.0F;
  const auto initialize = [](auto &vertices) {
    for (auto &vertex : vertices)
      vertex.set(sentinelX, sentinelY);
  };
  initialize(buffers.minor);
  initialize(buffers.major);
  initialize(buffers.axisX);
  initialize(buffers.axisY);

  const EngineeringGridMetrics metrics =
      buildEngineeringGrid(projection, buffers.spans());
  require(metrics.status == EngineeringGridBuildStatus::Built &&
              metrics.minorVertices == 0U && metrics.majorVertices == 0U &&
              metrics.axisXVertices == 0U && metrics.axisYVertices == 0U,
          "float-collapsed line retained degenerate GPU geometry");
  const auto unchanged = [](const auto &vertices) {
    return std::ranges::all_of(vertices, [](const auto &vertex) {
      return vertex.x == sentinelX && vertex.y == sentinelY;
    });
  };
  require(unchanged(buffers.minor) && unchanged(buffers.major) &&
              unchanged(buffers.axisX) && unchanged(buffers.axisY),
          "skipped float-collapsed line partially changed a retained buffer");
}

void verifyGeneratedProjections(
    const kearne::testkit::PropertyProfile &profile) {
  kearne::testkit::checkProperty(
      "bounded engineering grid", profile,
      [](kearne::testkit::Random &random, std::uint64_t index) {
        EngineeringGridProjection projection;
        projection.viewportLogicalPixels = {random.between(64.0, 8'192.0),
                                            random.between(64.0, 8'192.0)};
        projection.viewCenterMetres = {random.between(-1.0e9, 1.0e9),
                                       random.between(-1.0e9, 1.0e9)};
        projection.gridOriginMetres = {random.between(-1.0e6, 1.0e6),
                                       random.between(-1.0e6, 1.0e6)};
        projection.pixelsPerMetre = std::pow(10.0, random.between(-2.0, 9.0));
        projection.rotationRadians =
            random.between(-std::numbers::pi, std::numbers::pi);
        projection.minorSpacingMetres =
            std::pow(10.0, random.between(-9.0, 5.0));
        projection.majorInterval = static_cast<int>(random.next() % 15U) + 2;
        projection.minimumLineSpacingPixels = random.between(4.0, 32.0);
        projection.minorLineWidthPixels = random.between(0.5, 2.0);
        projection.majorLineWidthPixels = random.between(0.75, 3.0);
        projection.axisLineWidthPixels = random.between(1.0, 4.0);

        const bool checkSemantics = (index & 3U) == 0U;
        if (checkSemantics) {
          projection.viewCenterMetres = {random.between(-10.0, 10.0),
                                         random.between(-10.0, 10.0)};
          projection.gridOriginMetres = {random.between(-1.0, 1.0),
                                         random.between(-1.0, 1.0)};
          projection.pixelsPerMetre = std::pow(10.0, random.between(2.0, 6.0));
          projection.minorSpacingMetres =
              std::pow(10.0, random.between(-5.0, 0.0));
        }

        Buffers buffers;
        measuredAllocations = 0U;
        measureAllocations = true;
        const EngineeringGridMetrics metrics =
            buildEngineeringGrid(projection, buffers.spans());
        measureAllocations = false;
        require(measuredAllocations == 0U,
                "warm grid projection performed a heap allocation");
        requireContract(projection, metrics, buffers);
        if (checkSemantics)
          requireCanonicalSemantics(projection, metrics, buffers);
      });
}

} // namespace

int main() {
  try {
    verifyCanonicalGrid();
    verifyInvalidInputsClearGeometry();
    verifyPeriodicCameraContinuity();
    verifyWorldExtentDoesNotSetWork();
    verifyFloatCollapsedWidthsAreSkippedAtomically();
    const auto profile = kearne::testkit::propertyProfile();
    verifyGeneratedProjections(profile);
    std::cout << "verified " << profile.iterations
              << " bounded engineering-grid projections\n";
    return 0;
  } catch (const std::exception &error) {
    measureAllocations = false;
    std::cerr << error.what() << '\n';
    return 1;
  }
}
