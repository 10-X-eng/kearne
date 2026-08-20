#pragma once

#include <QColor>
#include <QPointF>
#include <QQuickItem>
#include <QSGGeometry>
#include <QSizeF>
#include <QtQml/qqmlregistration.h>

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <span>

namespace kearne::ui {

struct EngineeringGridPolicy {
  static constexpr std::size_t verticesPerLine = 6U;
  static constexpr std::size_t maximumLinesPerFamily = 256U;
  static constexpr std::size_t maximumLayerVertices =
      maximumLinesPerFamily * 2U * verticesPerLine;
  static constexpr std::size_t maximumAxisVertices = verticesPerLine;
};

struct EngineeringGridProjection {
  QSizeF viewportLogicalPixels;
  QPointF viewCenterMetres;
  QPointF gridOriginMetres;
  double pixelsPerMetre = 1'000.0;
  double rotationRadians = 0.0;
  double minorSpacingMetres = 0.01;
  int majorInterval = 5;
  double minimumLineSpacingPixels = 8.0;
  double minorLineWidthPixels = 1.0;
  double majorLineWidthPixels = 1.25;
  double axisLineWidthPixels = 1.5;
  bool operator==(const EngineeringGridProjection &) const = default;
};

struct EngineeringGridPalette {
  QColor minor{72, 82, 96, 96};
  QColor major{96, 108, 124, 144};
  QColor axisX{210, 72, 72, 224};
  QColor axisY{72, 176, 104, 224};
  bool operator==(const EngineeringGridPalette &) const = default;
};

enum class EngineeringGridBuildStatus : std::uint8_t {
  Built,
  EmptyViewport,
  InvalidProjection,
  CapacityExceeded,
};

struct EngineeringGridMetrics {
  EngineeringGridBuildStatus status =
      EngineeringGridBuildStatus::InvalidProjection;
  double displayedMinorSpacingMetres = 0.0;
  double displayedMinorSpacingPixels = 0.0;
  std::size_t xFamilyLines = 0;
  std::size_t yFamilyLines = 0;
  std::size_t minorLines = 0;
  std::size_t majorLines = 0;
  std::size_t axisLines = 0;
  std::size_t minorVertices = 0;
  std::size_t majorVertices = 0;
  std::size_t axisXVertices = 0;
  std::size_t axisYVertices = 0;
  bool densityLimited = false;
  bool operator==(const EngineeringGridMetrics &) const = default;
};

struct EngineeringGridSpacing {
  double minorMetres = 0.0;
  double minorPixels = 0.0;
  bool densityLimited = false;
};

[[nodiscard]] EngineeringGridSpacing
engineeringGridSpacing(const EngineeringGridProjection &projection) noexcept;

struct EngineeringGridLineBuffers {
  std::span<QSGGeometry::Point2D> minor;
  std::span<QSGGeometry::Point2D> major;
  std::span<QSGGeometry::Point2D> axisX;
  std::span<QSGGeometry::Point2D> axisY;
};

[[nodiscard]] EngineeringGridMetrics
buildEngineeringGrid(const EngineeringGridProjection &projection,
                     EngineeringGridLineBuffers buffers) noexcept;

class EngineeringGridItem : public QQuickItem {
  Q_OBJECT
  QML_NAMED_ELEMENT(EngineeringGridItem)
  Q_PROPERTY(QPointF viewCenterMetres READ viewCenterMetres WRITE
                 setViewCenterMetres NOTIFY viewCenterMetresChanged)
  Q_PROPERTY(QPointF gridOriginMetres READ gridOriginMetres WRITE
                 setGridOriginMetres NOTIFY gridOriginMetresChanged)
  Q_PROPERTY(qreal pixelsPerMetre READ pixelsPerMetre WRITE setPixelsPerMetre
                 NOTIFY pixelsPerMetreChanged)
  Q_PROPERTY(qreal rotationRadians READ rotationRadians WRITE setRotationRadians
                 NOTIFY rotationRadiansChanged)
  Q_PROPERTY(qreal minorSpacingMetres READ minorSpacingMetres WRITE
                 setMinorSpacingMetres NOTIFY minorSpacingMetresChanged)
  Q_PROPERTY(qreal displayedMinorSpacingMetres READ displayedMinorSpacingMetres
                 NOTIFY displayedMinorSpacingChanged)
  Q_PROPERTY(int majorInterval READ majorInterval WRITE setMajorInterval NOTIFY
                 majorIntervalChanged)
  Q_PROPERTY(
      qreal minimumLineSpacingPixels READ minimumLineSpacingPixels WRITE
          setMinimumLineSpacingPixels NOTIFY minimumLineSpacingPixelsChanged)
  Q_PROPERTY(qreal minorLineWidthPixels READ minorLineWidthPixels WRITE
                 setMinorLineWidthPixels NOTIFY lineWidthsChanged)
  Q_PROPERTY(qreal majorLineWidthPixels READ majorLineWidthPixels WRITE
                 setMajorLineWidthPixels NOTIFY lineWidthsChanged)
  Q_PROPERTY(qreal axisLineWidthPixels READ axisLineWidthPixels WRITE
                 setAxisLineWidthPixels NOTIFY lineWidthsChanged)
  Q_PROPERTY(QColor minorColor READ minorColor WRITE setMinorColor NOTIFY
                 paletteChanged)
  Q_PROPERTY(QColor majorColor READ majorColor WRITE setMajorColor NOTIFY
                 paletteChanged)
  Q_PROPERTY(QColor axisXColor READ axisXColor WRITE setAxisXColor NOTIFY
                 paletteChanged)
  Q_PROPERTY(QColor axisYColor READ axisYColor WRITE setAxisYColor NOTIFY
                 paletteChanged)

public:
  explicit EngineeringGridItem(QQuickItem *parent = nullptr);

  [[nodiscard]] QPointF viewCenterMetres() const;
  [[nodiscard]] QPointF gridOriginMetres() const;
  [[nodiscard]] qreal pixelsPerMetre() const;
  [[nodiscard]] qreal rotationRadians() const;
  [[nodiscard]] qreal minorSpacingMetres() const;
  [[nodiscard]] qreal displayedMinorSpacingMetres() const;
  [[nodiscard]] int majorInterval() const;
  [[nodiscard]] qreal minimumLineSpacingPixels() const;
  [[nodiscard]] qreal minorLineWidthPixels() const;
  [[nodiscard]] qreal majorLineWidthPixels() const;
  [[nodiscard]] qreal axisLineWidthPixels() const;
  [[nodiscard]] QColor minorColor() const;
  [[nodiscard]] QColor majorColor() const;
  [[nodiscard]] QColor axisXColor() const;
  [[nodiscard]] QColor axisYColor() const;
  [[nodiscard]] EngineeringGridMetrics gridMetrics() const;
  [[nodiscard]] std::uint64_t geometryBuildCount() const;

  void setViewCenterMetres(QPointF center);
  void setGridOriginMetres(QPointF origin);
  void setPixelsPerMetre(qreal scale);
  void setRotationRadians(qreal radians);
  void setMinorSpacingMetres(qreal spacing);
  void setMajorInterval(int interval);
  void setMinimumLineSpacingPixels(qreal spacing);
  void setMinorLineWidthPixels(qreal width);
  void setMajorLineWidthPixels(qreal width);
  void setAxisLineWidthPixels(qreal width);
  void setMinorColor(QColor color);
  void setMajorColor(QColor color);
  void setAxisXColor(QColor color);
  void setAxisYColor(QColor color);

signals:
  void viewCenterMetresChanged();
  void gridOriginMetresChanged();
  void pixelsPerMetreChanged();
  void rotationRadiansChanged();
  void minorSpacingMetresChanged();
  void displayedMinorSpacingChanged();
  void majorIntervalChanged();
  void minimumLineSpacingPixelsChanged();
  void lineWidthsChanged();
  void paletteChanged();

protected:
  QSGNode *updatePaintNode(QSGNode *oldNode,
                           UpdatePaintNodeData *data) override;
  void geometryChange(const QRectF &newGeometry,
                      const QRectF &oldGeometry) override;

private:
  void requestGeometryUpdate();
  void requestPaletteUpdate();
  [[nodiscard]] EngineeringGridProjection projection() const;

  EngineeringGridProjection projection_;
  EngineeringGridPalette palette_;
  std::uint64_t geometryGeneration_ = 1;
  std::uint64_t paletteGeneration_ = 1;
  mutable std::mutex metricsMutex_;
  EngineeringGridMetrics metrics_;
  std::uint64_t geometryBuildCount_ = 0;
};

} // namespace kearne::ui
