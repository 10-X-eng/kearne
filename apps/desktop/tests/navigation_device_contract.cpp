#include "navigation_device.hpp"
#include "viewport_camera.hpp"

#include <kearne/testkit/property.hpp>

#include <QCoreApplication>
#include <QEventLoop>
#include <QObject>

#include <spnav.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <utility>
#include <vector>

#include <unistd.h>

namespace {

void require(bool condition, const char *message) {
  if (!condition)
    throw std::runtime_error(message);
}

namespace fake_spnav {

std::array<int, 2> descriptors{-1, -1};
std::vector<spnav_event> events;
std::size_t cursor = 0;

bool empty() { return cursor == events.size(); }

int open() {
  if (descriptors[0] >= 0 || ::pipe(descriptors.data()) != 0)
    return -1;
  return 0;
}

int close() {
  for (int &descriptor : descriptors) {
    if (descriptor >= 0)
      static_cast<void>(::close(descriptor));
    descriptor = -1;
  }
  events.clear();
  cursor = 0;
  return 0;
}

void enqueue(std::vector<spnav_event> next) {
  require(descriptors[1] >= 0, "fake spnav transport is closed");
  require(empty(), "fake spnav queue was not drained");
  events = std::move(next);
  cursor = 0;
  if (events.empty())
    return;
  constexpr char ready = 1;
  require(::write(descriptors[1], &ready, sizeof(ready)) == sizeof(ready),
          "fake spnav readiness write failed");
}

int poll(spnav_event *event) {
  if (!event || empty())
    return 0;
  *event = events[cursor++];
  if (empty()) {
    char ready = 0;
    static_cast<void>(::read(descriptors[0], &ready, sizeof(ready)));
  }
  return event->type;
}

} // namespace fake_spnav

spnav_event motionEvent(int x, int y, int z, int rx, int ry, int rz,
                        unsigned int period) {
  spnav_event event{};
  event.motion.type = SPNAV_EVENT_MOTION;
  event.motion.x = x;
  event.motion.y = y;
  event.motion.z = z;
  event.motion.rx = rx;
  event.motion.ry = ry;
  event.motion.rz = rz;
  event.motion.period = period;
  return event;
}

spnav_event deviceEvent(bool added) {
  spnav_event event{};
  event.dev.type = SPNAV_EVENT_DEV;
  event.dev.op = added ? SPNAV_DEV_ADD : SPNAV_DEV_RM;
  return event;
}

spnav_event fitEvent() {
  spnav_event event{};
  event.button.type = SPNAV_EVENT_BUTTON;
  event.button.press = 1;
  event.button.bnum = 0;
  return event;
}

spnav_event ignoredEvent() {
  spnav_event event{};
  event.cfg.type = SPNAV_EVENT_CFG;
  return event;
}

class DrainReference final {
public:
  DrainReference() {
    QObject::connect(&camera, &kearne::ui::ViewportCamera::cameraChanged,
                     [&] { ++cameraChanges; });
  }

  void consume(const std::vector<spnav_event> &events) {
    std::size_t cursor = 0;
    while (cursor < events.size()) {
      std::array<qreal, 6> motion{};
      bool hasMotion = false;
      const auto publishMotion = [&] {
        if (!hasMotion)
          return;
        const kearne::ui::NavigationAction action =
            kearne::ui::SpaceMotionNavigation{motion[0], motion[1], motion[2],
                                              motion[3], motion[4], motion[5]};
        actions.push_back(action);
        static_cast<void>(camera.applyNavigation(action));
        hasMotion = false;
      };

      std::size_t consumed = 0;
      while (cursor < events.size() &&
             consumed < kearne::ui::NavigationDevice::maximumEventsPerDrain) {
        const spnav_event &event = events[cursor++];
        ++consumed;
        if (event.type == SPNAV_EVENT_MOTION) {
          if (!connected) {
            connected = true;
            ++stateChanges;
          }
          constexpr qreal range = 350.0;
          const qreal scale = std::clamp(
              static_cast<qreal>(event.motion.period) / 16.0, 0.1, 4.0);
          const auto component = [scale](int value) {
            return std::clamp(static_cast<qreal>(value) / range, -1.0, 1.0) *
                   scale;
          };
          motion[0] += component(event.motion.x);
          motion[1] += component(event.motion.y);
          motion[2] += component(event.motion.z);
          motion[3] += component(event.motion.rx);
          motion[4] += component(event.motion.ry);
          motion[5] += component(event.motion.rz);
          hasMotion = true;
          continue;
        }
        if (event.type == SPNAV_EVENT_DEV) {
          publishMotion();
          connected = event.dev.op == SPNAV_DEV_ADD;
          ++stateChanges;
          break;
        }
        if (event.type == SPNAV_EVENT_BUTTON && event.button.press != 0 &&
            event.button.bnum == 0) {
          publishMotion();
          const kearne::ui::NavigationAction action =
              kearne::ui::FitNavigation{};
          actions.push_back(action);
          static_cast<void>(camera.applyNavigation(action));
          break;
        }
      }
      publishMotion();
    }
  }

  kearne::ui::ViewportCamera camera;
  std::vector<kearne::ui::NavigationAction> actions;
  std::size_t cameraChanges = 0;
  std::size_t stateChanges = 0;
  bool connected = false;
};

class RecordingCameraTarget final : public QObject,
                                    public kearne::ui::NavigationTarget {
public:
  [[nodiscard]] bool
  applyNavigation(const kearne::ui::NavigationAction &action) override {
    actions.push_back(action);
    return camera.applyNavigation(action);
  }

  kearne::ui::ViewportCamera camera;
  std::vector<kearne::ui::NavigationAction> actions;
};

bool sameReal(qreal left, qreal right) {
  constexpr qreal tolerance = 1.0e-10;
  return std::abs(left - right) <=
         tolerance * std::max({qreal{1.0}, std::abs(left), std::abs(right)});
}

void requireSameCamera(const kearne::ui::ViewportCamera &actual,
                       const kearne::ui::ViewportCamera &expected) {
  require(sameReal(actual.yaw(), expected.yaw()), "camera yaw diverged");
  require(sameReal(actual.pitch(), expected.pitch()), "camera pitch diverged");
  require(sameReal(actual.roll(), expected.roll()), "camera roll diverged");
  require(sameReal(actual.panX(), expected.panX()), "camera pan X diverged");
  require(sameReal(actual.panY(), expected.panY()), "camera pan Y diverged");
  require(sameReal(actual.distance(), expected.distance()),
          "camera distance diverged");
  require(actual.viewName() == expected.viewName(), "camera view diverged");
}

void runScenario(std::vector<spnav_event> events,
                 bool requireContinuationYield = false) {
  require(!events.empty(), "navigation scenario is empty");

  DrainReference expected;
  expected.consume(events);

  RecordingCameraTarget actual;
  kearne::ui::NavigationTargetRouter router(actual);
  std::size_t actualCameraChanges = 0;
  kearne::ui::NavigationDevice device(router);
  bool yieldProbeQueued = false;
  std::size_t cameraChangesAtYield = 0;
  QObject::connect(
      &actual.camera, &kearne::ui::ViewportCamera::cameraChanged, &device, [&] {
        ++actualCameraChanges;
        if (!requireContinuationYield || yieldProbeQueued)
          return;
        yieldProbeQueued = true;
        QMetaObject::invokeMethod(
            &device, [&] { cameraChangesAtYield = actualCameraChanges; },
            Qt::QueuedConnection);
      });
  std::size_t actualStateChanges = 0;
  QObject::connect(&device, &kearne::ui::NavigationDevice::stateChanged,
                   [&] { ++actualStateChanges; });

  fake_spnav::enqueue(std::move(events));
  const std::size_t maximumPumps =
      expected.cameraChanges + expected.stateChanges + 64U;
  for (std::size_t pump = 0; pump < maximumPumps; ++pump) {
    QCoreApplication::processEvents(QEventLoop::AllEvents);
    if (fake_spnav::empty() && actualCameraChanges == expected.cameraChanges &&
        actualStateChanges == expected.stateChanges)
      break;
  }

  require(fake_spnav::empty(), "navigation backlog did not drain");
  require(actualCameraChanges == expected.cameraChanges,
          "motion was not published once per modeled drain");
  require(actualStateChanges == expected.stateChanges,
          "device state transitions diverged from the drain model");
  require(device.connected() == expected.connected,
          "device connection state diverged from the drain model");
  require(actual.actions == expected.actions,
          "normalized SpaceMouse actions diverged from the drain model");
  if (requireContinuationYield) {
    require(yieldProbeQueued, "continuation yield probe was not queued");
    require(cameraChangesAtYield == 1U,
            "navigation backlog did not yield between drains");
  }
  requireSameCamera(actual.camera, expected.camera);
}

void verifyBudgetBoundaries() {
  constexpr std::size_t budget =
      kearne::ui::NavigationDevice::maximumEventsPerDrain;
  for (const std::size_t count :
       {std::size_t{1}, budget - 1U, budget, budget + 1U, budget * 2U - 1U,
        budget * 2U, budget * 2U + 1U, budget * 17U + 13U}) {
    std::vector<spnav_event> events(count, motionEvent(1, 0, 0, 0, 0, 0, 16U));
    runScenario(std::move(events), count == budget * 2U + 1U);
  }
}

std::vector<spnav_event>
generatedScenario(const kearne::testkit::PropertyProfile &profile) {
  const std::uint64_t population =
      profile.replay
          ? std::max<std::uint64_t>(profile.replay->index + 1U, 1U)
          : (profile.iterations + profile.shardCount - 1U) / profile.shardCount;
  const std::uint64_t seed =
      profile.replay
          ? profile.replay->seed
          : profile.seed ^ (profile.shardIndex * 0x9e3779b97f4a7c15ULL);
  kearne::testkit::Random random(seed);
  std::vector<spnav_event> events;
  events.reserve(static_cast<std::size_t>(population + 6U));
  events.push_back(motionEvent(1, 0, 0, 0, 0, 0, 16U));
  events.push_back(deviceEvent(false));
  events.push_back(motionEvent(0, 1, 0, 0, 0, 0, 16U));
  events.push_back(fitEvent());
  events.push_back(ignoredEvent());
  for (std::uint64_t index = 0; index < population; ++index) {
    const std::uint64_t kind = random.next() % 4096U;
    if (kind == 0U) {
      events.push_back(deviceEvent(true));
    } else if (kind == 1U) {
      events.push_back(deviceEvent(false));
    } else if (kind == 2U) {
      events.push_back(fitEvent());
    } else if (kind == 3U) {
      events.push_back(ignoredEvent());
    } else {
      const auto axis = [&] {
        return static_cast<int>(random.next() % 1401U) - 700;
      };
      const auto period = static_cast<unsigned int>(random.next() % 96U + 1U);
      const std::array axes{axis(), axis(), axis(), axis(), axis(), axis()};
      events.push_back(motionEvent(axes[0], axes[1], axes[2], axes[3], axes[4],
                                   axes[5], period));
    }
  }
  events.push_back(motionEvent(1, 0, 0, 0, 0, 0, 16U));
  return events;
}

void verifyGeneratedDrainModel() {
  runScenario(generatedScenario(kearne::testkit::propertyProfile()));
}

} // namespace

extern "C" int spnav_open() { return fake_spnav::open(); }
extern "C" int spnav_close() { return fake_spnav::close(); }
extern "C" int spnav_fd() { return fake_spnav::descriptors[0]; }
extern "C" int spnav_poll_event(spnav_event *event) {
  return fake_spnav::poll(event);
}

int main(int argc, char **argv) {
  QCoreApplication application(argc, argv);
  try {
    verifyBudgetBoundaries();
    verifyGeneratedDrainModel();
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
