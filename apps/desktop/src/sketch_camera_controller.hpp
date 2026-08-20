#pragma once

#include "sketch_scene_projection.hpp"

#include <QObject>
#include <QPointF>
#include <QSizeF>
#include <QtQml/qqmlregistration.h>

namespace kearne::ui {

class SketchCameraController : public QObject {
  Q_OBJECT
  QML_NAMED_ELEMENT(SketchCameraController)
  QML_UNCREATABLE("Available through App.sketchCamera")
  Q_PROPERTY(qulonglong generation READ generation NOTIFY cameraChanged)
  Q_PROPERTY(QPointF centerMetres READ centerMetres NOTIFY cameraChanged)
  Q_PROPERTY(qreal metresPerLogicalPixel READ metresPerLogicalPixel NOTIFY
                 cameraChanged)
  Q_PROPERTY(qreal pixelsPerMetre READ pixelsPerMetre NOTIFY cameraChanged)
  Q_PROPERTY(qreal rotationRadians READ rotationRadians NOTIFY cameraChanged)

public:
  explicit SketchCameraController(QObject *parent = nullptr);

  [[nodiscard]] qulonglong generation() const;
  [[nodiscard]] QPointF centerMetres() const;
  [[nodiscard]] qreal metresPerLogicalPixel() const;
  [[nodiscard]] qreal pixelsPerMetre() const;
  [[nodiscard]] qreal rotationRadians() const;
  [[nodiscard]] SketchCamera2d camera() const;

  Q_INVOKABLE bool pan(qreal deltaXLogicalPixels, qreal deltaYLogicalPixels);
  Q_INVOKABLE bool zoomAt(qreal wheelSteps, qreal anchorXLogicalPixels,
                          qreal anchorYLogicalPixels,
                          qreal viewportWidthLogicalPixels,
                          qreal viewportHeightLogicalPixels);
  Q_INVOKABLE bool rotate(qreal deltaRadians);
  Q_INVOKABLE void reset();

  [[nodiscard]] bool fit(render::Bounds2d bounds, QSizeF viewportLogicalPixels,
                         qreal marginLogicalPixels = 48.0);

signals:
  void cameraChanged();

private:
  bool replace(render::Point2d centerMetres, double metresPerLogicalPixel,
               double rotationRadians);

  SketchCamera2d camera_;
};

} // namespace kearne::ui
