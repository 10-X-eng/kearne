#include "local_sketch_session.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QProcessEnvironment>
#include <QTimer>

#include <chrono>
#include <optional>
#include <stdexcept>
#include <utility>

namespace {

using namespace kearne;

void require(bool condition, const char *message) {
  if (!condition)
    throw std::runtime_error(message);
}

ui::LocalSketchSessionConfig config(std::size_t maximumPending = 4U) {
  QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
  environment.insert(QStringLiteral("PYTHONPATH"),
                     QStringLiteral(KEARNE_TEST_SDK_ROOT) +
                         QDir::listSeparator() +
                         QStringLiteral(KEARNE_TEST_GENERATED_PYTHON_ROOT));
  return {QStringLiteral(KEARNE_TEST_PYTHON),
          {QStringLiteral("-m"), QStringLiteral("kearne._worker")},
          std::move(environment),
          maximumPending};
}

Result<ui::LocalSketchProjection> awaitCreate(ui::LocalSketchSession &session) {
  std::optional<Result<ui::LocalSketchProjection>> completion;
  QEventLoop loop;
  QTimer timeout;
  timeout.setSingleShot(true);
  QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
  const bool queued =
      session.create([&](Result<ui::LocalSketchProjection> result) {
        completion = std::move(result);
        loop.quit();
      });
  require(queued, "Sketch creation was not queued");
  timeout.start(std::chrono::seconds{15});
  loop.exec();
  require(completion.has_value(), "Sketch creation did not complete");
  return std::move(*completion);
}

Result<ui::LocalSketchProjection>
awaitRectangle(ui::LocalSketchSession &session,
               ui::LocalRectangleGesture gesture) {
  std::optional<Result<ui::LocalSketchProjection>> completion;
  QEventLoop loop;
  QTimer timeout;
  timeout.setSingleShot(true);
  QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
  const bool queued = session.applyRectangle(
      gesture, [&](Result<ui::LocalSketchProjection> result) {
        completion = std::move(result);
        loop.quit();
      });
  require(queued, "rectangle operation was not queued");
  timeout.start(std::chrono::seconds{15});
  loop.exec();
  require(completion.has_value(), "rectangle operation did not complete");
  return std::move(*completion);
}

void verifyEndToEnd() {
  ui::LocalSketchSession session{config()};
  QElapsedTimer dispatch;
  dispatch.start();
  std::optional<Result<ui::LocalSketchProjection>> createdCompletion;
  QEventLoop loop;
  require(session.create([&](Result<ui::LocalSketchProjection> result) {
    createdCompletion = std::move(result);
    loop.quit();
  }),
          "Sketch creation was not accepted");
  require(dispatch.elapsed() < 25,
          "Sketch creation blocked the caller instead of dispatching");
  QTimer::singleShot(std::chrono::seconds{15}, &loop, &QEventLoop::quit);
  loop.exec();
  require(createdCompletion && createdCompletion->has_value(),
          "empty Sketch did not cross the local engineering lane");
  const ui::LocalSketchProjection created = **createdCompletion;
  require(created.scene && created.scene->primitives().empty() &&
              created.source.contains(QStringLiteral("SketchDefinition")) &&
              created.solveStatus == QStringLiteral("solved") &&
              created.degreesOfFreedom == 0,
          "empty Sketch projection is incomplete");

  auto rectangle = awaitRectangle(session, {-0.04, -0.025, 0.04, 0.025, false});
  require(rectangle && rectangle->scene &&
              rectangle->scene->primitives().size() == 4U &&
              rectangle->source.contains(QStringLiteral("line(")) &&
              rectangle->source.contains(QStringLiteral("coincident(")) &&
              rectangle->sourceRevision != created.sourceRevision &&
              rectangle->projectRevision != created.projectRevision &&
              rectangle->solveStatus == QStringLiteral("underconstrained") &&
              rectangle->degreesOfFreedom == 4,
          "rectangle did not cross source, history, solve, and scene");
  require(session.pendingOperationCount() == 0U,
          "completed operations remained pending");
}

void verifyBoundedDispatch() {
  ui::LocalSketchSession session{config(1U)};
  std::optional<Result<ui::LocalSketchProjection>> completion;
  QEventLoop loop;
  require(session.create([&](Result<ui::LocalSketchProjection> result) {
    completion = std::move(result);
    loop.quit();
  }),
          "bounded session rejected its first operation");
  require(!session.create([](Result<ui::LocalSketchProjection>) {}),
          "bounded session accepted work beyond its queue capacity");
  QTimer::singleShot(std::chrono::seconds{15}, &loop, &QEventLoop::quit);
  loop.exec();
  require(completion && completion->has_value(),
          "bounded session did not complete accepted work");
}

void verifyRejectedEditPreservesHead() {
  ui::LocalSketchSession session{config()};
  auto created = awaitCreate(session);
  require(created.has_value(), "setup Sketch creation failed");
  auto rejected = awaitRectangle(session, {0.01, 0.01, 0.01, 0.03, false});
  require(!rejected, "degenerate rectangle was accepted");
  auto accepted = awaitRectangle(session, {-0.02, -0.01, 0.02, 0.01, false});
  require(accepted && accepted->scene &&
              accepted->scene->primitives().size() == 4U &&
              accepted->projectRevision != created->projectRevision,
          "rejected edit corrupted the accepted Sketch head");
}

} // namespace

int main(int argc, char **argv) {
  QCoreApplication application{argc, argv};
  verifyEndToEnd();
  verifyBoundedDispatch();
  verifyRejectedEditPreservesHead();
  return 0;
}
