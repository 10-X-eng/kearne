#pragma once

#include <QObject>
#include <QPointF>
#include <QString>

#include <array>

namespace kearne::ui {

// Lightweight, non-canonical pointer feedback. Accepted geometry still crosses
// the typed command path only when the gesture is released.
class SketchGesturePreview final : public QObject {
  Q_OBJECT
  Q_PROPERTY(bool visible READ visible NOTIFY previewChanged)
  Q_PROPERTY(bool construction READ construction NOTIFY previewChanged)
  Q_PROPERTY(QPointF first READ first NOTIFY previewChanged)
  Q_PROPERTY(QPointF second READ second NOTIFY previewChanged)
  Q_PROPERTY(QPointF third READ third NOTIFY previewChanged)
  Q_PROPERTY(QPointF fourth READ fourth NOTIFY previewChanged)

public:
  explicit SketchGesturePreview(QObject *parent = nullptr);

  [[nodiscard]] bool visible() const { return visible_; }
  [[nodiscard]] bool construction() const { return construction_; }
  [[nodiscard]] QPointF first() const { return corners_[0]; }
  [[nodiscard]] QPointF second() const { return corners_[1]; }
  [[nodiscard]] QPointF third() const { return corners_[2]; }
  [[nodiscard]] QPointF fourth() const { return corners_[3]; }

  [[nodiscard]] bool updateDrag(const QString &commandId,
                                qreal firstXMillimeters,
                                qreal firstYMillimeters,
                                qreal oppositeXMillimeters,
                                qreal oppositeYMillimeters, bool construction);
  void clear();

signals:
  void previewChanged();

private:
  std::array<QPointF, 4> corners_{};
  bool visible_ = false;
  bool construction_ = false;
};

} // namespace kearne::ui
