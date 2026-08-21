#pragma once

#include "frontend_contract.hpp"

#include <QObject>
#include <QPointF>
#include <QString>

#include <cstdint>
#include <span>
#include <vector>

namespace kearne::ui {

enum class SketchPreviewQuantity : std::uint8_t { Length = 1, Angle = 2 };

struct SketchPreviewMeasurement {
  bool operator==(const SketchPreviewMeasurement &) const = default;

  QString prefix;
  SketchPreviewQuantity quantity = SketchPreviewQuantity::Length;
  double valueSi = 0.0;
  QPointF anchorMillimeters;
  QPointF originMillimeters;
};

// Responsive, non-canonical feedback projected through the same gesture model
// as accepted geometry. Only accepted inputs cross the typed command path.
class SketchGesturePreview final : public QObject {
  Q_OBJECT
  Q_PROPERTY(bool visible READ visible NOTIFY previewChanged)
  Q_PROPERTY(bool construction READ construction NOTIFY previewChanged)

public:
  explicit SketchGesturePreview(QObject *parent = nullptr);

  [[nodiscard]] bool visible() const { return visible_; }
  [[nodiscard]] bool construction() const { return construction_; }
  [[nodiscard]] std::span<const SketchPrimitiveProjection> primitives() const {
    return primitives_;
  }
  [[nodiscard]] std::span<const SketchPreviewMeasurement>
  measurements() const {
    return measurements_;
  }
  [[nodiscard]] std::span<const QPointF> inputPoints() const {
    return inputPoints_;
  }

  [[nodiscard]] bool updateGesture(const QString &commandId,
                                   std::span<const QPointF> pointsMillimeters,
                                   bool construction,
                                   const QString &methodId = {},
                                   bool closed = false,
                                   std::size_t sideCount = 0U,
                                   std::uint32_t degree = 3U);
  void clear();

signals:
  void previewChanged();

private:
  std::vector<QPointF> inputPoints_;
  std::vector<SketchPrimitiveProjection> primitives_;
  std::vector<SketchPreviewMeasurement> measurements_;
  bool visible_ = false;
  bool construction_ = false;
};

} // namespace kearne::ui
