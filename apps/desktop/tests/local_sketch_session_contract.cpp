#include "desktop_controller.hpp"
#include "local_sketch_session.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QProcessEnvironment>
#include <QTimer>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <limits>
#include <numbers>
#include <optional>
#include <stdexcept>
#include <utility>

namespace {

using namespace kearne;

void require(bool condition, const char *message) {
  if (!condition)
    throw std::runtime_error(message);
}

void requireSceneBounds(const render::SketchSceneSnapshot &scene,
                        double minimumX, double minimumY, double maximumX,
                        double maximumY, const char *message) {
  double observedMinimumX = std::numeric_limits<double>::infinity();
  double observedMinimumY = std::numeric_limits<double>::infinity();
  double observedMaximumX = -std::numeric_limits<double>::infinity();
  double observedMaximumY = -std::numeric_limits<double>::infinity();
  for (const render::Point2d point : scene.points()) {
    observedMinimumX = std::min(observedMinimumX, point.x);
    observedMinimumY = std::min(observedMinimumY, point.y);
    observedMaximumX = std::max(observedMaximumX, point.x);
    observedMaximumY = std::max(observedMaximumY, point.y);
  }
  constexpr double tolerance = 1.0e-9;
  require(std::abs(observedMinimumX - minimumX) <= tolerance &&
              std::abs(observedMinimumY - minimumY) <= tolerance &&
              std::abs(observedMaximumX - maximumX) <= tolerance &&
              std::abs(observedMaximumY - maximumY) <= tolerance,
          message);
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

Result<ui::LocalSketchProjection>
awaitCreate(ui::LocalSketchSession &session,
            ui::LocalSketchCreation creation = {}) {
  std::optional<Result<ui::LocalSketchProjection>> completion;
  QEventLoop loop;
  QTimer timeout;
  timeout.setSingleShot(true);
  QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
  const bool queued =
      session.create(creation, [&](Result<ui::LocalSketchProjection> result) {
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
awaitTool(ui::LocalSketchSession &session, ui::LocalSketchToolGesture gesture) {
  std::optional<Result<ui::LocalSketchProjection>> completion;
  QEventLoop loop;
  QTimer timeout;
  timeout.setSingleShot(true);
  QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
  const bool queued =
      session.applyTool(gesture, [&](Result<ui::LocalSketchProjection> result) {
        completion = std::move(result);
        loop.quit();
      });
  require(queued, "Sketch tool operation was not queued");
  timeout.start(std::chrono::seconds{15});
  loop.exec();
  require(completion.has_value(), "Sketch tool operation did not complete");
  return std::move(*completion);
}

Result<ui::LocalSketchProjection>
awaitConstraint(ui::LocalSketchSession &session,
                ui::LocalSketchConstraintGesture gesture) {
  std::optional<Result<ui::LocalSketchProjection>> completion;
  QEventLoop loop;
  QTimer timeout;
  timeout.setSingleShot(true);
  QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
  const bool queued = session.applyConstraint(
      std::move(gesture), [&](Result<ui::LocalSketchProjection> result) {
        completion = std::move(result);
        loop.quit();
      });
  require(queued, "Sketch constraint operation was not queued");
  timeout.start(std::chrono::seconds{15});
  loop.exec();
  require(completion.has_value(),
          "Sketch constraint operation did not complete");
  return std::move(*completion);
}

Result<ui::LocalSketchProjection>
awaitConstructionToggle(ui::LocalSketchSession &session, QString entityId) {
  std::optional<Result<ui::LocalSketchProjection>> completion;
  QEventLoop loop;
  require(
      session.toggleConstruction({std::move(entityId)},
                                 [&](Result<ui::LocalSketchProjection> result) {
                                   completion = std::move(result);
                                   loop.quit();
                                 }),
      "construction toggle was not queued");
  QTimer::singleShot(std::chrono::seconds{15}, &loop, &QEventLoop::quit);
  loop.exec();
  require(completion.has_value(), "construction toggle did not complete");
  return std::move(*completion);
}

Result<ui::LocalSketchProjection>
awaitCurveDrag(ui::LocalSketchSession &session, ui::LocalSketchCurveDrag drag) {
  std::optional<Result<ui::LocalSketchProjection>> completion;
  QEventLoop loop;
  require(session.dragCurve(std::move(drag),
                            [&](Result<ui::LocalSketchProjection> result) {
                              completion = std::move(result);
                              loop.quit();
                            }),
          "curve drag was not queued");
  QTimer::singleShot(std::chrono::seconds{15}, &loop, &QEventLoop::quit);
  loop.exec();
  require(completion.has_value(), "curve drag did not complete");
  return std::move(*completion);
}

Result<ui::LocalSketchProjection>
awaitTransform(ui::LocalSketchSession &session,
               ui::LocalSketchTransform transform) {
  std::optional<Result<ui::LocalSketchProjection>> completion;
  QEventLoop loop;
  require(session.transform(std::move(transform),
                            [&](Result<ui::LocalSketchProjection> result) {
                              completion = std::move(result);
                              loop.quit();
                            }),
          "Sketch transform was not queued");
  QTimer::singleShot(std::chrono::seconds{15}, &loop, &QEventLoop::quit);
  loop.exec();
  require(completion.has_value(), "Sketch transform did not complete");
  return std::move(*completion);
}

Result<ui::LocalSketchProjection> awaitSource(ui::LocalSketchSession &session,
                                              const QString &expected,
                                              QString source) {
  std::optional<Result<ui::LocalSketchProjection>> completion;
  QEventLoop loop;
  QTimer timeout;
  timeout.setSingleShot(true);
  QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
  require(session.replaceSource({expected, std::move(source)},
                                [&](Result<ui::LocalSketchProjection> result) {
                                  completion = std::move(result);
                                  loop.quit();
                                }),
          "source replacement was not queued");
  timeout.start(std::chrono::seconds{15});
  loop.exec();
  require(completion.has_value(), "source replacement did not complete");
  return std::move(*completion);
}

Result<ui::LocalSketchProjection> awaitHistory(ui::LocalSketchSession &session,
                                               bool redo) {
  std::optional<Result<ui::LocalSketchProjection>> completion;
  QEventLoop loop;
  QTimer timeout;
  timeout.setSingleShot(true);
  QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
  const auto delivered = [&](Result<ui::LocalSketchProjection> result) {
    completion = std::move(result);
    loop.quit();
  };
  require(redo ? session.redo(delivered) : session.undo(delivered),
          "history operation was not queued");
  timeout.start(std::chrono::seconds{15});
  loop.exec();
  require(completion.has_value(), "history operation did not complete");
  return std::move(*completion);
}

template <typename Predicate>
ui::FrontendSnapshotPtr awaitSnapshot(ui::FrontendController &port,
                                      Predicate predicate,
                                      const char *failure) {
  if (auto current = port.snapshot(); predicate(*current))
    return current;
  QEventLoop loop;
  QTimer timeout;
  timeout.setSingleShot(true);
  QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
  port.setChangeHandler([&] {
    if (predicate(*port.snapshot()))
      loop.quit();
  });
  timeout.start(std::chrono::seconds{15});
  loop.exec();
  port.setChangeHandler({});
  auto current = port.snapshot();
  require(predicate(*current), failure);
  return current;
}

void verifyEndToEnd() {
  ui::LocalSketchSession session{config()};
  QElapsedTimer dispatch;
  dispatch.start();
  std::optional<Result<ui::LocalSketchProjection>> createdCompletion;
  QEventLoop loop;
  require(session.create({},
                         [&](Result<ui::LocalSketchProjection> result) {
                           createdCompletion = std::move(result);
                           loop.quit();
                         }),
          "Sketch creation was not accepted");
  require(dispatch.elapsed() < 25,
          "Sketch creation blocked the caller instead of dispatching");
  QTimer::singleShot(std::chrono::seconds{15}, &loop, &QEventLoop::quit);
  loop.exec();
  require(createdCompletion && createdCompletion->has_value(),
          "empty Sketch did not cross the design-engine boundary");
  const ui::LocalSketchProjection created = **createdCompletion;
  require(created.scene && created.scene->primitives().empty() &&
              created.source.contains(QStringLiteral("SketchDefinition")) &&
              created.solveStatus == QStringLiteral("solved") &&
              created.degreesOfFreedom == 0,
          "empty Sketch projection is incomplete");

  auto rectangle = awaitTool(session, {ui::LocalSketchToolKind::Rectangle,
                                       {{-0.04, -0.025}, {0.04, 0.025}},
                                       false});
  require(rectangle && rectangle->scene &&
              rectangle->scene->primitives().size() == 4U &&
              rectangle->objects.size() == 1U &&
              rectangle->objects.front().label == "Rectangle 1" &&
              rectangle->objects.front().members.size() == 4U &&
              rectangle->source.contains(QStringLiteral("rectangle(")) &&
              rectangle->source.contains(QStringLiteral("line(")) &&
              rectangle->source.contains(QStringLiteral("coincident(")) &&
              rectangle->sourceRevision != created.sourceRevision &&
              rectangle->projectRevision != created.projectRevision &&
              rectangle->solveStatus == QStringLiteral("underconstrained") &&
              rectangle->degreesOfFreedom == 4,
          "rectangle did not cross source, history, solve, and scene");
  requireSceneBounds(*rectangle->scene, -0.04, -0.025, 0.04, 0.025,
                     "solver moved an already-valid rectangle away from its "
                     "canonical defining points");
  QString editedSource = rectangle->source;
  require(editedSource.contains(QStringLiteral("m(0.04)")),
          "rectangle source has no editable coordinate");
  editedSource.replace(QStringLiteral("m(0.04)"), QStringLiteral("m(0.05)"));
  auto sourceEdit =
      awaitSource(session, rectangle->sourceRevision, std::move(editedSource));
  require(sourceEdit && sourceEdit->scene &&
              sourceEdit->scene->primitives().size() == 4U &&
              sourceEdit->sourceRevision != rectangle->sourceRevision &&
              sourceEdit->projectRevision != rectangle->projectRevision &&
              sourceEdit->source.contains(QStringLiteral("m(0.05)")),
          "native source edit did not cross history, solve, and scene");
  require(sourceEdit->canUndo && !sourceEdit->canRedo,
          "source edit exposed incorrect history availability");

  auto undone = awaitHistory(session, false);
  require(undone && undone->projectRevision == rectangle->projectRevision &&
              undone->sourceRevision == rectangle->sourceRevision &&
              undone->source == rectangle->source &&
              undone->scene == rectangle->scene && undone->canUndo &&
              undone->canRedo,
          "undo did not restore the exact rectangle projection");
  auto redone = awaitHistory(session, true);
  require(redone && redone->projectRevision == sourceEdit->projectRevision &&
              redone->sourceRevision == sourceEdit->sourceRevision &&
              redone->source == sourceEdit->source &&
              redone->scene == sourceEdit->scene && redone->canUndo &&
              !redone->canRedo,
          "redo did not restore the exact source-edit projection");

  auto rectangleAgain = awaitHistory(session, false);
  auto createdAgain = awaitHistory(session, false);
  require(rectangleAgain && createdAgain &&
              createdAgain->projectRevision == created.projectRevision &&
              createdAgain->sourceRevision == created.sourceRevision &&
              createdAgain->scene == created.scene && !createdAgain->canUndo &&
              createdAgain->canRedo,
          "undo to the first Sketch revision was incoherent");
  auto rejectedUndo = awaitHistory(session, false);
  require(!rejectedUndo &&
              rejectedUndo.error().code == "engineering.history.no-undo",
          "undo crossed the first evaluated Sketch revision");
  auto recovered = awaitHistory(session, true);
  require(recovered &&
              recovered->projectRevision == rectangle->projectRevision &&
              recovered->scene == rectangle->scene,
          "failed boundary undo changed the accepted history head");
  require(session.pendingOperationCount() == 0U,
          "completed operations remained pending");
}

void verifyTransformOperations() {
  ui::LocalSketchSession session{config()};
  auto created = awaitCreate(session, {});
  require(created.has_value(), "transform Sketch creation failed");
  auto rectangle = awaitTool(session, {ui::LocalSketchToolKind::Rectangle,
                                       {{-0.04, -0.025}, {0.04, 0.025}},
                                       false});
  require(rectangle && rectangle->objects.size() == 1U,
          "transform rectangle creation failed");
  std::vector<QString> entities;
  for (const auto &member : rectangle->objects.front().members)
    entities.push_back(QString::fromStdString(member.entity.toString()));

  auto moved =
      awaitTransform(session, {entities,
                               ui::LocalSketchTransformMode::Replace,
                               {{0.0, 0.0, 0.01, -0.005, 0.0, 1.0, false}},
                               ui::LocalDimensionCopyPolicy::Preserve,
                               ui::LocalExternalConstraintPolicy::Refuse});
  require(moved && moved->objects.size() == 1U &&
              moved->objects.front().label == "Rectangle 1" && moved->scene &&
              moved->sourceRevision != rectangle->sourceRevision,
          "replace transform did not cross source, history, solve, and scene");
  requireSceneBounds(*moved->scene, -0.03, -0.03, 0.05, 0.02,
                     "replace transform produced incorrect geometry");

  auto restored = awaitHistory(session, false);
  require(restored && restored->source == rectangle->source,
          "transform undo did not restore exact source");
  auto moveRedone = awaitHistory(session, true);
  require(moveRedone && moveRedone->source == moved->source,
          "transform redo did not restore exact source");
  auto copied =
      awaitTransform(session, {entities,
                               ui::LocalSketchTransformMode::Copy,
                               {{0.0, 0.0, 0.10, 0.0, 0.0, 1.0, false},
                                {0.0, 0.0, 0.20, 0.0, 0.0, 1.0, false}},
                               ui::LocalDimensionCopyPolicy::Preserve,
                               ui::LocalExternalConstraintPolicy::Refuse});
  require(copied && copied->scene &&
              copied->scene->primitives().size() == 12U &&
              copied->objects.size() == 3U &&
              copied->objects[0].label == "Rectangle 1" &&
              copied->objects[1].label == "Rectangle 2" &&
              copied->objects[2].label == "Rectangle 3" &&
              copied->source.contains(QStringLiteral("Rectangle 2")) &&
              copied->source.contains(QStringLiteral("Rectangle 3")),
          "array transform lost geometry, human intent, or source");
  auto arrayUndone = awaitHistory(session, false);
  auto arrayRedone = awaitHistory(session, true);
  require(arrayUndone && arrayUndone->source == moved->source && arrayRedone &&
              arrayRedone->source == copied->source,
          "array transform undo/redo was not exact");
}

void verifyCoreGeometryTools() {
  ui::LocalSketchSession session{config()};
  auto created = awaitCreate(session, {});
  require(created.has_value(), "core geometry Sketch creation failed");
  struct Case {
    ui::LocalSketchToolGesture gesture;
    const char *sourceHelper;
    const char *humanLabel;
    std::size_t primitiveCount;
    std::size_t addedPrimitiveCount;
    std::size_t objectCount;
  };
  const std::array cases{
      Case{{ui::LocalSketchToolKind::Point, {{0.01, 0.02}}, false},
           "point(",
           "Point 1",
           1U,
           1U,
           1U},
      Case{{ui::LocalSketchToolKind::Line,
            {{-0.03, -0.01}, {0.03, 0.01}},
            false},
           "line(",
           "Line 1",
           2U,
           1U,
           2U},
      Case{{ui::LocalSketchToolKind::Circle, {{0.0, 0.0}, {0.015, 0.0}}, true},
           "circle(",
           "Circle 1",
           3U,
           1U,
           3U},
      Case{{ui::LocalSketchToolKind::Arc,
            {{0.04, 0.0}, {0.055, 0.0}, {0.04, 0.015}},
            false},
           "arc(",
           "Arc 1",
           4U,
           1U,
           4U},
      Case{{ui::LocalSketchToolKind::Rectangle,
            {{-0.02, -0.01}, {0.02, 0.01}},
            false},
           "rectangle(",
           "Rectangle 1",
           8U,
           4U,
           5U},
      Case{{ui::LocalSketchToolKind::Slot,
            {{0.06, -0.02}, {0.11, -0.02}, {0.085, -0.01}},
            false},
           "slot(",
           "Slot 1",
           12U,
           4U,
           6U},
      Case{{ui::LocalSketchToolKind::ArcSlot,
            {{0.10, 0.06}, {0.13, 0.06}, {0.10, 0.09}, {0.135, 0.06}},
            false},
           "arc_slot(",
           "Arc Slot 1",
           16U,
           4U,
           7U},
      Case{{ui::LocalSketchToolKind::ThreePointCircle,
            {{-0.02, 0.04}, {0.02, 0.04}, {0.0, 0.06}},
            false},
           "circle(",
           "Circle 2",
           17U,
           1U,
           8U},
      Case{{ui::LocalSketchToolKind::ThreePointArc,
            {{-0.02, -0.05}, {0.02, -0.05}, {0.0, -0.03}},
            false},
           "arc(",
           "Arc 2",
           18U,
           1U,
           9U},
      Case{{ui::LocalSketchToolKind::CenterRectangle,
            {{0.08, 0.02}, {0.10, 0.03}},
            false},
           "rectangle(",
           "Rectangle 2",
           22U,
           4U,
           10U},
      Case{{ui::LocalSketchToolKind::Polyline,
            {{-0.10, 0.08}, {-0.07, 0.10}, {-0.04, 0.08}},
            false,
            false},
           "polyline(",
           "Polyline 1",
           24U,
           2U,
           11U},
      Case{{ui::LocalSketchToolKind::Polyline,
            {{-0.10, 0.14}, {-0.07, 0.16}, {-0.04, 0.14}},
            false,
            true},
           "polyline(",
           "Polyline 2",
           27U,
           3U,
           12U},
      Case{{ui::LocalSketchToolKind::Triangle,
            {{-0.12, -0.12}, {-0.09, -0.12}},
            false},
           "regular_polygon(",
           "Triangle 1",
           30U,
           3U,
           13U},
      Case{{ui::LocalSketchToolKind::RegularPolygon,
            {{0.12, -0.12}, {0.15, -0.12}},
            false,
            false,
            9U},
           "regular_polygon(",
           "Polygon 1",
           39U,
           9U,
           14U},
      Case{{ui::LocalSketchToolKind::Oblong,
            {{0.02, -0.16}, {0.07, -0.16}, {0.045, -0.15}},
            false},
           "oblong(",
           "Oblong 1",
           43U,
           4U,
           15U},
      Case{{ui::LocalSketchToolKind::Ellipse,
            {{0.18, 0.12}, {0.21, 0.13}, {0.175, 0.135}},
            false},
           "ellipse(",
           "Ellipse 1",
           44U,
           1U,
           16U},
      Case{{ui::LocalSketchToolKind::ThreePointEllipse,
            {{-0.21, 0.12}, {-0.15, 0.14}, {-0.19, 0.16}},
            true},
           "ellipse(",
           "Ellipse 2",
           45U,
           1U,
           17U},
      Case{{ui::LocalSketchToolKind::EllipticalArc,
            {{0.18, -0.08},
             {0.22, -0.08},
             {0.18, -0.06},
             {0.22, -0.08},
             {0.18, -0.06}},
            false},
           "elliptical_arc(",
           "Elliptical Arc 1",
           46U,
           1U,
           18U},
      Case{{ui::LocalSketchToolKind::HyperbolicArc,
            {{0.25, 0.10}, {0.27, 0.10}, {0.28, 0.11}, {0.28, 0.09}},
            false},
           "hyperbolic_arc(",
           "Hyperbolic Arc 1",
           47U,
           1U,
           19U},
      Case{{ui::LocalSketchToolKind::ParabolicArc,
            {{0.27, -0.12}, {0.25, -0.12}, {0.26, -0.10}, {0.26, -0.14}},
            true},
           "parabolic_arc(",
           "Parabolic Arc 1",
           48U,
           1U,
           20U},
      Case{{ui::LocalSketchToolKind::BSpline,
            {{-0.30, 0.20}, {-0.27, 0.23}, {-0.24, 0.18}, {-0.21, 0.22}},
            false,
            false,
            0U,
            3U},
           "bspline(",
           "B-spline 1",
           49U,
           1U,
           21U},
      Case{{ui::LocalSketchToolKind::PeriodicBSpline,
            {{-0.18, 0.20}, {-0.14, 0.24}, {-0.10, 0.20}},
            false,
            false,
            0U,
            3U},
           "bspline(",
           "B-spline 2",
           50U,
           1U,
           22U},
      Case{{ui::LocalSketchToolKind::InterpolatedBSpline,
            {{0.08, 0.20}, {0.11, 0.24}, {0.14, 0.18}, {0.17, 0.22}},
            false},
           "bspline(",
           "B-spline 3",
           51U,
           1U,
           23U},
      Case{{ui::LocalSketchToolKind::PeriodicInterpolatedBSpline,
            {{0.20, 0.20}, {0.24, 0.24}},
            false},
           "bspline(",
           "B-spline 4",
           52U,
           1U,
           24U},
  };
  for (const Case &test : cases) {
    const auto before = session.pendingOperationCount();
    auto applied = awaitTool(session, test.gesture);
    if (!applied)
      throw std::runtime_error(std::string{test.humanLabel} + ": " +
                               applied.error().code + ": " +
                               applied.error().summary);
    if (!(applied->scene &&
          applied->scene->primitives().size() == test.primitiveCount &&
          applied->objects.size() == test.objectCount &&
          applied->objects.back().label == test.humanLabel &&
          applied->source.contains(QString::fromLatin1(test.sourceHelper)) &&
          (!test.gesture.construction ||
           applied->source.contains(QStringLiteral("construction=True"))) &&
          session.pendingOperationCount() == before))
      throw std::runtime_error(std::string{test.humanLabel} +
                               " did not cross source, solve, and render");
    auto undone = awaitHistory(session, false);
    require(undone && undone->scene &&
                undone->scene->primitives().size() + test.addedPrimitiveCount ==
                    test.primitiveCount,
            "core geometry tool undo restored the wrong scene");
    auto redone = awaitHistory(session, true);
    require(redone && redone->projectRevision == applied->projectRevision &&
                redone->source == applied->source &&
                redone->scene == applied->scene,
            "core geometry tool redo did not restore exact accepted state");
  }
}

void verifyConstraintCommands() {
  ui::LocalSketchSession session{config()};
  auto projection = awaitCreate(session);
  require(projection.has_value(), "constraint fixture Sketch creation failed");
  const auto add = [&](ui::LocalSketchToolGesture gesture) {
    projection = awaitTool(session, std::move(gesture));
    require(projection.has_value(), "constraint fixture geometry failed");
  };
  add({ui::LocalSketchToolKind::Point, {{0.0, 0.0}}});
  add({ui::LocalSketchToolKind::Point, {{0.02, 0.0}}});
  add({ui::LocalSketchToolKind::Point, {{-0.02, 0.0}}});
  add({ui::LocalSketchToolKind::Line, {{0.0, 0.0}, {0.04, 0.0}}});
  add({ui::LocalSketchToolKind::Line, {{0.0, 0.02}, {0.04, 0.02}}});
  add({ui::LocalSketchToolKind::Line, {{0.0, 0.0}, {0.0, 0.04}}});
  add({ui::LocalSketchToolKind::Line, {{0.05, 0.0}, {0.09, 0.0}}});
  add({ui::LocalSketchToolKind::Line, {{0.05, 0.03}, {0.09, 0.04}}});
  add({ui::LocalSketchToolKind::Circle, {{0.0, 0.01}, {0.01, 0.01}}});
  add({ui::LocalSketchToolKind::Circle, {{0.0, 0.01}, {0.005, 0.01}}});

  const auto member = [&projection](std::string_view label) {
    const auto object = std::ranges::find_if(
        projection->objects, [label](const sketch::SketchObject &candidate) {
          return candidate.label == label;
        });
    require(object != projection->objects.end() && !object->members.empty(),
            "constraint fixture object is missing");
    return QString::fromStdString(object->members.front().entity.toString());
  };
  const QString point = member("Point 1");
  const QString midpoint = member("Point 2");
  const QString mirrored = member("Point 3");
  const QString horizontal = member("Line 1");
  const QString parallel = member("Line 2");
  const QString vertical = member("Line 3");
  const QString collinear = member("Line 4");
  const QString inferredHorizontal = member("Line 5");
  const QString outer = member("Circle 1");
  const QString inner = member("Circle 2");
  const auto curve = [](const QString &entity) {
    return ui::LocalSketchConstraintSelection{entity, {}};
  };
  const auto at = [](const QString &entity, const char *key) {
    return ui::LocalSketchConstraintSelection{entity, QString::fromLatin1(key)};
  };
  struct Case {
    ui::LocalSketchConstraintKind kind;
    std::vector<ui::LocalSketchConstraintSelection> selections;
    const char *helper;
  };
  const std::array cases{
      Case{ui::LocalSketchConstraintKind::Coincident,
           {at(point, "point"), at(horizontal, "start")},
           "coincident("},
      Case{ui::LocalSketchConstraintKind::Horizontal,
           {curve(horizontal)},
           "horizontal("},
      Case{ui::LocalSketchConstraintKind::Vertical,
           {curve(vertical)},
           "vertical("},
      Case{ui::LocalSketchConstraintKind::HorizontalVertical,
           {curve(inferredHorizontal)},
           "horizontal("},
      Case{ui::LocalSketchConstraintKind::Parallel,
           {curve(horizontal), curve(parallel)},
           "parallel("},
      Case{ui::LocalSketchConstraintKind::Perpendicular,
           {curve(horizontal), curve(vertical)},
           "perpendicular("},
      Case{ui::LocalSketchConstraintKind::Tangent,
           {curve(horizontal), curve(outer)},
           "tangent("},
      Case{ui::LocalSketchConstraintKind::Equal,
           {curve(horizontal), curve(parallel)},
           "equal("},
      Case{ui::LocalSketchConstraintKind::Concentric,
           {curve(outer), curve(inner)},
           "concentric("},
      Case{ui::LocalSketchConstraintKind::Midpoint,
           {at(midpoint, "point"), curve(horizontal)},
           "midpoint("},
      Case{ui::LocalSketchConstraintKind::PointOnObject,
           {at(point, "point"), curve(outer)},
           "point_on_object("},
      Case{ui::LocalSketchConstraintKind::Symmetric,
           {at(midpoint, "point"), at(mirrored, "point"), curve(vertical)},
           "symmetric("},
      Case{ui::LocalSketchConstraintKind::Symmetric,
           {at(midpoint, "point"), at(mirrored, "point"), at(point, "point")},
           "symmetric_about_point("},
      Case{ui::LocalSketchConstraintKind::Lock, {at(point, "point")}, "lock("},
      Case{ui::LocalSketchConstraintKind::Block, {curve(collinear)}, "block("},
      Case{ui::LocalSketchConstraintKind::Group,
           {curve(parallel), curve(inner)},
           "group("},
      Case{ui::LocalSketchConstraintKind::Collinear,
           {curve(horizontal), curve(collinear)},
           "collinear("},
  };
  for (std::size_t index = 0U; index < cases.size(); ++index) {
    projection = awaitConstraint(
        session, {cases[index].kind, cases[index].selections, std::nullopt});
    require(projection && projection->constraints.size() == index + 1U &&
                projection->source.contains(
                    QString::fromLatin1(cases[index].helper)),
            "constraint command did not cross source, solve, and projection");
  }
  auto undone = awaitHistory(session, false);
  require(undone && undone->constraints.size() + 1U == cases.size(),
          "constraint undo did not restore exact canonical state");
  auto redone = awaitHistory(session, true);
  require(redone && redone->projectRevision == projection->projectRevision &&
              redone->source == projection->source &&
              redone->constraints == projection->constraints,
          "constraint redo did not restore exact canonical state");
  auto axisRemoved = awaitConstraint(
      session, {ui::LocalSketchConstraintKind::RemoveAxisAlignment,
                {curve(horizontal)},
                std::nullopt});
  require(axisRemoved && axisRemoved->constraints.size() + 1U == cases.size() &&
              axisRemoved->source != projection->source,
          "Remove Axis Alignment did not delete canonical constraint source");
  auto axisRestored = awaitHistory(session, false);
  require(axisRestored && axisRestored->source == projection->source &&
              axisRestored->constraints == projection->constraints,
          "Remove Axis Alignment undo did not restore exact source");
}

void verifyDimensionCommands() {
  ui::LocalSketchSession session{config()};
  auto projection = awaitCreate(session);
  require(projection.has_value(), "dimension fixture Sketch creation failed");
  const auto add = [&](ui::LocalSketchToolGesture gesture) {
    projection = awaitTool(session, std::move(gesture));
    require(projection.has_value(), "dimension fixture geometry failed");
  };
  add({ui::LocalSketchToolKind::Line, {{0.0, 0.0}, {0.04, 0.0}}});
  add({ui::LocalSketchToolKind::Line, {{0.0, 0.0}, {0.02, 0.02}}});
  add({ui::LocalSketchToolKind::Circle, {{0.08, 0.0}, {0.09, 0.0}}});
  add({ui::LocalSketchToolKind::Point, {{0.0, 0.0}}});
  add({ui::LocalSketchToolKind::Point, {{0.03, 0.04}}});
  const auto member = [&projection](std::string_view label) {
    const auto object = std::ranges::find_if(
        projection->objects, [label](const sketch::SketchObject &candidate) {
          return candidate.label == label;
        });
    require(object != projection->objects.end() && !object->members.empty(),
            "dimension fixture object is missing");
    return QString::fromStdString(object->members.front().entity.toString());
  };
  const QString horizontal = member("Line 1");
  const QString diagonal = member("Line 2");
  const QString circle = member("Circle 1");
  const QString firstPoint = member("Point 1");
  const QString secondPoint = member("Point 2");
  const auto curve = [](const QString &entity) {
    return ui::LocalSketchConstraintSelection{entity, {}};
  };
  const auto point = [](const QString &entity) {
    return ui::LocalSketchConstraintSelection{entity, QStringLiteral("point")};
  };
  struct Case {
    ui::LocalSketchConstraintKind kind;
    std::vector<ui::LocalSketchConstraintSelection> selections;
    double value;
    const char *helper;
  };
  const std::array cases{
      Case{ui::LocalSketchConstraintKind::Distance,
           {curve(horizontal)},
           0.04,
           "distance("},
      Case{ui::LocalSketchConstraintKind::HorizontalDistance,
           {point(firstPoint), point(secondPoint)},
           0.03,
           "horizontal_distance("},
      Case{ui::LocalSketchConstraintKind::VerticalDistance,
           {point(firstPoint), point(secondPoint)},
           0.04,
           "vertical_distance("},
      Case{ui::LocalSketchConstraintKind::Radius,
           {curve(circle)},
           0.01,
           "radius("},
      Case{ui::LocalSketchConstraintKind::Diameter,
           {curve(circle)},
           0.02,
           "diameter("},
      Case{ui::LocalSketchConstraintKind::Angle,
           {curve(horizontal), curve(diagonal)},
           std::numbers::pi / 4.0,
           "angle("},
  };
  for (const Case &test : cases) {
    const auto before = projection->projectRevision;
    projection =
        awaitConstraint(session, {test.kind, test.selections, test.value});
    require(projection && projection->constraints.size() == 1U &&
                projection->source.contains(QString::fromLatin1(test.helper)),
            "dimension did not cross source, solve, and projection");
    projection = awaitHistory(session, false);
    require(projection && projection->constraints.empty() &&
                projection->projectRevision == before,
            "dimension undo did not restore the exact Sketch");
  }
}

void verifyInteractiveEdits() {
  ui::LocalSketchSession session{config()};
  require(awaitCreate(session).has_value(),
          "interactive edit fixture Sketch was not created");
  auto rectangle = awaitTool(session, {ui::LocalSketchToolKind::Rectangle,
                                       {{-0.04, -0.025}, {0.04, 0.025}},
                                       false});
  require(rectangle && rectangle->scene &&
              rectangle->scene->primitives().size() == 4U &&
              rectangle->objects.size() == 1U && rectangle->profileCount == 1U,
          "interactive edit fixture rectangle was not created");
  const auto &edge = rectangle->scene->primitives().front();
  const QString entityId = QString::fromStdString(edge.entity.toString());

  auto construction = awaitConstructionToggle(session, entityId);
  require(construction && construction->scene,
          "construction toggle did not publish a scene");
  const auto *constructionEdge =
      construction->scene->findPrimitive(edge.entity);
  require(constructionEdge &&
              constructionEdge->style < construction->scene->styles().size() &&
              construction->scene->styles()[constructionEdge->style].role ==
                  render::SketchStyleRole::Construction &&
              construction->profileCount == 0U,
          "construction toggle did not change the canonical render style");
  auto regular = awaitConstructionToggle(session, entityId);
  require(regular && regular->scene && regular->source == rectangle->source &&
              regular->profileCount == 1U,
          "second construction toggle did not restore normal geometry");

  std::optional<Result<std::shared_ptr<const render::SketchSceneSnapshot>>>
      latestPreview;
  std::size_t previewCompletions = 0U;
  QEventLoop previewLoop;
  QElapsedTimer previewDispatch;
  previewDispatch.start();
  constexpr std::size_t previewRequests = 16U;
  for (std::size_t index = 0U; index < previewRequests; ++index) {
    const double targetY = -0.03 - static_cast<double>(index) * 0.001;
    require(session.previewCurveDrag(
                {entityId, 0.0, -0.025, 0.0, targetY},
                [&](Result<std::shared_ptr<const render::SketchSceneSnapshot>>
                        result) {
                  ++previewCompletions;
                  latestPreview = std::move(result);
                  previewLoop.quit();
                }),
            "curve drag preview was not accepted");
  }
  require(previewDispatch.elapsed() < 25,
          "curve drag previews blocked the caller");
  QTimer::singleShot(std::chrono::seconds{5}, &previewLoop,
                     &QEventLoop::quit);
  previewLoop.exec();
  require(latestPreview && latestPreview->has_value() &&
              previewCompletions == 1U && session.pendingOperationCount() == 0U,
          "curve drag preview was not bounded and latest-wins");
  requireSceneBounds(***latestPreview, -0.04, -0.045, 0.04, 0.025,
                     "curve drag preview did not publish the latest geometry");
  session.cancelCurveDragPreview();

  auto resized = awaitCurveDrag(session, {entityId, 0.0, -0.025, 0.0, -0.04});
  require(resized && resized->scene && resized->source != regular->source &&
              resized->degreesOfFreedom == 4,
          "edge drag did not update canonical source and preserve constraints");
  requireSceneBounds(*resized->scene, -0.04, -0.04, 0.04, 0.025,
                     "edge drag did not resize the expected rectangle edge");

  auto ellipse = awaitTool(session, {ui::LocalSketchToolKind::Ellipse,
                                     {{0.10, 0.0}, {0.14, 0.0}, {0.10, 0.02}},
                                     false});
  require(ellipse && ellipse->scene &&
              ellipse->scene->primitives().size() == 5U &&
              ellipse->objects.back().label == "Ellipse 1" &&
              ellipse->profileCount == 2U,
          "interactive edit fixture ellipse was not created");
  const auto &ellipsePrimitive = ellipse->scene->primitives().back();
  const QString ellipseId =
      QString::fromStdString(ellipsePrimitive.entity.toString());
  auto resizedEllipse =
      awaitCurveDrag(session, {ellipseId, 0.14, 0.0, 0.16, 0.0});
  const auto *resizedPrimitive =
      resizedEllipse && resizedEllipse->scene
          ? resizedEllipse->scene->findPrimitive(ellipsePrimitive.entity)
          : nullptr;
  require(resizedPrimitive &&
              resizedPrimitive->kind == render::SketchPrimitiveKind::Ellipse &&
              std::abs(resizedPrimitive->radius - 0.06) < 1.0e-12 &&
              std::abs(resizedPrimitive->secondaryRadius - 0.03) < 1.0e-12 &&
              resizedEllipse->source != ellipse->source,
          "ellipse drag did not preserve its exact aspect ratio in source");
  auto ellipseUndo = awaitHistory(session, false);
  const auto *undonePrimitive =
      ellipseUndo && ellipseUndo->scene
          ? ellipseUndo->scene->findPrimitive(ellipsePrimitive.entity)
          : nullptr;
  require(undonePrimitive && ellipseUndo->source == ellipse->source &&
              undonePrimitive->radius == ellipsePrimitive.radius &&
              undonePrimitive->secondaryRadius ==
                  ellipsePrimitive.secondaryRadius,
          "ellipse drag undo did not restore the exact canonical revision");
  auto ellipseRedo = awaitHistory(session, true);
  require(ellipseRedo && ellipseRedo->source == resizedEllipse->source,
          "ellipse drag redo did not restore the exact canonical revision");
}

void verifyBoundedDispatch() {
  ui::LocalSketchSession session{config(1U)};
  std::optional<Result<ui::LocalSketchProjection>> completion;
  QEventLoop loop;
  require(session.create({},
                         [&](Result<ui::LocalSketchProjection> result) {
                           completion = std::move(result);
                           loop.quit();
                         }),
          "bounded session rejected its first operation");
  require(!session.create({}, [](Result<ui::LocalSketchProjection>) {}),
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
  auto rejected = awaitTool(session, {ui::LocalSketchToolKind::Rectangle,
                                      {{0.01, 0.01}, {0.01, 0.03}},
                                      false});
  require(!rejected, "degenerate rectangle was accepted");
  auto accepted = awaitTool(session, {ui::LocalSketchToolKind::Rectangle,
                                      {{-0.02, -0.01}, {0.02, 0.01}},
                                      false});
  require(accepted && accepted->scene &&
              accepted->scene->primitives().size() == 4U &&
              accepted->projectRevision != created->projectRevision,
          "rejected edit corrupted the accepted Sketch head");
}

void verifyPreparationFailureIsAsynchronous() {
  auto invalid = config();
  invalid.sourceEditorProgram = QStringLiteral("/kearne/missing/python");
  ui::LocalSketchSession session{std::move(invalid)};
  std::optional<Result<void>> completion;
  QEventLoop loop;
  QElapsedTimer dispatch;
  dispatch.start();
  session.whenReady([&](Result<void> result) {
    completion = std::move(result);
    loop.quit();
  });
  require(dispatch.elapsed() < 25, "worker preparation blocked the caller");
  QTimer::singleShot(std::chrono::seconds{5}, &loop, &QEventLoop::quit);
  loop.exec();
  require(completion && !completion->has_value() &&
              completion->error().code == "worker.process.start",
          "worker preparation failure was not delivered asynchronously");
}

void verifyProductionFrontendProjection() {
  auto port = ui::makeDesktopController(
      std::make_unique<ui::LocalSketchSession>(config()),
      std::vector<ui::UiOption>{
          {QStringLiteral("light"), QStringLiteral("Light")}},
      QStringLiteral("light"), QStringLiteral("mm"), QStringLiteral("compact"),
      QStringLiteral("fusion"), QStringLiteral("standard"));
  static_cast<void>(awaitSnapshot(
      *port,
      [](const ui::FrontendSnapshot &value) { return value.backendConnected; },
      "design-engine preparation did not complete"));

  port->selectWorkspace(QStringLiteral("sketch"));
  require(!port->snapshot()->sketchEditing &&
              port->snapshot()->inspectorStatus ==
                  QStringLiteral("Select a plane or create a new Sketch") &&
              std::ranges::any_of(port->snapshot()->commands,
                                  [](const auto &command) {
                                    return command.id ==
                                               QStringLiteral(
                                                   "model.sketch.create") &&
                                           command.available;
                                  }) &&
              std::ranges::none_of(
                  port->snapshot()->commands,
                  [](const auto &command) {
                    return command.id == QStringLiteral("sketch.rectangle") &&
                           command.available;
                  }),
          "Sketch entry context exposed tools before a Sketch was open");
  port->selectWorkspace(QStringLiteral("model"));

  port->requestCommand(QStringLiteral("model.sketch.create"));
  require(port->snapshot()->activeCommandId ==
              QStringLiteral("model.sketch.create"),
          "New Sketch draft did not open");
  port->cancelCommandDraft(QStringLiteral("model.sketch.create"));
  require(port->snapshot()->projectRevision == QStringLiteral("not-created") &&
              !port->snapshot()->sketchScene,
          "cancelling New Sketch changed accepted state");
  port->selectEntity(QStringLiteral("reference.plane.xz"));
  port->requestCommand(QStringLiteral("model.sketch.create"));
  const auto selectedPlaneDraft = port->snapshot();
  require(selectedPlaneDraft->commandDraft.state ==
              ui::CommandDraftState::Pending,
          "canvas plane selection did not immediately queue New Sketch");
  auto created = awaitSnapshot(
      *port,
      [](const ui::FrontendSnapshot &value) {
        return value.activeWorkspaceId == QStringLiteral("sketch") &&
               value.projectRevision != QStringLiteral("not-created");
      },
      "New Sketch did not complete through the production frontend");
  require(
      created->backendConnected && created->sketchScene &&
          created->sketchScene->primitives().empty() &&
          created->activeWorkspaceId == QStringLiteral("sketch") &&
          created->sketchEditing &&
          created->sketchProjection.planeId ==
              QStringLiteral("reference.plane.xz") &&
          created->gridPlaneLabel == QStringLiteral("XZ") &&
          created->modelSource.contains(QStringLiteral("SketchDefinition")) &&
          created->selectedFunction.revision ==
              created->sketchProjection.sourceRevision,
      "New Sketch did not become one production frontend projection");

  port->requestCommand(QStringLiteral("sketch.rectangle"));
  auto rectangleDraft = port->snapshot();
  const QString base = rectangleDraft->projectRevision;
  const ui::SketchInputRequest invalidFirst{QStringLiteral("sketch.rectangle"),
                                            base,
                                            ui::SketchInputKind::PlanePoint,
                                            {0.01, 0.01},
                                            {},
                                            {}};
  const ui::SketchInputRequest invalidOpposite{
      QStringLiteral("sketch.rectangle"),
      base,
      ui::SketchInputKind::PlanePoint,
      {0.01, 0.03},
      {},
      {}};
  require(port->submitSketchInput(invalidFirst) &&
              port->submitSketchInput(invalidOpposite),
          "production frontend rejected invalid rectangle dispatch");
  const auto rejected = awaitSnapshot(
      *port,
      [&base](const ui::FrontendSnapshot &value) {
        return value.projectRevision == base && !value.diagnostics.empty();
      },
      "invalid rectangle did not return a structured rejection");
  require(rejected->projectRevision == base && rejected->sketchScene &&
              rejected->sketchScene->primitives().empty() &&
              rejected->commandDraft.state == ui::CommandDraftState::Editing &&
              rejected->sketchInteraction.inputCount == 0,
          "invalid rectangle changed accepted state or prevented retry");
  const ui::SketchInputRequest first{QStringLiteral("sketch.rectangle"),
                                     base,
                                     ui::SketchInputKind::PlanePoint,
                                     {-0.04, -0.025},
                                     {},
                                     {}};
  const ui::SketchInputRequest opposite{QStringLiteral("sketch.rectangle"),
                                        base,
                                        ui::SketchInputKind::PlanePoint,
                                        {0.04, 0.025},
                                        {},
                                        {}};
  require(port->submitSketchInput(first) && port->submitSketchInput(opposite),
          "production frontend rejected rectangle gesture");
  require(port->snapshot()->commandDraft.state ==
              ui::CommandDraftState::Pending,
          "completed rectangle gesture did not enter pending state");
  const auto rectangle = awaitSnapshot(
      *port,
      [&base](const ui::FrontendSnapshot &value) {
        return value.projectRevision != base && value.sketchScene &&
               value.sketchScene->primitives().size() == 4U;
      },
      "rectangle did not complete through the production frontend");
  require(
      rectangle->sketchScene &&
          rectangle->sketchScene->primitives().size() == 4U &&
          rectangle->projectRevision != base &&
          rectangle->revisionLabel == QStringLiteral("Unsaved changes") &&
          rectangle->modelSource.contains(QStringLiteral("coincident(")) &&
          rectangle->activeCommandId.isEmpty() &&
          rectangle->commandDraft.state == ui::CommandDraftState::None &&
          rectangle->sketchInteraction.inputKind == ui::SketchInputKind::None &&
          std::ranges::any_of(rectangle->structure,
                              [](const ui::StructureItem &item) {
                                return item.label ==
                                           QStringLiteral("Rectangle 1") &&
                                       item.kind ==
                                           QStringLiteral("sketch-rectangle");
                              }) &&
          std::ranges::none_of(
              rectangle->structure,
              [](const ui::StructureItem &item) {
                return item.kind == QStringLiteral("sketch-line") ||
                       item.kind == QStringLiteral("sketch-profile") ||
                       item.id == QStringLiteral("group.sketch.geometry");
              }) &&
          std::ranges::count_if(rectangle->structure,
                                [](const ui::StructureItem &item) {
                                  return item.kind ==
                                             QStringLiteral("sketch-edge") &&
                                         item.depth == 4;
                                }) == 4,
      "rectangle source, solve, scene, structure, and Select state diverged");

  const auto rectangleItem = std::ranges::find_if(
      rectangle->structure, [](const ui::StructureItem &item) {
        return item.kind == QStringLiteral("sketch-rectangle");
      });
  require(rectangleItem != rectangle->structure.end(),
          "Rectangle object is missing from Structure");
  port->selectEntity(rectangleItem->id);
  require(
      port->snapshot()->selectionSummary == QStringLiteral("Rectangle 1") &&
          port->snapshot()->selectedSketchEntityIds.size() == 4U &&
          std::ranges::any_of(port->snapshot()->fields,
                              [](const auto &field) {
                                return field.label == QStringLiteral("Width");
                              }) &&
          std::ranges::any_of(port->snapshot()->fields,
                              [](const auto &field) {
                                return field.label == QStringLiteral("Height");
                              }),
      "Rectangle object did not expose human dimensions");
  const QString bottomEdgeId = QString::fromStdString(
      rectangle->sketchScene->primitives().front().entity.toString());
  port->selectEntity(bottomEdgeId);
  require(port->snapshot()->selectionSummary ==
                  QStringLiteral("Rectangle 1 · Bottom edge") &&
              port->snapshot()->selectedSketchEntityIds ==
                  std::vector<QString>{bottomEdgeId} &&
              std::ranges::any_of(port->snapshot()->fields,
                                  [](const auto &field) {
                                    return field.label ==
                                               QStringLiteral("Length") &&
                                           std::get<QString>(field.value)
                                               .endsWith(QStringLiteral(" mm"));
                                  }) &&
              std::ranges::none_of(port->snapshot()->fields,
                                   [](const auto &field) {
                                     return field.label ==
                                                QStringLiteral("Identity") ||
                                            field.label ==
                                                QStringLiteral("Revision");
                                   }),
          "Sketch selection did not expose concise human properties");

  const auto committedRectangleScene = rectangle->sketchScene;
  require(port->previewSketchCurve(bottomEdgeId, {0.0, -0.025},
                                   {0.0, -0.04}),
          "production frontend rejected live edge preview");
  const auto edgePreview = awaitSnapshot(
      *port,
      [&committedRectangleScene](const ui::FrontendSnapshot &value) {
        return value.sketchScene != committedRectangleScene;
      },
      "live edge preview did not reach the frontend projection");
  require(edgePreview->projectRevision == rectangle->projectRevision &&
              edgePreview->sketchScene,
          "live edge preview changed accepted project state");
  requireSceneBounds(*edgePreview->sketchScene, -0.04, -0.04, 0.04, 0.025,
                     "live edge preview did not follow the pointer");
  port->clearSketchCurvePreview();
  require(port->snapshot()->sketchScene == committedRectangleScene &&
              port->snapshot()->projectRevision == rectangle->projectRevision,
          "canceling live edge preview changed committed state");
  port->selectWorkspace(QStringLiteral("model"));
  port->selectEntity(QStringLiteral("function.sketch"));
  port->selectWorkspace(QStringLiteral("sketch"));
  require(port->snapshot()->sketchEditing,
          "preselected production Sketch did not reopen for editing");

  QString editedSource = rectangle->modelSource;
  require(editedSource.contains(QStringLiteral("m(0.04)")),
          "frontend source has no editable coordinate");
  editedSource.replace(QStringLiteral("m(0.04)"), QStringLiteral("m(0.05)"));
  const QString sourceRevision = rectangle->selectedFunction.revision;
  const ui::SourceEditRequest preview{rectangle->selectedFunction.id,
                                      rectangle->selectedFunction.sourcePath,
                                      sourceRevision, editedSource};
  require(port->submitSourceEdit(preview, ui::SourceEditMode::Preview) &&
              port->snapshot()->projectRevision == rectangle->projectRevision,
          "source preview changed accepted state");
  require(port->submitSourceEdit(preview, ui::SourceEditMode::Apply),
          "production frontend rejected native source edit");
  const auto sourceEdited = awaitSnapshot(
      *port,
      [&sourceRevision](const ui::FrontendSnapshot &value) {
        return value.selectedFunction.revision != sourceRevision;
      },
      "native source edit did not complete through the production frontend");
  require(sourceEdited->sketchScene &&
              sourceEdited->sketchScene->primitives().size() == 4U &&
              sourceEdited->modelSource.contains(QStringLiteral("m(0.05)")) &&
              sourceEdited->canUndo && !sourceEdited->canRedo,
          "native source edit projection is incomplete");

  const QString editedProjectRevision = sourceEdited->projectRevision;
  const auto editedScene = sourceEdited->sketchScene;
  require(port->undo(), "production frontend rejected undo");
  require(!port->snapshot()->canUndo && !port->snapshot()->canRedo &&
              !port->snapshot()->sourceEditingAvailable,
          "pending undo left conflicting edits enabled");
  const auto undone = awaitSnapshot(
      *port,
      [&rectangle](const ui::FrontendSnapshot &value) {
        return value.projectRevision == rectangle->projectRevision;
      },
      "undo did not complete through the production frontend");
  require(undone->modelSource == rectangle->modelSource &&
              undone->sketchScene == rectangle->sketchScene &&
              undone->canUndo && undone->canRedo &&
              undone->sourceEditingAvailable,
          "frontend undo did not restore source, history, and scene together");
  require(port->redo(), "production frontend rejected redo");
  const auto redone = awaitSnapshot(
      *port,
      [&editedProjectRevision](const ui::FrontendSnapshot &value) {
        return value.projectRevision == editedProjectRevision;
      },
      "redo did not complete through the production frontend");
  require(redone->modelSource == sourceEdited->modelSource &&
              redone->sketchScene == editedScene && redone->canUndo &&
              !redone->canRedo && redone->sourceEditingAvailable,
          "frontend redo did not restore source, history, and scene together");

  port->requestCommand(QStringLiteral("sketch.slot"));
  const QString slotBase = port->snapshot()->projectRevision;
  const auto slotPoint = [&slotBase](double x, double y) {
    return ui::SketchInputRequest{QStringLiteral("sketch.slot"),
                                  slotBase,
                                  ui::SketchInputKind::PlanePoint,
                                  {x, y},
                                  {},
                                  {}};
  };
  require(port->submitSketchInput(slotPoint(0.07, -0.02)) &&
              port->submitSketchInput(slotPoint(0.12, -0.02)) &&
              port->submitSketchInput(slotPoint(0.095, -0.01)),
          "production frontend rejected the three-point Slot gesture");
  const auto slot = awaitSnapshot(
      *port,
      [&slotBase](const ui::FrontendSnapshot &value) {
        return value.projectRevision != slotBase && value.sketchScene &&
               value.sketchScene->primitives().size() == 8U;
      },
      "Slot did not complete through the production frontend");
  require(slot->modelSource.contains(QStringLiteral("slot(")) &&
              std::ranges::count_if(
                  slot->structure,
                  [](const ui::StructureItem &item) {
                    return item.label == QStringLiteral("Slot 1") &&
                           item.kind == QStringLiteral("sketch-slot") &&
                           item.depth == 3;
                  }) == 1 &&
              std::ranges::count_if(
                  slot->structure,
                  [](const ui::StructureItem &item) {
                    return item.depth == 4 &&
                           (item.label == QStringLiteral("Start cap") ||
                            item.label == QStringLiteral("End cap") ||
                            item.label == QStringLiteral("Top side") ||
                            item.label == QStringLiteral("Bottom side"));
                  }) == 4 &&
              slot->activeCommandId.isEmpty(),
          "Slot source, object hierarchy, and Select state diverged");
  const auto slotItem =
      std::ranges::find_if(slot->structure, [](const ui::StructureItem &item) {
        return item.kind == QStringLiteral("sketch-slot");
      });
  require(slotItem != slot->structure.end(),
          "Slot object is missing from Structure");
  port->selectEntity(slotItem->id);
  require(port->snapshot()->selectionSummary == QStringLiteral("Slot 1") &&
              std::ranges::any_of(port->snapshot()->fields,
                                  [](const auto &field) {
                                    return field.label ==
                                           QStringLiteral("Center distance");
                                  }) &&
              std::ranges::any_of(port->snapshot()->fields,
                                  [](const auto &field) {
                                    return field.label ==
                                           QStringLiteral("Width");
                                  }),
          "Slot object did not expose human dimensions");

  port->requestCommand(QStringLiteral("sketch.arc-slot"));
  const QString arcSlotBase = port->snapshot()->projectRevision;
  const auto arcSlotPoint = [&arcSlotBase](double x, double y) {
    return ui::SketchInputRequest{QStringLiteral("sketch.arc-slot"),
                                  arcSlotBase,
                                  ui::SketchInputKind::PlanePoint,
                                  {x, y},
                                  {},
                                  {}};
  };
  require(port->submitSketchInput(arcSlotPoint(0.10, 0.06)) &&
              port->submitSketchInput(arcSlotPoint(0.13, 0.06)) &&
              port->submitSketchInput(arcSlotPoint(0.10, 0.09)) &&
              port->submitSketchInput(arcSlotPoint(0.135, 0.06)),
          "production frontend rejected the four-point Arc Slot gesture");
  const auto arcSlot = awaitSnapshot(
      *port,
      [&arcSlotBase](const ui::FrontendSnapshot &value) {
        return value.projectRevision != arcSlotBase && value.sketchScene &&
               value.sketchScene->primitives().size() == 12U;
      },
      "Arc Slot did not complete through the production frontend");
  require(arcSlot->modelSource.contains(QStringLiteral("arc_slot(")) &&
              std::ranges::count_if(
                  arcSlot->structure,
                  [](const ui::StructureItem &item) {
                    return item.label == QStringLiteral("Arc Slot 1") &&
                           item.kind == QStringLiteral("sketch-arc-slot") &&
                           item.depth == 3;
                  }) == 1 &&
              arcSlot->activeCommandId.isEmpty(),
          "Arc Slot source, object hierarchy, and Select state diverged");
  const auto arcSlotItem = std::ranges::find_if(
      arcSlot->structure, [](const ui::StructureItem &item) {
        return item.kind == QStringLiteral("sketch-arc-slot");
      });
  require(arcSlotItem != arcSlot->structure.end(),
          "Arc Slot object is missing from Structure");
  port->selectEntity(arcSlotItem->id);
  require(
      port->snapshot()->selectionSummary == QStringLiteral("Arc Slot 1") &&
          std::ranges::any_of(port->snapshot()->fields,
                              [](const auto &field) {
                                return field.label ==
                                       QStringLiteral("Centerline radius");
                              }) &&
          std::ranges::any_of(port->snapshot()->fields,
                              [](const auto &field) {
                                return field.label == QStringLiteral("Width");
                              }) &&
          std::ranges::any_of(port->snapshot()->fields,
                              [](const auto &field) {
                                return field.label == QStringLiteral("Sweep");
                              }),
      "Arc Slot object did not expose human dimensions");

  port->requestCommand(QStringLiteral("sketch.circle"));
  port->editField(QStringLiteral("sketch.circle.method"),
                  QStringLiteral("three-point"));
  const QString threePointCircleBase = port->snapshot()->projectRevision;
  require(port->snapshot()->sketchInteraction.minimumInputCount == 3 &&
              port->snapshot()->sketchInteraction.maximumInputCount == 3,
          "Three-point Circle method did not request three viewport points");
  const auto circlePoint = [&threePointCircleBase](double x, double y) {
    return ui::SketchInputRequest{QStringLiteral("sketch.circle"),
                                  threePointCircleBase,
                                  ui::SketchInputKind::PlanePoint,
                                  {x, y},
                                  {},
                                  {}};
  };
  require(port->submitSketchInput(circlePoint(-0.02, 0.12)) &&
              port->submitSketchInput(circlePoint(0.02, 0.12)) &&
              port->submitSketchInput(circlePoint(0.0, 0.14)),
          "production frontend rejected Three-point Circle input");
  const auto threePointCircle = awaitSnapshot(
      *port,
      [&threePointCircleBase](const ui::FrontendSnapshot &value) {
        return value.projectRevision != threePointCircleBase &&
               value.sketchScene &&
               value.sketchScene->primitives().size() == 13U;
      },
      "Three-point Circle did not complete through the production frontend");
  require(threePointCircle->modelSource.contains(QStringLiteral("circle(")) &&
              std::ranges::any_of(threePointCircle->structure,
                                  [](const ui::StructureItem &item) {
                                    return item.label ==
                                               QStringLiteral("Circle 1") &&
                                           item.kind ==
                                               QStringLiteral("sketch-circle");
                                  }) &&
              threePointCircle->activeCommandId.isEmpty(),
          "Three-point Circle did not converge on the canonical Circle path");

  port->requestCommand(QStringLiteral("sketch.arc"));
  const QString threePointArcBase = port->snapshot()->projectRevision;
  require(std::ranges::any_of(port->snapshot()->fields,
                              [](const ui::FieldDescriptor &field) {
                                return field.id == QStringLiteral(
                                                       "sketch.arc.method") &&
                                       std::get<QString>(field.value) ==
                                           QStringLiteral("three-point");
                              }),
          "Three-point Arc is not the selected Arc method");
  const auto arcPoint = [&threePointArcBase](double x, double y) {
    return ui::SketchInputRequest{QStringLiteral("sketch.arc"),
                                  threePointArcBase,
                                  ui::SketchInputKind::PlanePoint,
                                  {x, y},
                                  {},
                                  {}};
  };
  require(port->submitSketchInput(arcPoint(-0.02, -0.10)) &&
              port->submitSketchInput(arcPoint(0.02, -0.10)) &&
              port->submitSketchInput(arcPoint(0.0, -0.08)),
          "production frontend rejected Three-point Arc input");
  const auto threePointArc = awaitSnapshot(
      *port,
      [&threePointArcBase](const ui::FrontendSnapshot &value) {
        return value.projectRevision != threePointArcBase &&
               value.sketchScene &&
               value.sketchScene->primitives().size() == 14U;
      },
      "Three-point Arc did not complete through the production frontend");
  require(threePointArc->modelSource.contains(QStringLiteral("arc(")) &&
              std::ranges::any_of(threePointArc->structure,
                                  [](const ui::StructureItem &item) {
                                    return item.label ==
                                               QStringLiteral("Arc 1") &&
                                           item.kind ==
                                               QStringLiteral("sketch-arc");
                                  }) &&
              threePointArc->activeCommandId.isEmpty(),
          "Three-point Arc did not converge on the canonical Arc path");

  port->requestCommand(QStringLiteral("sketch.rectangle"));
  port->editField(QStringLiteral("sketch.rectangle.method"),
                  QStringLiteral("center"));
  const QString centerRectangleBase = port->snapshot()->projectRevision;
  const auto centerRectanglePoint = [&centerRectangleBase](double x, double y) {
    return ui::SketchInputRequest{QStringLiteral("sketch.rectangle"),
                                  centerRectangleBase,
                                  ui::SketchInputKind::PlanePoint,
                                  {x, y},
                                  {},
                                  {}};
  };
  require(port->submitSketchInput(centerRectanglePoint(0.20, 0.10)) &&
              port->submitSketchInput(centerRectanglePoint(0.22, 0.11)),
          "production frontend rejected Center Rectangle input");
  const auto centerRectangle = awaitSnapshot(
      *port,
      [&centerRectangleBase](const ui::FrontendSnapshot &value) {
        return value.projectRevision != centerRectangleBase &&
               value.sketchScene &&
               value.sketchScene->primitives().size() == 18U;
      },
      "Center Rectangle did not complete through the production frontend");
  const auto centeredItem = std::ranges::find_if(
      centerRectangle->structure, [](const ui::StructureItem &item) {
        return item.label == QStringLiteral("Rectangle 2") &&
               item.kind == QStringLiteral("sketch-rectangle");
      });
  require(centeredItem != centerRectangle->structure.end() &&
              centerRectangle->activeCommandId.isEmpty(),
          "Center Rectangle did not compile to symmetric canonical corners");
  port->selectEntity(centeredItem->id);
  require(std::ranges::any_of(port->snapshot()->fields,
                              [](const ui::FieldDescriptor &field) {
                                return field.label == QStringLiteral("Width") &&
                                       std::get<QString>(field.value) ==
                                           QStringLiteral("40 mm");
                              }),
          "Center Rectangle did not preserve its symmetric width");

  port->requestCommand(QStringLiteral("sketch.polyline"));
  port->editField(QStringLiteral("sketch.polyline.close-profile"), true);
  const QString polylineBase = port->snapshot()->projectRevision;
  const auto polylinePoint = [&polylineBase](double x, double y) {
    return ui::SketchInputRequest{QStringLiteral("sketch.polyline"),
                                  polylineBase,
                                  ui::SketchInputKind::PlanePoint,
                                  {x, y},
                                  {},
                                  {}};
  };
  require(port->submitSketchInput(polylinePoint(-0.16, 0.08)) &&
              port->submitSketchInput(polylinePoint(-0.12, 0.12)) &&
              port->submitSketchInput(polylinePoint(-0.08, 0.08)) &&
              port->snapshot()->commandDraft.applySupported,
          "closed Polyline did not accept its point sequence");
  ui::CommandDraftRequest polylineDraft{
      QStringLiteral("sketch.polyline"), polylineBase, {}};
  for (const ui::FieldDescriptor &field : port->snapshot()->fields)
    polylineDraft.fields.push_back({field.id, field.value});
  require(port->submitCommandDraft(polylineDraft, ui::CommandDraftMode::Apply),
          "production frontend rejected completed Polyline input");
  const auto polyline = awaitSnapshot(
      *port,
      [&polylineBase](const ui::FrontendSnapshot &value) {
        return value.projectRevision != polylineBase && value.sketchScene &&
               value.sketchScene->primitives().size() == 21U;
      },
      "Polyline did not complete through the production frontend");
  const auto polylineItem = std::ranges::find_if(
      polyline->structure, [](const ui::StructureItem &item) {
        return item.label == QStringLiteral("Polyline 1") &&
               item.kind == QStringLiteral("sketch-polyline");
      });
  require(polyline->modelSource.contains(QStringLiteral("polyline(")) &&
              polylineItem != polyline->structure.end() &&
              std::ranges::count_if(polyline->structure,
                                    [](const ui::StructureItem &item) {
                                      return item.depth == 4 &&
                                             item.label.startsWith(
                                                 QStringLiteral("Segment "));
                                    }) == 3,
          "Polyline source and human Structure hierarchy diverged");
  port->selectEntity(polylineItem->id);
  require(std::ranges::any_of(port->snapshot()->fields,
                              [](const ui::FieldDescriptor &field) {
                                return field.label ==
                                           QStringLiteral("Segments") &&
                                       std::get<QString>(field.value) ==
                                           QStringLiteral("3");
                              }),
          "Polyline object did not expose its segment count");

  const auto segment = std::ranges::find_if(
      polyline->structure, [](const ui::StructureItem &item) {
        return item.label == QStringLiteral("Segment 3") && item.depth == 4;
      });
  require(segment != polyline->structure.end(),
          "Polyline Segment 3 is missing from Structure");

  port->requestCommand(QStringLiteral("sketch.polygon"));
  port->editField(QStringLiteral("sketch.polygon.method"),
                  QStringLiteral("hexagon"));
  const auto polygonDraft = port->snapshot();
  require(std::ranges::any_of(
              polygonDraft->fields,
              [](const ui::FieldDescriptor &field) {
                return field.id == QStringLiteral("sketch.polygon.sides") &&
                       field.readOnly &&
                       std::get<QString>(field.value) == QStringLiteral("6");
              }),
          "Polygon preset did not expose its exact side count");
  const QString polygonBase = polygonDraft->projectRevision;
  const auto polygonPoint = [&polygonBase](double x, double y) {
    return ui::SketchInputRequest{QStringLiteral("sketch.polygon"),
                                  polygonBase,
                                  ui::SketchInputKind::PlanePoint,
                                  {x, y},
                                  {},
                                  {}};
  };
  require(port->submitSketchInput(polygonPoint(0.12, 0.12)) &&
              port->submitSketchInput(polygonPoint(0.15, 0.12)),
          "production frontend rejected Hexagon input");
  const auto polygon = awaitSnapshot(
      *port,
      [&polygonBase](const ui::FrontendSnapshot &value) {
        return value.projectRevision != polygonBase && value.sketchScene &&
               value.sketchScene->primitives().size() == 27U;
      },
      "Hexagon did not complete through the production frontend");
  const auto polygonItem = std::ranges::find_if(
      polygon->structure, [](const ui::StructureItem &item) {
        return item.label == QStringLiteral("Polygon 1") &&
               item.kind == QStringLiteral("sketch-polygon");
      });
  require(polygon->modelSource.contains(QStringLiteral("regular_polygon(")) &&
              polygonItem != polygon->structure.end() &&
              std::ranges::count_if(polygon->structure,
                                    [](const ui::StructureItem &item) {
                                      return item.depth == 4 &&
                                             item.label.startsWith(
                                                 QStringLiteral("Side "));
                                    }) == 6,
          "Hexagon source and human Structure hierarchy diverged");
  port->selectEntity(polygonItem->id);
  require(std::ranges::any_of(port->snapshot()->fields,
                              [](const ui::FieldDescriptor &field) {
                                return field.label == QStringLiteral("Sides") &&
                                       std::get<QString>(field.value) ==
                                           QStringLiteral("6");
                              }),
          "Hexagon object did not expose its side count");

  port->requestCommand(QStringLiteral("sketch.oblong"));
  const QString oblongBase = port->snapshot()->projectRevision;
  const auto oblongPoint = [&oblongBase](double x, double y) {
    return ui::SketchInputRequest{QStringLiteral("sketch.oblong"),
                                  oblongBase,
                                  ui::SketchInputKind::PlanePoint,
                                  {x, y},
                                  {},
                                  {}};
  };
  require(port->submitSketchInput(oblongPoint(-0.18, -0.12)) &&
              port->submitSketchInput(oblongPoint(-0.12, -0.12)) &&
              port->submitSketchInput(oblongPoint(-0.15, -0.105)),
          "production frontend rejected Oblong input");
  const auto oblong = awaitSnapshot(
      *port,
      [&oblongBase](const ui::FrontendSnapshot &value) {
        return value.projectRevision != oblongBase && value.sketchScene &&
               value.sketchScene->primitives().size() == 31U;
      },
      "Oblong did not complete through the production frontend");
  const auto oblongItem = std::ranges::find_if(
      oblong->structure, [](const ui::StructureItem &item) {
        return item.label == QStringLiteral("Oblong 1") &&
               item.kind == QStringLiteral("sketch-oblong");
      });
  require(oblong->modelSource.contains(QStringLiteral("oblong(")) &&
              oblongItem != oblong->structure.end(),
          "Oblong source and human Structure hierarchy diverged");
  port->selectEntity(oblongItem->id);
  require(std::ranges::any_of(port->snapshot()->fields,
                              [](const ui::FieldDescriptor &field) {
                                return field.label == QStringLiteral("Width") &&
                                       std::get<QString>(field.value) ==
                                           QStringLiteral("30 mm");
                              }),
          "Oblong object did not expose its width");

  port->requestCommand(QStringLiteral("sketch.ellipse"));
  const QString ellipseBase = port->snapshot()->projectRevision;
  const auto ellipsePoint = [&ellipseBase](double x, double y) {
    return ui::SketchInputRequest{QStringLiteral("sketch.ellipse"),
                                  ellipseBase,
                                  ui::SketchInputKind::PlanePoint,
                                  {x, y},
                                  {},
                                  {}};
  };
  require(port->submitSketchInput(ellipsePoint(0.25, 0.10)) &&
              port->submitSketchInput(ellipsePoint(0.29, 0.10)) &&
              port->submitSketchInput(ellipsePoint(0.25, 0.12)),
          "production frontend rejected Center Ellipse input");
  const auto ellipse = awaitSnapshot(
      *port,
      [&ellipseBase](const ui::FrontendSnapshot &value) {
        return value.projectRevision != ellipseBase && value.sketchScene &&
               value.sketchScene->primitives().size() == 32U;
      },
      "Center Ellipse did not complete through the production frontend");
  const auto ellipseItem = std::ranges::find_if(
      ellipse->structure, [](const ui::StructureItem &item) {
        return item.label == QStringLiteral("Ellipse 1") &&
               item.kind == QStringLiteral("sketch-ellipse");
      });
  require(
      ellipse->modelSource.contains(QStringLiteral("ellipse(")) &&
          ellipseItem != ellipse->structure.end() &&
          ellipse->sketchScene->primitives().back().kind ==
              render::SketchPrimitiveKind::Ellipse &&
          std::abs(ellipse->sketchScene->primitives().back().radius - 0.04) <
              1.0e-12 &&
          std::abs(ellipse->sketchScene->primitives().back().secondaryRadius -
                   0.02) < 1.0e-12,
      "Center Ellipse source, Structure, and exact scene diverged");
  port->selectEntity(ellipseItem->id);
  require(std::ranges::any_of(port->snapshot()->fields,
                              [](const ui::FieldDescriptor &field) {
                                return field.label ==
                                           QStringLiteral("Major radius") &&
                                       std::get<QString>(field.value) ==
                                           QStringLiteral("40 mm");
                              }) &&
              std::ranges::any_of(port->snapshot()->fields,
                                  [](const ui::FieldDescriptor &field) {
                                    return field.label ==
                                               QStringLiteral("Minor radius") &&
                                           std::get<QString>(field.value) ==
                                               QStringLiteral("20 mm");
                                  }),
          "Ellipse object did not expose human axis dimensions");

  port->requestCommand(QStringLiteral("sketch.ellipse"));
  port->editField(QStringLiteral("sketch.ellipse.method"),
                  QStringLiteral("three-point"));
  const QString threePointEllipseBase = port->snapshot()->projectRevision;
  const auto threePointEllipsePoint = [&threePointEllipseBase](double x,
                                                               double y) {
    return ui::SketchInputRequest{QStringLiteral("sketch.ellipse"),
                                  threePointEllipseBase,
                                  ui::SketchInputKind::PlanePoint,
                                  {x, y},
                                  {},
                                  {}};
  };
  require(port->submitSketchInput(threePointEllipsePoint(-0.29, 0.10)) &&
              port->submitSketchInput(threePointEllipsePoint(-0.21, 0.10)) &&
              port->submitSketchInput(threePointEllipsePoint(-0.25, 0.12)),
          "production frontend rejected Three-point Ellipse input");
  const auto threePointEllipse = awaitSnapshot(
      *port,
      [&threePointEllipseBase](const ui::FrontendSnapshot &value) {
        return value.projectRevision != threePointEllipseBase &&
               value.sketchScene &&
               value.sketchScene->primitives().size() == 33U;
      },
      "Three-point Ellipse did not complete through the production frontend");
  require(std::ranges::any_of(threePointEllipse->structure,
                              [](const ui::StructureItem &item) {
                                return item.label ==
                                           QStringLiteral("Ellipse 2") &&
                                       item.kind ==
                                           QStringLiteral("sketch-ellipse");
                              }) &&
              threePointEllipse->activeCommandId.isEmpty(),
          "Three-point Ellipse did not reuse the canonical Ellipse path");

  port->requestCommand(QStringLiteral("sketch.elliptical-arc"));
  const QString ellipticalArcBase = port->snapshot()->projectRevision;
  const auto ellipticalArcPoint = [&ellipticalArcBase](double x, double y) {
    return ui::SketchInputRequest{QStringLiteral("sketch.elliptical-arc"),
                                  ellipticalArcBase,
                                  ui::SketchInputKind::PlanePoint,
                                  {x, y},
                                  {},
                                  {}};
  };
  require(port->submitSketchInput(ellipticalArcPoint(0.25, -0.10)) &&
              port->submitSketchInput(ellipticalArcPoint(0.29, -0.10)) &&
              port->submitSketchInput(ellipticalArcPoint(0.25, -0.08)) &&
              port->submitSketchInput(ellipticalArcPoint(0.29, -0.10)) &&
              port->submitSketchInput(ellipticalArcPoint(0.25, -0.08)),
          "production frontend rejected Elliptical Arc input");
  const auto ellipticalArc = awaitSnapshot(
      *port,
      [&ellipticalArcBase](const ui::FrontendSnapshot &value) {
        return value.projectRevision != ellipticalArcBase &&
               value.sketchScene &&
               value.sketchScene->primitives().size() == 34U;
      },
      "Elliptical Arc did not complete through the production frontend");
  const auto ellipticalArcItem = std::ranges::find_if(
      ellipticalArc->structure, [](const ui::StructureItem &item) {
        return item.label == QStringLiteral("Elliptical Arc 1") &&
               item.kind == QStringLiteral("sketch-elliptical-arc");
      });
  require(
      ellipticalArc->modelSource.contains(QStringLiteral("elliptical_arc(")) &&
          ellipticalArcItem != ellipticalArc->structure.end() &&
          ellipticalArc->sketchScene->primitives().back().kind ==
              render::SketchPrimitiveKind::EllipticalArc,
      "Elliptical Arc source, Structure, and exact scene diverged");
  port->selectEntity(ellipticalArcItem->id);
  require(std::ranges::any_of(port->snapshot()->fields,
                              [](const ui::FieldDescriptor &field) {
                                return field.label == QStringLiteral("Sweep") &&
                                       std::get<QString>(field.value) ==
                                           QStringLiteral("90.0°");
                              }),
          "Elliptical Arc object did not expose its human sweep");

  const std::size_t constraintCount = std::ranges::count(
      ellipticalArc->structure, QStringLiteral("sketch-constraint"),
      &ui::StructureItem::kind);
  port->requestCommand(QStringLiteral("sketch.horizontal-vertical"));
  const QString horizontalBase = port->snapshot()->projectRevision;
  require(port->submitSketchInput({QStringLiteral("sketch.horizontal-vertical"),
                                   horizontalBase,
                                   ui::SketchInputKind::Entity,
                                   {},
                                   segment->id,
                                   {}}),
          "production frontend rejected Horizontal / Vertical selection");
  const auto constrained = awaitSnapshot(
      *port,
      [&horizontalBase](const ui::FrontendSnapshot &value) {
        return value.projectRevision != horizontalBase &&
               value.activeCommandId.isEmpty();
      },
      "Horizontal / Vertical did not complete through the production frontend");
  require(static_cast<std::size_t>(std::ranges::count(
              constrained->structure, QStringLiteral("sketch-constraint"),
              &ui::StructureItem::kind)) == constraintCount + 1U &&
              constrained->modelSource.contains(QStringLiteral("horizontal(")),
          "Horizontal constraint source and Structure projection diverged");
  const auto constraintItem = std::ranges::find_if(
      constrained->structure.rbegin(), constrained->structure.rend(),
      [](const ui::StructureItem &item) {
        return item.kind == QStringLiteral("sketch-constraint");
      });
  require(constraintItem != constrained->structure.rend(),
          "applied Horizontal constraint is missing from Structure");
  port->selectEntity(constraintItem->id);
  require(port->snapshot()->selectionSummary == constraintItem->label &&
              std::ranges::any_of(port->snapshot()->fields,
                                  [](const ui::FieldDescriptor &field) {
                                    return field.label ==
                                               QStringLiteral("Type") &&
                                           std::get<QString>(field.value) ==
                                               QStringLiteral("Horizontal");
                                  }) &&
              std::ranges::none_of(port->snapshot()->fields,
                                   [](const ui::FieldDescriptor &field) {
                                     return field.label ==
                                                QStringLiteral("Identity") ||
                                            field.label ==
                                                QStringLiteral("Revision");
                                   }),
          "constraint selection did not expose concise human properties");

  port->requestCommand(QStringLiteral("sketch.dimension"));
  port->editField(QStringLiteral("sketch.dimension.kind"),
                  QStringLiteral("distance"));
  port->editField(QStringLiteral("sketch.dimension.expression"),
                  QStringLiteral("40 mm"));
  const QString dimensionBase = port->snapshot()->projectRevision;
  require(port->submitSketchInput({QStringLiteral("sketch.dimension"),
                                   dimensionBase,
                                   ui::SketchInputKind::Entity,
                                   {},
                                   segment->id,
                                   {}}),
          "production frontend rejected Distance geometry");
  ui::CommandDraftRequest dimensionDraft{
      QStringLiteral("sketch.dimension"),
      port->snapshot()->commandDraft.baseRevision,
      {}};
  for (const ui::FieldDescriptor &field : port->snapshot()->fields)
    dimensionDraft.fields.push_back({field.id, field.value});
  require(port->submitCommandDraft(dimensionDraft, ui::CommandDraftMode::Apply),
          "production frontend rejected Distance apply");
  const auto dimensioned = awaitSnapshot(
      *port,
      [&dimensionBase](const ui::FrontendSnapshot &value) {
        return value.projectRevision != dimensionBase &&
               value.activeCommandId.isEmpty();
      },
      "Distance did not complete through the production frontend");
  require(dimensioned->modelSource.contains(QStringLiteral("distance(")) &&
              std::ranges::count(dimensioned->structure,
                                 QStringLiteral("sketch-constraint"),
                                 &ui::StructureItem::kind) ==
                  static_cast<std::ptrdiff_t>(constraintCount + 2U),
          "Distance source and Structure projection diverged");

  struct SplineCase {
    QString command;
    std::vector<ui::PlanePoint> points;
    bool finishByClosingPoint = false;
  };
  const std::array splineCases{
      SplineCase{QStringLiteral("sketch.bspline.control-points"),
                 {{-0.30, 0.20}, {-0.27, 0.23}, {-0.24, 0.18}, {-0.21, 0.22}}},
      SplineCase{QStringLiteral("sketch.bspline.periodic-control-points"),
                 {{-0.18, 0.20}, {-0.14, 0.24}, {-0.10, 0.20}},
                 true},
      SplineCase{QStringLiteral("sketch.bspline.interpolation"),
                 {{0.08, 0.20}, {0.11, 0.24}, {0.14, 0.18}, {0.17, 0.22}}},
      SplineCase{QStringLiteral("sketch.bspline.periodic-interpolation"),
                 {{0.20, 0.20}, {0.24, 0.24}},
                 true},
  };
  std::size_t expectedPrimitives =
      dimensioned->sketchScene->primitives().size();
  std::size_t expectedSplines = dimensioned->sketchScene->splines().size();
  QString editableSplineEntity;
  for (std::size_t caseIndex = 0U; caseIndex < splineCases.size();
       ++caseIndex) {
    const SplineCase &test = splineCases[caseIndex];
    port->requestCommand(test.command);
    const QString splineBase = port->snapshot()->projectRevision;
    require(port->snapshot()->activeCommandId == test.command &&
                port->snapshot()->sketchInteraction.minimumInputCount == 2 &&
                port->snapshot()->sketchInteraction.maximumInputCount == 0,
            "B-spline command did not expose its multi-point interaction");
    for (const ui::PlanePoint point : test.points)
      require(port->submitSketchInput({test.command,
                                       splineBase,
                                       ui::SketchInputKind::PlanePoint,
                                       point,
                                       {},
                                       {}}),
              "B-spline command rejected a valid canvas point");
    if (caseIndex == 0U) {
      require(port->removeLastSketchInput() &&
                  port->snapshot()->sketchInteraction.inputCount ==
                      static_cast<int>(test.points.size() - 1U) &&
                  port->submitSketchInput({test.command,
                                           splineBase,
                                           ui::SketchInputKind::PlanePoint,
                                           test.points.back(),
                                           {},
                                           {}}),
              "B-spline Backspace editing did not restore the draft");
    }
    require(port->snapshot()->commandDraft.applySupported &&
                std::ranges::any_of(
                    port->snapshot()->sketchProjection.primitives,
                    [](const ui::SketchPrimitiveProjection &primitive) {
                      return primitive.draft;
                    }),
            "B-spline points did not produce an applicable viewport preview");
    if (test.finishByClosingPoint) {
      require(port->submitSketchInput({test.command,
                                       splineBase,
                                       ui::SketchInputKind::PlanePoint,
                                       test.points.front(),
                                       {},
                                       {}}),
              "periodic B-spline closing click did not finish the command");
    } else {
      ui::CommandDraftRequest draft{test.command, splineBase, {}};
      for (const ui::FieldDescriptor &field : port->snapshot()->fields)
        draft.fields.push_back({field.id, field.value});
      require(port->submitCommandDraft(draft, ui::CommandDraftMode::Apply),
              "B-spline Apply did not finish the command");
    }
    ++expectedPrimitives;
    ++expectedSplines;
    const auto applied = awaitSnapshot(
        *port,
        [&splineBase, expectedPrimitives,
         expectedSplines](const ui::FrontendSnapshot &value) {
          return value.projectRevision != splineBase && value.sketchScene &&
                 value.sketchScene->primitives().size() == expectedPrimitives &&
                 value.sketchScene->splines().size() == expectedSplines;
        },
        "B-spline did not complete through the production frontend");
    require(applied->activeCommandId.isEmpty() &&
                applied->sketchScene->primitives().back().kind ==
                    render::SketchPrimitiveKind::BSpline &&
                applied->modelSource.contains(QStringLiteral("bspline(")) &&
                std::ranges::any_of(
                    applied->structure,
                    [expectedSplines](const ui::StructureItem &item) {
                      return item.label == QStringLiteral("B-spline %1")
                                               .arg(expectedSplines) &&
                             item.kind == QStringLiteral("sketch-bspline");
                    }),
            "B-spline source, scene, Structure, and Select state diverged");
    if (editableSplineEntity.isEmpty())
      editableSplineEntity = QString::fromStdString(
          applied->sketchScene->primitives().back().entity.toString());
    const QString splineLabel =
        QStringLiteral("B-spline %1").arg(expectedSplines);
    const auto splineItem = std::ranges::find(applied->structure, splineLabel,
                                              &ui::StructureItem::label);
    require(splineItem != applied->structure.end(),
            "B-spline object is missing from Structure");
    port->selectEntity(splineItem->id);
    require(
        std::ranges::any_of(port->snapshot()->fields,
                            [](const ui::FieldDescriptor &field) {
                              return field.label == QStringLiteral("Degree");
                            }) &&
            std::ranges::any_of(port->snapshot()->fields,
                                [](const ui::FieldDescriptor &field) {
                                  return field.label ==
                                         QStringLiteral("Control points");
                                }) &&
            std::ranges::any_of(port->snapshot()->fields,
                                [](const ui::FieldDescriptor &field) {
                                  return field.label == QStringLiteral("Form");
                                }),
        "B-spline selection did not expose human curve properties");
  }

  struct SplineEditCase {
    QString command;
    QString field;
    QString value;
  };
  const std::array splineEdits{
      SplineEditCase{QStringLiteral("sketch.bspline.increase-degree"), {}, {}},
      SplineEditCase{
          QStringLiteral("sketch.bspline.decrease-degree"),
          QStringLiteral("sketch.bspline.decrease-degree.maximum-deviation"),
          QStringLiteral("0.001 mm")},
      SplineEditCase{QStringLiteral("sketch.bspline.insert-knot"),
                     QStringLiteral("sketch.bspline.insert-knot.parameter"),
                     QStringLiteral("0.5")},
      SplineEditCase{
          QStringLiteral("sketch.bspline.increase-knot-multiplicity"),
          QStringLiteral("sketch.bspline.increase-knot-multiplicity.knot"),
          QStringLiteral("2")},
      SplineEditCase{
          QStringLiteral("sketch.bspline.decrease-knot-multiplicity"),
          QStringLiteral("sketch.bspline.decrease-knot-multiplicity.knot"),
          QStringLiteral("2")},
      SplineEditCase{QStringLiteral("sketch.bspline.pole-weight"),
                     QStringLiteral("sketch.bspline.pole-weight.weight"),
                     QStringLiteral("1.5")},
  };
  QString editRevision = port->snapshot()->projectRevision;
  for (const SplineEditCase &edit : splineEdits) {
    port->requestCommand(edit.command);
    require(port->snapshot()->activeCommandId == edit.command &&
                port->snapshot()->sketchInteraction.inputKind ==
                    ui::SketchInputKind::Entity &&
                port->submitSketchInput({edit.command,
                                         editRevision,
                                         ui::SketchInputKind::Entity,
                                         {},
                                         editableSplineEntity,
                                         {}}),
            "B-spline edit did not accept its curve selection");
    if (!edit.field.isEmpty())
      port->editField(edit.field, edit.value);
    ui::CommandDraftRequest draft{
        edit.command, port->snapshot()->commandDraft.baseRevision, {}};
    for (const ui::FieldDescriptor &field : port->snapshot()->fields)
      draft.fields.push_back({field.id, field.value});
    require(port->submitCommandDraft(draft, ui::CommandDraftMode::Apply),
            "B-spline edit did not apply through the GUI command path");
    const auto edited = awaitSnapshot(
        *port,
        [&editRevision](const ui::FrontendSnapshot &value) {
          return value.projectRevision != editRevision &&
                 value.activeCommandId.isEmpty();
        },
        "B-spline edit did not complete through the production frontend");
    require(edited->sketchScene &&
                edited->sketchScene->primitives().size() ==
                    expectedPrimitives &&
                edited->sketchScene->splines().size() == expectedSplines &&
                edited->modelSource.contains(QStringLiteral("bspline(")) &&
                edited->inspectorStatus.contains(
                    QStringLiteral("B-spline updated")),
            "B-spline edit lost source, scene, or human completion state");
    editRevision = edited->projectRevision;
  }

  require(port->undo(), "B-spline edit undo was not accepted");
  const auto splineUndo = awaitSnapshot(
      *port,
      [&editRevision](const ui::FrontendSnapshot &value) {
        return value.projectRevision != editRevision;
      },
      "B-spline edit undo did not restore its prior revision");
  const QString undoneSplineRevision = splineUndo->projectRevision;
  require(port->redo(), "B-spline edit redo was not accepted");
  const auto splineRedo = awaitSnapshot(
      *port,
      [&undoneSplineRevision](const ui::FrontendSnapshot &value) {
        return value.projectRevision != undoneSplineRevision;
      },
      "B-spline edit redo did not restore its revision");
  require(splineRedo->sketchScene &&
              splineRedo->sketchScene->splines().size() == expectedSplines,
          "B-spline edit redo lost its rendered curve");

  std::vector<QString> rectangleEntities;
  rectangleEntities.reserve(rectangle->sketchScene->primitives().size());
  for (const auto &primitive : rectangle->sketchScene->primitives())
    rectangleEntities.push_back(
        QString::fromStdString(primitive.entity.toString()));
  const auto beforeTransforms = port->snapshot();
  const auto applyTransform =
      [&](const QString &command,
          std::initializer_list<std::pair<QString, QString>> fields,
          std::size_t expectedPrimitiveCount) {
        port->requestCommand(command);
        const QString transformBase = port->snapshot()->projectRevision;
        require(
            port->snapshot()->activeCommandId == command &&
                port->snapshot()->sketchInteraction.inputKind ==
                    ui::SketchInputKind::Entity &&
                port->snapshot()->sketchInteraction.minimumInputCount == 1 &&
                port->snapshot()->sketchInteraction.maximumInputCount == 1024,
            "Sketch transform did not expose whole-object selection");
        for (const QString &entity : rectangleEntities)
          require(port->submitSketchInput({command,
                                           transformBase,
                                           ui::SketchInputKind::Entity,
                                           {},
                                           entity,
                                           {}}),
                  "Sketch transform rejected a selected Rectangle edge");
        for (const auto &[suffix, value] : fields)
          port->editField(command + suffix, value);
        ui::CommandDraftRequest draft{
            command, port->snapshot()->commandDraft.baseRevision, {}};
        for (const ui::FieldDescriptor &field : port->snapshot()->fields)
          draft.fields.push_back({field.id, field.value});
        require(port->submitCommandDraft(draft, ui::CommandDraftMode::Apply) &&
                    port->snapshot()->commandDraft.state ==
                        ui::CommandDraftState::Pending,
                "Sketch transform did not enter its background operation");
        return awaitSnapshot(
            *port,
            [&transformBase,
             expectedPrimitiveCount](const ui::FrontendSnapshot &value) {
              return value.projectRevision != transformBase &&
                     value.activeCommandId.isEmpty() && value.sketchScene &&
                     value.sketchScene->primitives().size() ==
                         expectedPrimitiveCount;
            },
            "Sketch transform did not publish its accepted revision");
      };

  auto transformed =
      applyTransform(QStringLiteral("sketch.translate"),
                     {{QStringLiteral(".first-x"), QStringLiteral("5 mm")},
                      {QStringLiteral(".first-y"), QStringLiteral("3 mm")}},
                     expectedPrimitives);
  transformed =
      applyTransform(QStringLiteral("sketch.rotate"),
                     {{QStringLiteral(".angle"), QStringLiteral("30 deg")}},
                     expectedPrimitives);
  transformed =
      applyTransform(QStringLiteral("sketch.scale"),
                     {{QStringLiteral(".mode"), QStringLiteral("copy")},
                      {QStringLiteral(".factor"), QStringLiteral("1.25")}},
                     expectedPrimitives + 4U);
  transformed =
      applyTransform(QStringLiteral("sketch.symmetry"),
                     {{QStringLiteral(".axis-angle"), QStringLiteral("0 deg")}},
                     expectedPrimitives + 8U);
  transformed =
      applyTransform(QStringLiteral("sketch.translate"),
                     {{QStringLiteral(".mode"), QStringLiteral("array")},
                      {QStringLiteral(".first-x"), QStringLiteral("100 mm")},
                      {QStringLiteral(".copies"), QStringLiteral("1")},
                      {QStringLiteral(".second-y"), QStringLiteral("100 mm")},
                      {QStringLiteral(".rows"), QStringLiteral("2")}},
                     expectedPrimitives + 20U);
  transformed = applyTransform(
      QStringLiteral("sketch.rotate"),
      {{QStringLiteral(".mode"), QStringLiteral("array")},
       {QStringLiteral(".angle"), QStringLiteral("180 deg")},
       {QStringLiteral(".copies"), QStringLiteral("2")},
       {QStringLiteral(".dimensions"), QStringLiteral("equalize")}},
      expectedPrimitives + 28U);
  require(transformed->modelSource.contains(QStringLiteral("Rectangle 8")) &&
              transformed->inspectorStatus ==
                  QStringLiteral("Rotate complete · Select active"),
          "array labels or human transform completion state diverged");

  for (int operation = 0; operation < 6; ++operation) {
    const QString transformRevision = port->snapshot()->projectRevision;
    require(port->undo(), "Sketch transform undo was not accepted");
    static_cast<void>(awaitSnapshot(
        *port,
        [&transformRevision](const ui::FrontendSnapshot &value) {
          return value.projectRevision != transformRevision;
        },
        "Sketch transform undo did not publish its restored revision"));
  }
  require(
      port->snapshot()->projectRevision == beforeTransforms->projectRevision &&
          port->snapshot()->modelSource == beforeTransforms->modelSource &&
          port->snapshot()->sketchScene == beforeTransforms->sketchScene,
      "Sketch transform undo chain did not restore the exact source and scene");

  const auto applyCurveModification = [&](const QString &command,
                                          std::initializer_list<
                                              ui::SketchInputRequest>
                                              selections,
                                          std::initializer_list<
                                              std::pair<QString, QString>>
                                              fields,
                                          const QString &objectLabel,
                                          const QString &sourceToken) {
    port->requestCommand(command);
    const QString baseRevision = port->snapshot()->projectRevision;
    require(port->snapshot()->activeCommandId == command &&
                port->snapshot()->sketchInteraction.inputKind ==
                    ui::SketchInputKind::Entity,
            "Sketch curve modification did not expose curve selection");
    for (ui::SketchInputRequest selection : selections) {
      selection.commandId = command;
      selection.expectedRevision = baseRevision;
      require(port->submitSketchInput(selection),
              "Sketch curve modification rejected a selected curve");
    }
    for (const auto &[suffix, value] : fields)
      port->editField(command + suffix, value);
    ui::CommandDraftRequest draft{
        command, port->snapshot()->commandDraft.baseRevision, {}};
    for (const ui::FieldDescriptor &field : port->snapshot()->fields)
      draft.fields.push_back({field.id, field.value});
    require(port->submitCommandDraft(draft, ui::CommandDraftMode::Apply) &&
                port->snapshot()->commandDraft.state ==
                    ui::CommandDraftState::Pending,
            "Sketch curve modification did not enter its background operation");
    const auto modified = awaitSnapshot(
        *port,
        [](const ui::FrontendSnapshot &value) {
          return value.commandDraft.state != ui::CommandDraftState::Pending;
        },
        "Sketch curve modification did not publish its accepted revision");
    if (modified->commandDraft.state == ui::CommandDraftState::Rejected)
      throw std::runtime_error(
          QStringLiteral("Sketch curve modification failed: %1")
              .arg(modified->inspectorStatus)
              .toStdString());
    require(
        modified->projectRevision != baseRevision &&
            modified->activeCommandId.isEmpty() && modified->sketchScene &&
            modified->sketchScene->primitives().size() ==
                expectedPrimitives + 1U &&
            std::ranges::any_of(modified->structure,
                                [&objectLabel](const ui::StructureItem &item) {
                                  return item.label == objectLabel;
                                }) &&
            modified->modelSource.contains(sourceToken) &&
            modified->inspectorStatus.endsWith(
                QStringLiteral("complete · Select active")),
        "Sketch curve modification lost source or human completion state");
    const QString modifiedRevision = modified->projectRevision;
    require(port->undo(), "Sketch curve modification undo was not accepted");
    const auto restored = awaitSnapshot(
        *port,
        [&modifiedRevision](const ui::FrontendSnapshot &value) {
          return value.projectRevision != modifiedRevision;
        },
        "Sketch curve modification undo did not publish its restored revision");
    require(restored->projectRevision == beforeTransforms->projectRevision &&
                restored->modelSource == beforeTransforms->modelSource &&
                restored->sketchScene == beforeTransforms->sketchScene,
            "Sketch curve modification undo did not restore exact state");
  };

  const ui::SketchInputRequest bottomPick{
      {}, {}, ui::SketchInputKind::Entity, {0.0, -0.025}, rectangleEntities[0],
      {}};
  const ui::SketchInputRequest rightPick{
      {}, {}, ui::SketchInputKind::Entity, {0.05, 0.02}, rectangleEntities[1],
      {}};
  applyCurveModification(
      QStringLiteral("sketch.fillet"), {bottomPick, rightPick},
      {{QStringLiteral(".size"), QStringLiteral("5 mm")},
       {QStringLiteral(".external-constraints"), QStringLiteral("detach")}},
      QStringLiteral("Fillet 1"), QStringLiteral("fillet_object("));
  applyCurveModification(
      QStringLiteral("sketch.chamfer"), {bottomPick, rightPick},
      {{QStringLiteral(".size"), QStringLiteral("5 mm")},
       {QStringLiteral(".external-constraints"), QStringLiteral("detach")}},
      QStringLiteral("Chamfer 1"), QStringLiteral("chamfer_object("));
  applyCurveModification(
      QStringLiteral("sketch.offset"), {bottomPick},
      {{QStringLiteral(".distance"), QStringLiteral("5 mm")}},
      QStringLiteral("Offset 1"), QStringLiteral("offset_object("));
}

} // namespace

int main(int argc, char **argv) {
  QCoreApplication application{argc, argv};
  verifyEndToEnd();
  verifyTransformOperations();
  verifyCoreGeometryTools();
  verifyConstraintCommands();
  verifyDimensionCommands();
  verifyInteractiveEdits();
  verifyBoundedDispatch();
  verifyRejectedEditPreservesHead();
  verifyPreparationFailureIsAsynchronous();
  verifyProductionFrontendProjection();
  return 0;
}
