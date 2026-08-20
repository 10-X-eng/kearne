#include "engineering_grid_item.hpp"

#include <QSGFlatColorMaterial>
#include <QSGGeometryNode>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace kearne::ui {
namespace {

constexpr double coordinateEpsilon = 1.0e-12;
constexpr std::size_t lineCapacity =
    EngineeringGridPolicy::maximumLinesPerFamily;
constexpr std::size_t layerVertexCapacity =
    EngineeringGridPolicy::maximumLayerVertices;
constexpr std::size_t axisVertexCapacity =
    EngineeringGridPolicy::maximumAxisVertices;

bool finitePoint(QPointF point) {
  return std::isfinite(point.x()) && std::isfinite(point.y());
}

bool validProjection(const EngineeringGridProjection &projection) {
  constexpr double maximumScreenCoordinate =
      static_cast<double>(std::numeric_limits<float>::max()) * 0.25;
  return std::isfinite(projection.viewportLogicalPixels.width()) &&
         std::isfinite(projection.viewportLogicalPixels.height()) &&
         projection.viewportLogicalPixels.width() <= maximumScreenCoordinate &&
         projection.viewportLogicalPixels.height() <= maximumScreenCoordinate &&
         finitePoint(projection.viewCenterMetres) &&
         finitePoint(projection.gridOriginMetres) &&
         std::isfinite(projection.pixelsPerMetre) &&
         projection.pixelsPerMetre > 0.0 &&
         std::isfinite(projection.rotationRadians) &&
         std::isfinite(projection.minorSpacingMetres) &&
         projection.minorSpacingMetres > 0.0 && projection.majorInterval > 0 &&
         std::isfinite(projection.minimumLineSpacingPixels) &&
         projection.minimumLineSpacingPixels > 0.0 &&
         std::isfinite(projection.minorLineWidthPixels) &&
         projection.minorLineWidthPixels > 0.0 &&
         projection.minorLineWidthPixels <= maximumScreenCoordinate &&
         std::isfinite(projection.majorLineWidthPixels) &&
         projection.majorLineWidthPixels > 0.0 &&
         projection.majorLineWidthPixels <= maximumScreenCoordinate &&
         std::isfinite(projection.axisLineWidthPixels) &&
         projection.axisLineWidthPixels > 0.0 &&
         projection.axisLineWidthPixels <= maximumScreenCoordinate;
}

double oneTwoFiveCeiling(double ratio) {
  if (!std::isfinite(ratio) || ratio <= 0.0)
    return std::numeric_limits<double>::quiet_NaN();
  const double exponent = std::floor(std::log10(ratio));
  const double decade = std::pow(10.0, exponent);
  if (!std::isfinite(decade) || decade <= 0.0)
    return std::numeric_limits<double>::quiet_NaN();
  const double normalized = ratio / decade;
  const double mantissa = normalized <= 1.0   ? 1.0
                          : normalized <= 2.0 ? 2.0
                          : normalized <= 5.0 ? 5.0
                                              : 10.0;
  const double multiplier = mantissa * decade;
  return std::isfinite(multiplier) ? multiplier
                                   : std::numeric_limits<double>::quiet_NaN();
}

bool clipAxis(double point, double direction, double minimum, double maximum,
              double &first, double &last) {
  if (std::abs(direction) <= coordinateEpsilon)
    return point >= minimum && point <= maximum;
  double near = (minimum - point) / direction;
  double far = (maximum - point) / direction;
  if (near > far)
    std::swap(near, far);
  first = std::max(first, near);
  last = std::min(last, far);
  return first <= last;
}

bool clipInfiniteLine(QPointF point, QPointF direction, QSizeF viewport,
                      QPointF &start, QPointF &end) {
  double first = -std::numeric_limits<double>::infinity();
  double last = std::numeric_limits<double>::infinity();
  if (!clipAxis(point.x(), direction.x(), 0.0, viewport.width(), first, last) ||
      !clipAxis(point.y(), direction.y(), 0.0, viewport.height(), first,
                last) ||
      !std::isfinite(first) || !std::isfinite(last))
    return false;
  start = point + direction * first;
  end = point + direction * last;
  return finitePoint(start) && finitePoint(end);
}

bool appendLine(std::span<QSGGeometry::Point2D> vertices,
                std::size_t &vertexCount, QPointF start, QPointF end,
                double width) {
  if (vertexCount + EngineeringGridPolicy::verticesPerLine > vertices.size())
    return false;
  const QPointF segment = end - start;
  const double length = std::hypot(segment.x(), segment.y());
  if (!std::isfinite(length) || length <= coordinateEpsilon)
    return true;
  const float startX = static_cast<float>(start.x());
  const float startY = static_cast<float>(start.y());
  const float endX = static_cast<float>(end.x());
  const float endY = static_cast<float>(end.y());
  if (startX == endX && startY == endY)
    return true;
  const QPointF normal{-segment.y() * width / (2.0 * length),
                       segment.x() * width / (2.0 * length)};
  const std::array<QPointF, EngineeringGridPolicy::verticesPerLine> quad{
      start + normal, end + normal, end - normal,
      start + normal, end - normal, start - normal};
  std::array<QSGGeometry::Point2D, EngineeringGridPolicy::verticesPerLine>
      encoded;
  for (std::size_t index = 0; index < quad.size(); ++index) {
    const QPointF point = quad[index];
    if (!finitePoint(point))
      return false;
    encoded[index].set(static_cast<float>(point.x()),
                       static_cast<float>(point.y()));
  }
  const auto finiteTriangleArea = [&](std::size_t first, std::size_t second,
                                      std::size_t third) {
    const double firstX = encoded[second].x - encoded[first].x;
    const double firstY = encoded[second].y - encoded[first].y;
    const double secondX = encoded[third].x - encoded[first].x;
    const double secondY = encoded[third].y - encoded[first].y;
    const double twiceArea = firstX * secondY - firstY * secondX;
    return std::isfinite(twiceArea) && twiceArea != 0.0;
  };
  if (!finiteTriangleArea(0U, 1U, 2U) || !finiteTriangleArea(3U, 4U, 5U))
    return true;
  std::ranges::copy(encoded, vertices.begin() +
                                 static_cast<std::ptrdiff_t>(vertexCount));
  vertexCount += encoded.size();
  return true;
}

int positiveModulo(std::int64_t value, int divisor) {
  const std::int64_t remainder = value % divisor;
  return static_cast<int>(remainder < 0 ? remainder + divisor : remainder);
}

int nearestLinePhase(double centerDelta, double spacing, int interval) {
  const double period = spacing * static_cast<double>(interval);
  if (!std::isfinite(period) || period <= 0.0)
    return 0;
  const double phase =
      std::nearbyint(std::remainder(centerDelta, period) / spacing);
  if (!std::isfinite(phase) ||
      phase < static_cast<double>(std::numeric_limits<int>::min()) ||
      phase > static_cast<double>(std::numeric_limits<int>::max()))
    return 0;
  return positiveModulo(static_cast<int>(phase), interval);
}

struct FamilyOutput {
  std::span<QSGGeometry::Point2D> minor;
  std::span<QSGGeometry::Point2D> major;
  std::size_t &minorVertices;
  std::size_t &majorVertices;
  std::size_t &minorLines;
  std::size_t &majorLines;
};

void clearGeometryMetrics(EngineeringGridMetrics &metrics) {
  metrics.xFamilyLines = 0;
  metrics.yFamilyLines = 0;
  metrics.minorLines = 0;
  metrics.majorLines = 0;
  metrics.axisLines = 0;
  metrics.minorVertices = 0;
  metrics.majorVertices = 0;
  metrics.axisXVertices = 0;
  metrics.axisYVertices = 0;
}

EngineeringGridBuildStatus
emitFamily(const EngineeringGridProjection &projection, QPointF viewportCenter,
           QPointF normal, QPointF direction, double centerDelta,
           double displayedSpacingMetres, double displayedSpacingPixels,
           FamilyOutput output, std::size_t &familyLines) {
  const double halfWidth = projection.viewportLogicalPixels.width() * 0.5;
  const double halfHeight = projection.viewportLogicalPixels.height() * 0.5;
  const double extent =
      std::abs(normal.x()) * halfWidth + std::abs(normal.y()) * halfHeight;
  const double centerOffsetPixels =
      -std::remainder(centerDelta, displayedSpacingMetres) *
      projection.pixelsPerMetre;
  if (!std::isfinite(extent) || !std::isfinite(centerOffsetPixels))
    return EngineeringGridBuildStatus::InvalidProjection;

  const double margin = std::max({projection.minorLineWidthPixels,
                                  projection.majorLineWidthPixels,
                                  projection.axisLineWidthPixels});
  const double firstValue = std::ceil((-extent - margin - centerOffsetPixels) /
                                      displayedSpacingPixels);
  const double lastValue = std::floor((extent + margin - centerOffsetPixels) /
                                      displayedSpacingPixels);
  if (!std::isfinite(firstValue) || !std::isfinite(lastValue))
    return EngineeringGridBuildStatus::InvalidProjection;
  if (firstValue > lastValue)
    return EngineeringGridBuildStatus::Built;
  if (firstValue < static_cast<double>(std::numeric_limits<int>::min()) ||
      lastValue > static_cast<double>(std::numeric_limits<int>::max()))
    return EngineeringGridBuildStatus::CapacityExceeded;
  const int first = static_cast<int>(firstValue);
  const int last = static_cast<int>(lastValue);
  const std::size_t candidateCount =
      static_cast<std::size_t>(static_cast<std::int64_t>(last) - first + 1);
  if (candidateCount > lineCapacity)
    return EngineeringGridBuildStatus::CapacityExceeded;

  const double axisScalar = -centerDelta * projection.pixelsPerMetre;
  const bool axisVisible =
      std::isfinite(axisScalar) && std::abs(axisScalar) <= extent + margin;
  const int phase = nearestLinePhase(centerDelta, displayedSpacingMetres,
                                     projection.majorInterval);
  for (std::int64_t relative = first; relative <= last; ++relative) {
    const double scalar = centerOffsetPixels + static_cast<double>(relative) *
                                                   displayedSpacingPixels;
    if (axisVisible && std::abs(scalar - axisScalar) <=
                           std::max(0.25, displayedSpacingPixels * 1.0e-10))
      continue;
    QPointF start;
    QPointF end;
    if (!clipInfiniteLine(viewportCenter + normal * scalar, direction,
                          projection.viewportLogicalPixels, start, end))
      continue;
    const bool major =
        positiveModulo(static_cast<std::int64_t>(phase) + relative,
                       projection.majorInterval) == 0;
    std::size_t &vertices = major ? output.majorVertices : output.minorVertices;
    std::size_t &lines = major ? output.majorLines : output.minorLines;
    const auto destination = major ? output.major : output.minor;
    const double width = major ? projection.majorLineWidthPixels
                               : projection.minorLineWidthPixels;
    const std::size_t before = vertices;
    if (!appendLine(destination, vertices, start, end, width))
      return EngineeringGridBuildStatus::CapacityExceeded;
    if (vertices != before) {
      ++lines;
      ++familyLines;
    }
  }
  return EngineeringGridBuildStatus::Built;
}

class GridLayerNode final : public QSGGeometryNode {
public:
  explicit GridLayerNode(std::size_t capacity)
      : geometry_(QSGGeometry::defaultAttributes_Point2D(),
                  static_cast<int>(capacity)),
        capacity_(capacity) {
    geometry_.setDrawingMode(QSGGeometry::DrawTriangles);
    geometry_.setVertexDataPattern(QSGGeometry::DynamicPattern);
    geometry_.setVertexCount(0);
    setGeometry(&geometry_);
    setMaterial(&material_);
  }

  std::span<QSGGeometry::Point2D> writableVertices() {
    geometry_.setVertexCount(static_cast<int>(capacity_));
    return {geometry_.vertexDataAsPoint2D(), capacity_};
  }

  void finishGeometry(std::size_t count) {
    geometry_.setVertexCount(static_cast<int>(count));
    geometry_.markVertexDataDirty();
    markDirty(QSGNode::DirtyGeometry);
  }

  void setColor(const QColor &color) {
    if (material_.color() == color)
      return;
    material_.setColor(color);
    markDirty(QSGNode::DirtyMaterial);
  }

private:
  QSGGeometry geometry_;
  QSGFlatColorMaterial material_;
  std::size_t capacity_;
};

class EngineeringGridRootNode final : public QSGNode {
public:
  EngineeringGridRootNode()
      : minor_(new GridLayerNode(layerVertexCapacity)),
        major_(new GridLayerNode(layerVertexCapacity)),
        axisX_(new GridLayerNode(axisVertexCapacity)),
        axisY_(new GridLayerNode(axisVertexCapacity)) {
    appendChildNode(minor_);
    appendChildNode(major_);
    appendChildNode(axisX_);
    appendChildNode(axisY_);
  }

  ~EngineeringGridRootNode() override {
    while (QSGNode *child = firstChild()) {
      removeChildNode(child);
      delete child;
    }
  }

  EngineeringGridMetrics rebuild(const EngineeringGridProjection &projection) {
    EngineeringGridLineBuffers buffers{
        minor_->writableVertices(), major_->writableVertices(),
        axisX_->writableVertices(), axisY_->writableVertices()};
    const EngineeringGridMetrics metrics =
        buildEngineeringGrid(projection, buffers);
    minor_->finishGeometry(metrics.minorVertices);
    major_->finishGeometry(metrics.majorVertices);
    axisX_->finishGeometry(metrics.axisXVertices);
    axisY_->finishGeometry(metrics.axisYVertices);
    return metrics;
  }

  void updatePalette(const EngineeringGridPalette &palette) {
    minor_->setColor(palette.minor);
    major_->setColor(palette.major);
    axisX_->setColor(palette.axisX);
    axisY_->setColor(palette.axisY);
  }

  std::uint64_t geometryGeneration = 0;
  std::uint64_t paletteGeneration = 0;

private:
  GridLayerNode *minor_;
  GridLayerNode *major_;
  GridLayerNode *axisX_;
  GridLayerNode *axisY_;
};

} // namespace

EngineeringGridSpacing
engineeringGridSpacing(const EngineeringGridProjection &projection) noexcept {
  EngineeringGridSpacing spacing;
  if (!validProjection(projection))
    return spacing;
  const double baseSpacingPixels =
      projection.minorSpacingMetres * projection.pixelsPerMetre;
  const double diagonal = std::hypot(projection.viewportLogicalPixels.width(),
                                     projection.viewportLogicalPixels.height());
  const double lineMargin = std::max({projection.minorLineWidthPixels,
                                      projection.majorLineWidthPixels,
                                      projection.axisLineWidthPixels});
  constexpr double densityDivisor = static_cast<double>(lineCapacity) - 4.0;
  const double requiredSpacingPixels =
      std::max(projection.minimumLineSpacingPixels,
               (diagonal + 2.0 * lineMargin) / densityDivisor);
  if (!std::isfinite(baseSpacingPixels) || baseSpacingPixels <= 0.0 ||
      !std::isfinite(requiredSpacingPixels))
    return spacing;
  const double multiplier =
      oneTwoFiveCeiling(requiredSpacingPixels / baseSpacingPixels);
  spacing.minorMetres = projection.minorSpacingMetres * multiplier;
  spacing.minorPixels = baseSpacingPixels * multiplier;
  spacing.densityLimited = multiplier > 1.0;
  if (!std::isfinite(spacing.minorMetres) || spacing.minorMetres <= 0.0 ||
      !std::isfinite(spacing.minorPixels) || spacing.minorPixels <= 0.0)
    return {};
  return spacing;
}

EngineeringGridMetrics
buildEngineeringGrid(const EngineeringGridProjection &projection,
                     EngineeringGridLineBuffers buffers) noexcept {
  EngineeringGridMetrics metrics;
  if (projection.viewportLogicalPixels.width() <= 0.0 ||
      projection.viewportLogicalPixels.height() <= 0.0) {
    metrics.status = EngineeringGridBuildStatus::EmptyViewport;
    return metrics;
  }
  if (!validProjection(projection))
    return metrics;
  if (buffers.minor.size() < layerVertexCapacity ||
      buffers.major.size() < layerVertexCapacity ||
      buffers.axisX.size() < axisVertexCapacity ||
      buffers.axisY.size() < axisVertexCapacity) {
    metrics.status = EngineeringGridBuildStatus::CapacityExceeded;
    return metrics;
  }

  const EngineeringGridSpacing spacing = engineeringGridSpacing(projection);
  metrics.displayedMinorSpacingMetres = spacing.minorMetres;
  metrics.displayedMinorSpacingPixels = spacing.minorPixels;
  metrics.densityLimited = spacing.densityLimited;
  if (!std::isfinite(metrics.displayedMinorSpacingMetres) ||
      metrics.displayedMinorSpacingMetres <= 0.0 ||
      !std::isfinite(metrics.displayedMinorSpacingPixels) ||
      metrics.displayedMinorSpacingPixels <= 0.0)
    return metrics;

  const double cosine = std::cos(projection.rotationRadians);
  const double sine = std::sin(projection.rotationRadians);
  if (!std::isfinite(cosine) || !std::isfinite(sine))
    return metrics;
  const QPointF worldX{cosine, -sine};
  const QPointF worldY{-sine, -cosine};
  const QPointF viewportCenter{projection.viewportLogicalPixels.width() * 0.5,
                               projection.viewportLogicalPixels.height() * 0.5};
  const double deltaX =
      projection.viewCenterMetres.x() - projection.gridOriginMetres.x();
  const double deltaY =
      projection.viewCenterMetres.y() - projection.gridOriginMetres.y();
  if (!std::isfinite(deltaX) || !std::isfinite(deltaY))
    return metrics;

  FamilyOutput output{buffers.minor,         buffers.major,
                      metrics.minorVertices, metrics.majorVertices,
                      metrics.minorLines,    metrics.majorLines};
  metrics.status = emitFamily(projection, viewportCenter, worldX, worldY,
                              deltaX, metrics.displayedMinorSpacingMetres,
                              metrics.displayedMinorSpacingPixels, output,
                              metrics.xFamilyLines);
  if (metrics.status != EngineeringGridBuildStatus::Built) {
    clearGeometryMetrics(metrics);
    return metrics;
  }
  metrics.status = emitFamily(projection, viewportCenter, worldY, worldX,
                              deltaY, metrics.displayedMinorSpacingMetres,
                              metrics.displayedMinorSpacingPixels, output,
                              metrics.yFamilyLines);
  if (metrics.status != EngineeringGridBuildStatus::Built) {
    clearGeometryMetrics(metrics);
    return metrics;
  }

  auto emitAxis = [&](QPointF normal, QPointF direction, double delta,
                      std::span<QSGGeometry::Point2D> destination,
                      std::size_t &vertices) {
    const double scalar = -delta * projection.pixelsPerMetre;
    if (!std::isfinite(scalar))
      return true;
    QPointF start;
    QPointF end;
    if (!clipInfiniteLine(viewportCenter + normal * scalar, direction,
                          projection.viewportLogicalPixels, start, end))
      return true;
    const std::size_t before = vertices;
    if (!appendLine(destination, vertices, start, end,
                    projection.axisLineWidthPixels))
      return false;
    if (vertices != before)
      ++metrics.axisLines;
    return true;
  };
  if (!emitAxis(worldY, worldX, deltaY, buffers.axisX, metrics.axisXVertices) ||
      !emitAxis(worldX, worldY, deltaX, buffers.axisY, metrics.axisYVertices)) {
    metrics.status = EngineeringGridBuildStatus::CapacityExceeded;
    clearGeometryMetrics(metrics);
  }
  return metrics;
}

EngineeringGridItem::EngineeringGridItem(QQuickItem *parent)
    : QQuickItem(parent) {
  setFlag(ItemHasContents, true);
  setClip(true);
}

QPointF EngineeringGridItem::viewCenterMetres() const {
  return projection_.viewCenterMetres;
}

QPointF EngineeringGridItem::gridOriginMetres() const {
  return projection_.gridOriginMetres;
}

qreal EngineeringGridItem::pixelsPerMetre() const {
  return projection_.pixelsPerMetre;
}

qreal EngineeringGridItem::rotationRadians() const {
  return projection_.rotationRadians;
}

qreal EngineeringGridItem::minorSpacingMetres() const {
  return projection_.minorSpacingMetres;
}

qreal EngineeringGridItem::displayedMinorSpacingMetres() const {
  return engineeringGridSpacing(projection()).minorMetres;
}

int EngineeringGridItem::majorInterval() const {
  return projection_.majorInterval;
}

qreal EngineeringGridItem::minimumLineSpacingPixels() const {
  return projection_.minimumLineSpacingPixels;
}

qreal EngineeringGridItem::minorLineWidthPixels() const {
  return projection_.minorLineWidthPixels;
}

qreal EngineeringGridItem::majorLineWidthPixels() const {
  return projection_.majorLineWidthPixels;
}

qreal EngineeringGridItem::axisLineWidthPixels() const {
  return projection_.axisLineWidthPixels;
}

QColor EngineeringGridItem::minorColor() const { return palette_.minor; }

QColor EngineeringGridItem::majorColor() const { return palette_.major; }

QColor EngineeringGridItem::axisXColor() const { return palette_.axisX; }

QColor EngineeringGridItem::axisYColor() const { return palette_.axisY; }

EngineeringGridMetrics EngineeringGridItem::gridMetrics() const {
  const std::scoped_lock lock{metricsMutex_};
  return metrics_;
}

std::uint64_t EngineeringGridItem::geometryBuildCount() const {
  const std::scoped_lock lock{metricsMutex_};
  return geometryBuildCount_;
}

void EngineeringGridItem::setViewCenterMetres(QPointF center) {
  if (projection_.viewCenterMetres == center)
    return;
  projection_.viewCenterMetres = center;
  emit viewCenterMetresChanged();
  requestGeometryUpdate();
}

void EngineeringGridItem::setGridOriginMetres(QPointF origin) {
  if (projection_.gridOriginMetres == origin)
    return;
  projection_.gridOriginMetres = origin;
  emit gridOriginMetresChanged();
  requestGeometryUpdate();
}

void EngineeringGridItem::setPixelsPerMetre(qreal scale) {
  if (projection_.pixelsPerMetre == scale)
    return;
  projection_.pixelsPerMetre = scale;
  emit pixelsPerMetreChanged();
  requestGeometryUpdate();
}

void EngineeringGridItem::setRotationRadians(qreal radians) {
  if (projection_.rotationRadians == radians)
    return;
  projection_.rotationRadians = radians;
  emit rotationRadiansChanged();
  requestGeometryUpdate();
}

void EngineeringGridItem::setMinorSpacingMetres(qreal spacing) {
  if (projection_.minorSpacingMetres == spacing)
    return;
  projection_.minorSpacingMetres = spacing;
  emit minorSpacingMetresChanged();
  requestGeometryUpdate();
}

void EngineeringGridItem::setMajorInterval(int interval) {
  if (projection_.majorInterval == interval)
    return;
  projection_.majorInterval = interval;
  emit majorIntervalChanged();
  requestGeometryUpdate();
}

void EngineeringGridItem::setMinimumLineSpacingPixels(qreal spacing) {
  if (projection_.minimumLineSpacingPixels == spacing)
    return;
  projection_.minimumLineSpacingPixels = spacing;
  emit minimumLineSpacingPixelsChanged();
  requestGeometryUpdate();
}

void EngineeringGridItem::setMinorLineWidthPixels(qreal width) {
  if (projection_.minorLineWidthPixels == width)
    return;
  projection_.minorLineWidthPixels = width;
  emit lineWidthsChanged();
  requestGeometryUpdate();
}

void EngineeringGridItem::setMajorLineWidthPixels(qreal width) {
  if (projection_.majorLineWidthPixels == width)
    return;
  projection_.majorLineWidthPixels = width;
  emit lineWidthsChanged();
  requestGeometryUpdate();
}

void EngineeringGridItem::setAxisLineWidthPixels(qreal width) {
  if (projection_.axisLineWidthPixels == width)
    return;
  projection_.axisLineWidthPixels = width;
  emit lineWidthsChanged();
  requestGeometryUpdate();
}

void EngineeringGridItem::setMinorColor(QColor color) {
  if (palette_.minor == color)
    return;
  palette_.minor = color;
  emit paletteChanged();
  requestPaletteUpdate();
}

void EngineeringGridItem::setMajorColor(QColor color) {
  if (palette_.major == color)
    return;
  palette_.major = color;
  emit paletteChanged();
  requestPaletteUpdate();
}

void EngineeringGridItem::setAxisXColor(QColor color) {
  if (palette_.axisX == color)
    return;
  palette_.axisX = color;
  emit paletteChanged();
  requestPaletteUpdate();
}

void EngineeringGridItem::setAxisYColor(QColor color) {
  if (palette_.axisY == color)
    return;
  palette_.axisY = color;
  emit paletteChanged();
  requestPaletteUpdate();
}

QSGNode *EngineeringGridItem::updatePaintNode(QSGNode *oldNode,
                                              UpdatePaintNodeData *) {
  auto *root = static_cast<EngineeringGridRootNode *>(oldNode);
  if (!root)
    root = new EngineeringGridRootNode;
  if (root->geometryGeneration != geometryGeneration_) {
    const EngineeringGridMetrics next = root->rebuild(projection());
    {
      const std::scoped_lock lock{metricsMutex_};
      metrics_ = next;
      ++geometryBuildCount_;
    }
    root->geometryGeneration = geometryGeneration_;
  }
  if (root->paletteGeneration != paletteGeneration_) {
    root->updatePalette(palette_);
    root->paletteGeneration = paletteGeneration_;
  }
  return root;
}

void EngineeringGridItem::geometryChange(const QRectF &newGeometry,
                                         const QRectF &oldGeometry) {
  QQuickItem::geometryChange(newGeometry, oldGeometry);
  if (newGeometry.size() != oldGeometry.size())
    requestGeometryUpdate();
}

void EngineeringGridItem::requestGeometryUpdate() {
  ++geometryGeneration_;
  emit displayedMinorSpacingChanged();
  update();
}

void EngineeringGridItem::requestPaletteUpdate() {
  ++paletteGeneration_;
  update();
}

EngineeringGridProjection EngineeringGridItem::projection() const {
  EngineeringGridProjection result = projection_;
  result.viewportLogicalPixels = size();
  return result;
}

} // namespace kearne::ui
