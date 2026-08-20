#pragma once

#include "navigation_input.hpp"

#include <QObject>
#include <QString>
#include <QtQml/qqmlregistration.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>

class QSocketNotifier;

namespace kearne::ui {

class NavigationDevice : public QObject {
  Q_OBJECT
  QML_NAMED_ELEMENT(NavigationDevice)
  QML_UNCREATABLE("Available through App.navigationDevice")
  Q_PROPERTY(bool connected READ connected NOTIFY stateChanged)
  Q_PROPERTY(QString status READ status NOTIFY stateChanged)

public:
  // Applied events are bounded independently of machine speed. A drain may
  // retain one additional, non-applied lookahead event to detect backlog.
  static constexpr std::size_t maximumEventsPerDrain = 256U;

  explicit NavigationDevice(NavigationTargetRouter &router,
                            QObject *parent = nullptr);
  ~NavigationDevice() override;

  [[nodiscard]] bool connected() const;
  [[nodiscard]] QString status() const;

signals:
  void stateChanged();

private:
  enum class DeviceEventKind : std::uint8_t {
    Motion,
    DeviceAdded,
    DeviceRemoved,
    Fit,
    Ignored,
  };

  struct DeviceEvent {
    DeviceEventKind kind = DeviceEventKind::Ignored;
    qreal tx = 0.0;
    qreal ty = 0.0;
    qreal tz = 0.0;
    qreal rx = 0.0;
    qreal ry = 0.0;
    qreal rz = 0.0;
  };

  [[nodiscard]] std::optional<DeviceEvent> nextEvent();
  void queueDrainContinuation();
  void drainEvents();

  NavigationTargetRouter &router_;
  bool serviceOpen_ = false;
  bool connected_ = false;
  bool continuationQueued_ = false;
  QString status_ = QStringLiteral("No 3D controller service detected");
  std::optional<DeviceEvent> pendingEvent_;
  std::unique_ptr<QSocketNotifier> notifier_;
};

} // namespace kearne::ui
