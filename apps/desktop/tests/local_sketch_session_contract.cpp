#include "development_frontend_port.hpp"
#include "local_sketch_session.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QProcessEnvironment>
#include <QTimer>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
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
ui::FrontendSnapshotPtr awaitSnapshot(ui::FrontendPort &port,
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

void verifyInteractiveEdits() {
  ui::LocalSketchSession session{config()};
  require(awaitCreate(session).has_value(),
          "interactive edit fixture Sketch was not created");
  auto rectangle = awaitRectangle(session, {-0.04, -0.025, 0.04, 0.025, false});
  require(rectangle && rectangle->scene &&
              rectangle->scene->primitives().size() == 4U &&
              rectangle->profileCount == 1U,
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

  auto resized = awaitCurveDrag(session, {entityId, 0.0, -0.025, 0.0, -0.04});
  require(resized && resized->scene && resized->source != regular->source &&
              resized->degreesOfFreedom == 4,
          "edge drag did not update canonical source and preserve constraints");
  requireSceneBounds(*resized->scene, -0.04, -0.04, 0.04, 0.025,
                     "edge drag did not resize the expected rectangle edge");
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
  auto rejected = awaitRectangle(session, {0.01, 0.01, 0.01, 0.03, false});
  require(!rejected, "degenerate rectangle was accepted");
  auto accepted = awaitRectangle(session, {-0.02, -0.01, 0.02, 0.01, false});
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
  auto port = ui::makeLocalFrontendPort(
      std::make_unique<ui::LocalSketchSession>(config()),
      std::vector<ui::UiOption>{
          {QStringLiteral("light"), QStringLiteral("Light")}},
      QStringLiteral("light"), QStringLiteral("mm"), QStringLiteral("compact"),
      QStringLiteral("fusion"), QStringLiteral("standard"));
  static_cast<void>(awaitSnapshot(
      *port,
      [](const ui::FrontendSnapshot &value) { return value.backendConnected; },
      "local engineering preparation did not complete"));

  port->requestCommand(QStringLiteral("model.sketch.create"));
  require(port->snapshot()->activeCommandId ==
              QStringLiteral("model.sketch.create"),
          "New Sketch draft did not open");
  port->cancelCommandDraft(QStringLiteral("model.sketch.create"));
  require(port->snapshot()->projectRevision == QStringLiteral("not-created") &&
              !port->snapshot()->sketchScene,
          "cancelling New Sketch changed accepted state");
  port->requestCommand(QStringLiteral("model.sketch.create"));
  port->selectEntity(QStringLiteral("reference.plane.xz"));
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
          rectangle->modelSource.contains(QStringLiteral("coincident(")) &&
          rectangle->activeCommandId.isEmpty() &&
          rectangle->commandDraft.state == ui::CommandDraftState::None &&
          rectangle->sketchInteraction.inputKind == ui::SketchInputKind::None &&
          std::ranges::any_of(
              rectangle->structure,
              [](const ui::StructureItem &item) {
                return item.id == QStringLiteral("output.sketch.profile.1");
              }),
      "rectangle source, solve, scene, structure, and Select state diverged");

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
}

} // namespace

int main(int argc, char **argv) {
  QCoreApplication application{argc, argv};
  verifyEndToEnd();
  verifyInteractiveEdits();
  verifyBoundedDispatch();
  verifyRejectedEditPreservesHead();
  verifyPreparationFailureIsAsynchronous();
  verifyProductionFrontendProjection();
  return 0;
}
