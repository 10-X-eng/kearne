#include "navigation_device.hpp"

#include <QSocketNotifier>

#ifdef KEARNE_HAS_SPNAV
#include <spnav.h>
#endif

#include <algorithm>
#include <utility>

namespace kearne::ui {

NavigationDevice::NavigationDevice(NavigationTargetRouter &router,
                                   QObject *parent)
    : QObject(parent), router_(router) {
#ifdef KEARNE_HAS_SPNAV
  if (spnav_open() == 0 && spnav_fd() >= 0) {
    serviceOpen_ = true;
    status_ = QStringLiteral("3D controller service ready");
    notifier_ = std::make_unique<QSocketNotifier>(spnav_fd(),
                                                  QSocketNotifier::Read, this);
    connect(notifier_.get(), &QSocketNotifier::activated, this, [this] {
      if (!continuationQueued_)
        drainEvents();
    });
  }
#else
  status_ = QStringLiteral("3D controller adapter unavailable in this build");
#endif
}

NavigationDevice::~NavigationDevice() {
#ifdef KEARNE_HAS_SPNAV
  if (serviceOpen_)
    static_cast<void>(spnav_close());
#endif
}

bool NavigationDevice::connected() const { return connected_; }
QString NavigationDevice::status() const { return status_; }

std::optional<NavigationDevice::DeviceEvent> NavigationDevice::nextEvent() {
#ifdef KEARNE_HAS_SPNAV
  if (pendingEvent_)
    return std::exchange(pendingEvent_, std::nullopt);

  spnav_event event{};
  if (spnav_poll_event(&event) == 0)
    return std::nullopt;

  if (event.type == SPNAV_EVENT_MOTION) {
    constexpr qreal range = 350.0;
    const qreal timeScale =
        std::clamp(static_cast<qreal>(event.motion.period) / 16.0, 0.1, 4.0);
    const auto component = [timeScale](int value) {
      return std::clamp(static_cast<qreal>(value) / range, -1.0, 1.0) *
             timeScale;
    };
    return DeviceEvent{DeviceEventKind::Motion,    component(event.motion.x),
                       component(event.motion.y),  component(event.motion.z),
                       component(event.motion.rx), component(event.motion.ry),
                       component(event.motion.rz)};
  }
  if (event.type == SPNAV_EVENT_DEV) {
    return DeviceEvent{event.dev.op == SPNAV_DEV_ADD
                           ? DeviceEventKind::DeviceAdded
                           : DeviceEventKind::DeviceRemoved};
  }
  if (event.type == SPNAV_EVENT_BUTTON && event.button.press != 0 &&
      event.button.bnum == 0)
    return DeviceEvent{DeviceEventKind::Fit};
  return DeviceEvent{};
#else
  return std::nullopt;
#endif
}

void NavigationDevice::queueDrainContinuation() {
  if (continuationQueued_)
    return;
  continuationQueued_ = true;
  QMetaObject::invokeMethod(
      this,
      [this] {
        continuationQueued_ = false;
        drainEvents();
      },
      Qt::QueuedConnection);
}

void NavigationDevice::drainEvents() {
#ifdef KEARNE_HAS_SPNAV
  qreal tx = 0.0;
  qreal ty = 0.0;
  qreal tz = 0.0;
  qreal rx = 0.0;
  qreal ry = 0.0;
  qreal rz = 0.0;
  bool hasMotion = false;
  const auto publishMotion = [&] {
    if (!hasMotion)
      return;
    static_cast<void>(
        router_.applyNavigation(SpaceMotionNavigation{tx, ty, tz, rx, ry, rz}));
    tx = ty = tz = rx = ry = rz = 0.0;
    hasMotion = false;
  };

  std::size_t consumed = 0;
  bool endedAtControlBoundary = false;
  while (consumed < maximumEventsPerDrain) {
    const std::optional<DeviceEvent> event = nextEvent();
    if (!event)
      break;
    ++consumed;
    if (event->kind == DeviceEventKind::Motion) {
      if (!connected_) {
        connected_ = true;
        status_ = QStringLiteral("3D controller active");
        emit stateChanged();
      }
      tx += event->tx;
      ty += event->ty;
      tz += event->tz;
      rx += event->rx;
      ry += event->ry;
      rz += event->rz;
      hasMotion = true;
    } else if (event->kind == DeviceEventKind::DeviceAdded ||
               event->kind == DeviceEventKind::DeviceRemoved) {
      publishMotion();
      connected_ = event->kind == DeviceEventKind::DeviceAdded;
      status_ = connected_ ? QStringLiteral("3D controller active")
                           : QStringLiteral("3D controller service ready");
      emit stateChanged();
      endedAtControlBoundary = true;
      break;
    } else if (event->kind == DeviceEventKind::Fit) {
      publishMotion();
      static_cast<void>(router_.applyNavigation(FitNavigation{}));
      endedAtControlBoundary = true;
      break;
    }
  }
  publishMotion();

  if (consumed == maximumEventsPerDrain || endedAtControlBoundary) {
    if (std::optional<DeviceEvent> lookahead = nextEvent()) {
      pendingEvent_ = *lookahead;
      queueDrainContinuation();
    }
  }
#endif
}

} // namespace kearne::ui
