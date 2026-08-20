#include "local_sketch_session.hpp"
#include "sketch_gesture_preview.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QProcessEnvironment>
#include <QTimer>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace {

using namespace kearne;

constexpr std::size_t samples = 11U;
constexpr std::size_t previewSamples = 101U;

void require(bool condition, const char *message) {
  if (!condition)
    throw std::runtime_error(message);
}

ui::LocalSketchSessionConfig config() {
  QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
  environment.insert(QStringLiteral("PYTHONPATH"),
                     QStringLiteral(KEARNE_TEST_SDK_ROOT) +
                         QDir::listSeparator() +
                         QStringLiteral(KEARNE_TEST_GENERATED_PYTHON_ROOT));
  return {QStringLiteral(KEARNE_TEST_PYTHON),
          {QStringLiteral("-m"), QStringLiteral("kearne._worker")},
          std::move(environment),
          4U};
}

template <typename Start>
std::pair<Result<ui::LocalSketchProjection>, double> await(Start start,
                                                           double &dispatchMs) {
  std::optional<Result<ui::LocalSketchProjection>> completion;
  QEventLoop loop;
  QTimer timeout;
  timeout.setSingleShot(true);
  QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
  QElapsedTimer total;
  total.start();
  QElapsedTimer dispatch;
  dispatch.start();
  require(start([&](Result<ui::LocalSketchProjection> result) {
            completion = std::move(result);
            loop.quit();
          }),
          "benchmark operation was not queued");
  dispatchMs = static_cast<double>(dispatch.nsecsElapsed()) / 1.0e6;
  timeout.start(std::chrono::seconds{5});
  loop.exec();
  require(completion.has_value(), "benchmark operation timed out");
  return {std::move(*completion),
          static_cast<double>(total.nsecsElapsed()) / 1.0e6};
}

void awaitReady(ui::LocalSketchSession &session) {
  std::optional<Result<void>> completion;
  QEventLoop loop;
  QTimer timeout;
  timeout.setSingleShot(true);
  QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
  session.whenReady([&](Result<void> result) {
    completion = std::move(result);
    loop.quit();
  });
  if (!completion) {
    timeout.start(std::chrono::seconds{5});
    loop.exec();
  }
  require(completion && completion->has_value(),
          "source editor preparation failed");
}

double percentile95(std::vector<double> values) {
  std::ranges::sort(values);
  const auto rank = static_cast<std::size_t>(
      std::ceil(0.95 * static_cast<double>(values.size())));
  return values[std::max<std::size_t>(1U, rank) - 1U];
}

void print(std::string_view name, const std::vector<double> &values) {
  std::cout << name << "_ms=";
  for (std::size_t index = 0; index < values.size(); ++index) {
    if (index != 0U)
      std::cout << ',';
    std::cout << values[index];
  }
  std::cout << " p95_ms=" << percentile95(values) << '\n';
}

} // namespace

int main(int argc, char **argv) {
  QCoreApplication application{argc, argv};
  std::cout << "build_type=" << KEARNE_TEST_BUILD_TYPE << '\n';
  std::vector<double> loadedCreate;
  std::vector<double> warmCreate;
  std::vector<double> warmRectangle;
  std::vector<double> dispatch;
  std::vector<double> dragPreview;
  loadedCreate.reserve(samples);
  warmCreate.reserve(samples);
  warmRectangle.reserve(samples);
  dispatch.reserve(samples * 3U);
  dragPreview.reserve(previewSamples);

  ui::SketchGesturePreview preview;
  for (std::size_t sample = 0; sample < previewSamples; ++sample) {
    QElapsedTimer elapsed;
    elapsed.start();
    require(preview.updateDrag(QStringLiteral("sketch.rectangle"), -40.0, -25.0,
                               40.0 + static_cast<double>(sample) * 0.001, 25.0,
                               false),
            "rectangle drag preview update was rejected");
    dragPreview.push_back(static_cast<double>(elapsed.nsecsElapsed()) / 1.0e6);
  }
  require(preview.visible() && preview.first() == QPointF(-40.0, -25.0),
          "rectangle drag preview projection is invalid");

  for (std::size_t sample = 0; sample < samples; ++sample) {
    ui::LocalSketchSession session{config()};
    double dispatchMs = 0.0;
    auto [created, elapsed] = await(
        [&session](ui::LocalSketchSession::Completion completion) {
          return session.create({}, std::move(completion));
        },
        dispatchMs);
    require(created && created->scene && created->scene->primitives().empty(),
            "loaded New Sketch result is invalid");
    loadedCreate.push_back(elapsed);
    dispatch.push_back(dispatchMs);
  }

  for (std::size_t sample = 0; sample < samples; ++sample) {
    ui::LocalSketchSession session{config()};
    awaitReady(session);
    double createDispatch = 0.0;
    auto [created, createElapsed] = await(
        [&session](ui::LocalSketchSession::Completion completion) {
          return session.create({}, std::move(completion));
        },
        createDispatch);
    require(created && created->scene && created->scene->primitives().empty(),
            "warm New Sketch result is invalid");
    warmCreate.push_back(createElapsed);
    dispatch.push_back(createDispatch);

    double rectangleDispatch = 0.0;
    auto [rectangle, rectangleElapsed] = await(
        [&session](ui::LocalSketchSession::Completion completion) {
          return session.applyRectangle({-0.04, -0.025, 0.04, 0.025, false},
                                        std::move(completion));
        },
        rectangleDispatch);
    require(rectangle && rectangle->scene &&
                rectangle->scene->primitives().size() == 4U &&
                rectangle->source.contains(QStringLiteral("coincident(")),
            "warm rectangle result is invalid");
    warmRectangle.push_back(rectangleElapsed);
    dispatch.push_back(rectangleDispatch);
  }

  print("loaded_new_sketch", loadedCreate);
  print("warm_new_sketch", warmCreate);
  print("warm_rectangle", warmRectangle);
  print("dispatch", dispatch);
  print("drag_preview_update", dragPreview);
  require(percentile95(loadedCreate) <= 500.0,
          "loaded New Sketch exceeded 500 ms p95");
  require(percentile95(warmCreate) <= 150.0,
          "warm New Sketch exceeded 150 ms p95");
  require(percentile95(warmRectangle) <= 100.0,
          "warm rectangle exceeded 100 ms p95");
  require(percentile95(dispatch) <= 100.0,
          "operation acknowledgement exceeded 100 ms p95");
  require(percentile95(dragPreview) <= 16.7,
          "drag preview projection exceeded 16.7 ms p95");
  return 0;
}
