#pragma once

#include "navigation_input.hpp"

#include <QObject>
#include <QString>
#include <QtQml/qqmlregistration.h>

namespace kearne::ui {

class ViewportCamera : public QObject, public NavigationTarget {
  Q_OBJECT
  QML_NAMED_ELEMENT(ViewportCamera)
  QML_UNCREATABLE("Available through App.camera")
  Q_PROPERTY(QString navigationProfile READ navigationProfile WRITE
                 setNavigationProfile NOTIFY navigationProfileChanged)
  Q_PROPERTY(bool zoomReversed READ zoomReversed WRITE setZoomReversed NOTIFY
                 zoomReversedChanged)
  Q_PROPERTY(qreal yaw READ yaw NOTIFY cameraChanged)
  Q_PROPERTY(qreal pitch READ pitch NOTIFY cameraChanged)
  Q_PROPERTY(qreal roll READ roll NOTIFY cameraChanged)
  Q_PROPERTY(qreal panX READ panX NOTIFY cameraChanged)
  Q_PROPERTY(qreal panY READ panY NOTIFY cameraChanged)
  Q_PROPERTY(qreal distance READ distance NOTIFY cameraChanged)
  Q_PROPERTY(QString viewName READ viewName NOTIFY cameraChanged)
  Q_PROPERTY(QString state READ state NOTIFY cameraChanged)

public:
  explicit ViewportCamera(
      QString navigationProfile = QStringLiteral("solidworks"),
      bool zoomReversed = false, QObject *parent = nullptr);

  [[nodiscard]] QString navigationProfile() const;
  [[nodiscard]] bool zoomReversed() const;
  [[nodiscard]] qreal yaw() const;
  [[nodiscard]] qreal pitch() const;
  [[nodiscard]] qreal roll() const;
  [[nodiscard]] qreal panX() const;
  [[nodiscard]] qreal panY() const;
  [[nodiscard]] qreal distance() const;
  [[nodiscard]] QString viewName() const;
  [[nodiscard]] QString state() const;

  void setNavigationProfile(const QString &profile);
  void setZoomReversed(bool reversed);

  Q_INVOKABLE bool applyPointerDrag(int buttons, int modifiers, qreal dx,
                                    qreal dy);
  Q_INVOKABLE void applyWheel(qreal angleDeltaY);
  Q_INVOKABLE void applySpaceMotion(qreal tx, qreal ty, qreal tz, qreal rx,
                                    qreal ry, qreal rz, qreal periodMs = 16.0);
  Q_INVOKABLE void orbit(qreal dx, qreal dy);
  Q_INVOKABLE void turn(qreal yawDegrees, qreal pitchDegrees);
  Q_INVOKABLE void pan(qreal dx, qreal dy);
  Q_INVOKABLE void zoom(qreal amount);
  Q_INVOKABLE void fit();
  Q_INVOKABLE bool setView(const QString &view);

  [[nodiscard]] bool applyNavigation(const NavigationAction &action) override;

signals:
  void navigationProfileChanged();
  void zoomReversedChanged();
  void cameraChanged();

private:
  void changeCamera(qreal yaw, qreal pitch, qreal roll, qreal panX, qreal panY,
                    qreal distance, const QString &viewName);

  NavigationProfile navigationProfile_ = NavigationProfile::SolidWorks;
  bool zoomReversed_ = false;
  qreal yaw_ = 35.264389682754654;
  qreal pitch_ = -45.0;
  qreal roll_ = 0.0;
  qreal panX_ = 0.0;
  qreal panY_ = 0.0;
  qreal distance_ = 220.0;
  QString viewName_ = QStringLiteral("isometric");
};

} // namespace kearne::ui
