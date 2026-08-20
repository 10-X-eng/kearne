#include "local_sketch_session.hpp"

#include <kearne/adapters/ceres_sketch_solver.hpp>
#include <kearne/adapters/sketch_source_worker.hpp>
#include <kearne/document/canonical.hpp>
#include <kearne/document/content_store.hpp>
#include <kearne/engineering/service.hpp>
#include <kearne/sketch/tools.hpp>
#include <kearne/sketch_workflow/workflow.hpp>

#include <QByteArray>
#include <QDateTime>
#include <QMetaObject>
#include <QPointer>
#include <QRandomGenerator>
#include <QThread>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace kearne::ui {
namespace {

template <typename Id> Result<Id> makeId() {
  typename Id::RandomTail random{};
  QRandomGenerator *generator = QRandomGenerator::system();
  for (std::size_t offset = 0; offset < random.size(); offset += 4U) {
    const std::uint32_t value = generator->generate();
    const std::size_t count = std::min<std::size_t>(4U, random.size() - offset);
    for (std::size_t byte = 0; byte < count; ++byte)
      random[offset + byte] = static_cast<std::uint8_t>(value >> (byte * 8U));
  }
  const qint64 now = QDateTime::currentMSecsSinceEpoch();
  if (now < 0)
    return std::unexpected(diagnostic("desktop.sketch.clock",
                                      "system time cannot produce a UUIDv7"));
  return Id::create(static_cast<std::uint64_t>(now), random);
}

template <typename Id, std::size_t Size>
Result<std::array<Id, Size>> makeIds() {
  std::vector<Id> values;
  values.reserve(Size);
  for (std::size_t index = 0; index < Size; ++index) {
    auto value = makeId<Id>();
    if (!value)
      return std::unexpected(std::move(value.error()));
    values.push_back(std::move(*value));
  }
  return [&values]<std::size_t... Index>(std::index_sequence<Index...>) {
    return std::array<Id, Size>{std::move(values[Index])...};
  }(std::make_index_sequence<Size>{});
}

template <typename Digest>
Result<Digest> operationDigest(std::string_view context, const JobId &job) {
  document::CanonicalWriter writer;
  writer.header(context, 1U);
  writer.identifier(job);
  auto digest = document::hashCanonical<ContentDigest>(context, writer.value());
  if (!digest)
    return std::unexpected(std::move(digest.error()));
  return Digest::fromBytes(digest->algorithm(), digest->bytes());
}

Result<sketch::LengthValue> length(double metres) {
  return sketch::LengthValue::fromSi(metres);
}

adapters::FramedWorkerProcessConfig
sourceEditorConfig(LocalSketchSessionConfig config) {
  return {std::move(config.sourceEditorProgram),
          std::move(config.sourceEditorArguments),
          std::move(config.sourceEditorEnvironment)};
}

QString solveStatus(sketch::SolveStatus value) {
  switch (value) {
  case sketch::SolveStatus::Solved:
    return QStringLiteral("solved");
  case sketch::SolveStatus::Underconstrained:
    return QStringLiteral("underconstrained");
  case sketch::SolveStatus::Inconsistent:
    return QStringLiteral("inconsistent");
  case sketch::SolveStatus::Diverged:
    return QStringLiteral("diverged");
  case sketch::SolveStatus::Cancelled:
    return QStringLiteral("cancelled");
  }
  return QStringLiteral("failed");
}

Result<LocalSketchProjection>
frontendProjection(const sketch_workflow::SketchState &state,
                   LocalSketchPlane plane) {
  if (!state.evaluation)
    return std::unexpected(state.evaluationFailure.value_or(diagnostic(
        "desktop.sketch.evaluation-missing",
        "Sketch committed without an inspectable evaluation result")));
  if (!state.evaluation->replacementScene)
    return std::unexpected(
        state.evaluation->diagnostics.empty()
            ? diagnostic("desktop.sketch.scene-withheld",
                         "Sketch evaluation withheld its render scene")
            : state.evaluation->diagnostics.front());
  if (state.evaluation->solve.degreesOfFreedom >
      static_cast<std::size_t>(std::numeric_limits<int>::max()))
    return std::unexpected(
        diagnostic("desktop.sketch.dof-range",
                   "Sketch degrees of freedom exceed UI range"));
  const auto &bytes = state.source.bytes;
  const QString source =
      QString::fromUtf8(reinterpret_cast<const char *>(bytes.data()),
                        static_cast<qsizetype>(bytes.size()));
  return LocalSketchProjection{
      QString::fromStdString(state.revision.toString()),
      QString::fromStdString(state.source.digest.toString()),
      QString::fromStdString(state.address.sourcePath),
      QString::fromStdString(state.address.functionName),
      source,
      plane,
      solveStatus(state.evaluation->solve.status),
      static_cast<int>(state.evaluation->solve.degreesOfFreedom),
      sketch::closedProfileCount(state.definition),
      state.evaluation->replacementScene,
  };
}

class LocalHumanPermissionPolicy final : public engineering::PermissionPolicy {
public:
  LocalHumanPermissionPolicy(ActorId actor, PermissionContextId context)
      : actor_(std::move(actor)), context_(std::move(context)) {}

  Result<void>
  authorize(const engineering::PermissionRequest &request) const override {
    if (request.actor != actor_ || request.context != context_ ||
        request.origin != Origin::Human)
      return std::unexpected(
          diagnostic("desktop.engineering.permission",
                     "local Sketch session rejected an unauthorized command"));
    return {};
  }

private:
  ActorId actor_;
  PermissionContextId context_;
};

struct Backend final {
  static Result<std::unique_ptr<Backend>>
  create(adapters::FramedWorkerProcessConfig process) {
    auto project = makeId<ProjectId>();
    auto actor = makeId<ActorId>();
    auto permission = makeId<PermissionContextId>();
    auto root = makeId<RecordId>();
    auto genesisTransaction = makeId<TransactionId>();
    auto worker = makeId<WorkerInstanceId>();
    auto binding = makeId<ModelBindingId>();
    auto schemaJob = makeId<JobId>();
    auto warmupJob = makeId<JobId>();
    if (!project)
      return std::unexpected(std::move(project.error()));
    if (!actor)
      return std::unexpected(std::move(actor.error()));
    if (!permission)
      return std::unexpected(std::move(permission.error()));
    if (!root)
      return std::unexpected(std::move(root.error()));
    if (!genesisTransaction)
      return std::unexpected(std::move(genesisTransaction.error()));
    if (!worker)
      return std::unexpected(std::move(worker.error()));
    if (!binding)
      return std::unexpected(std::move(binding.error()));
    if (!schemaJob)
      return std::unexpected(std::move(schemaJob.error()));
    if (!warmupJob)
      return std::unexpected(std::move(warmupJob.error()));
    auto schema = operationDigest<SchemaSetDigest>(
        "kearne.desktop.local-schema.v1", *schemaJob);
    if (!schema)
      return std::unexpected(std::move(schema.error()));

    auto content = std::make_shared<document::InMemoryContentStore>(
        document::ContentStoreLimits{1U << 20U, 16U << 20U});
    auto permissions =
        std::make_shared<LocalHumanPermissionPolicy>(*actor, *permission);
    auto engineering = engineering::InMemoryEngineeringService::create(
        {*project, *root, *genesisTransaction, *actor, *schema,
         static_cast<std::uint64_t>(QDateTime::currentMSecsSinceEpoch()),
         "Untitled"},
        permissions, content);
    if (!engineering)
      return std::unexpected(std::move(engineering.error()));
    try {
      auto backend = std::unique_ptr<Backend>(new Backend{
          {*project, *actor, *permission},
          *binding,
          std::move(content),
          std::move(*engineering),
          std::move(process),
          *worker,
      });
      auto warmed = backend->sourceEditor.create(*warmupJob,
                                                 "_kearne_source_editor_ready");
      if (!warmed)
        return std::unexpected(std::move(warmed.error()));
      return backend;
    } catch (const std::bad_alloc &) {
      return std::unexpected(
          diagnostic("desktop.sketch.session-allocation",
                     "local Sketch session allocation failed"));
    }
  }

  Result<LocalSketchProjection> createSketch(LocalSketchCreation creation) {
    if (state)
      return std::unexpected(
          diagnostic("desktop.sketch.already-created",
                     "the local proof session already contains a Sketch"));
    auto operation = nextOperation();
    if (!operation)
      return std::unexpected(std::move(operation.error()));
    auto identity = nextEvaluation(operation->sourceJob);
    if (!identity)
      return std::unexpected(std::move(identity.error()));
    auto created =
        workflow.create({"model/sketch.py", "sketch"}, *operation, *identity);
    if (!created)
      return std::unexpected(std::move(created.error()));
    plane = creation.plane;
    publishState(std::move(*created));
    return projection();
  }

  Result<LocalSketchProjection>
  applyRectangle(const LocalRectangleGesture &gesture) {
    if (!state)
      return std::unexpected(
          diagnostic("desktop.sketch.not-created",
                     "create a Sketch before applying a rectangle"));
    auto firstX = length(gesture.firstXMetres);
    auto firstY = length(gesture.firstYMetres);
    auto oppositeX = length(gesture.oppositeXMetres);
    auto oppositeY = length(gesture.oppositeYMetres);
    if (!firstX)
      return std::unexpected(std::move(firstX.error()));
    if (!firstY)
      return std::unexpected(std::move(firstY.error()));
    if (!oppositeX)
      return std::unexpected(std::move(oppositeX.error()));
    if (!oppositeY)
      return std::unexpected(std::move(oppositeY.error()));

    auto edges = makeIds<SketchEntityId, 4U>();
    auto constraints = makeIds<SketchConstraintId, 8U>();
    if (!edges)
      return std::unexpected(std::move(edges.error()));
    if (!constraints)
      return std::unexpected(std::move(constraints.error()));
    sketch::RectangleToolIds ids{std::move(*edges), std::move(*constraints)};
    auto operation = nextOperation();
    if (!operation)
      return std::unexpected(std::move(operation.error()));
    auto identity = nextEvaluation(operation->sourceJob);
    if (!identity)
      return std::unexpected(std::move(identity.error()));
    auto edited = workflow.applyTool(*state, *operation,
                                     sketch::RectangleToolInput{
                                         ids,
                                         {*firstX, *firstY},
                                         {*oppositeX, *oppositeY},
                                         gesture.construction,
                                     },
                                     *identity);
    if (!edited)
      return std::unexpected(std::move(edited.error()));
    publishState(std::move(*edited));
    return projection();
  }

  Result<LocalSketchProjection>
  toggleConstruction(const LocalSketchConstructionToggle &toggle) {
    if (!state)
      return std::unexpected(
          diagnostic("desktop.sketch.not-created",
                     "create a Sketch before editing its geometry"));
    auto id = SketchEntityId::parse(toggle.entityId.toStdString());
    if (!id)
      return std::unexpected(std::move(id.error()));
    auto edited = sketch::toggleConstruction(state->definition, *id);
    if (!edited)
      return std::unexpected(std::move(edited.error()));
    return applyEdits(std::move(*edited));
  }

  Result<LocalSketchProjection> dragCurve(const LocalSketchCurveDrag &drag) {
    if (!state)
      return std::unexpected(
          diagnostic("desktop.sketch.not-created",
                     "create a Sketch before editing its geometry"));
    if (!std::isfinite(drag.firstXMetres) ||
        !std::isfinite(drag.firstYMetres) ||
        !std::isfinite(drag.currentXMetres) ||
        !std::isfinite(drag.currentYMetres))
      return std::unexpected(diagnostic("desktop.sketch.drag-non-finite",
                                        "Sketch drag coordinates are invalid"));
    auto id = SketchEntityId::parse(drag.entityId.toStdString());
    if (!id)
      return std::unexpected(std::move(id.error()));
    auto firstX = length(drag.firstXMetres);
    auto firstY = length(drag.firstYMetres);
    auto currentX = length(drag.currentXMetres);
    auto currentY = length(drag.currentYMetres);
    if (!firstX)
      return std::unexpected(std::move(firstX.error()));
    if (!firstY)
      return std::unexpected(std::move(firstY.error()));
    if (!currentX)
      return std::unexpected(std::move(currentX.error()));
    if (!currentY)
      return std::unexpected(std::move(currentY.error()));
    auto edited = sketch::dragCurve(
        state->definition, {*id, {*firstX, *firstY}, {*currentX, *currentY}});
    if (!edited)
      return std::unexpected(std::move(edited.error()));
    return applyEdits(std::move(*edited));
  }

  Result<LocalSketchProjection>
  replaceSource(const LocalSourceReplacement &replacement) {
    if (!state)
      return std::unexpected(
          diagnostic("desktop.sketch.not-created",
                     "create a Sketch before replacing its source"));
    if (replacement.expectedSourceRevision !=
        QString::fromStdString(state->source.digest.toString()))
      return std::unexpected(
          diagnostic("desktop.sketch.stale-source",
                     "Sketch source changed after it was observed"));
    const QByteArray utf8 = replacement.source.toUtf8();
    document::Bytes bytes(
        reinterpret_cast<const std::uint8_t *>(utf8.constData()),
        reinterpret_cast<const std::uint8_t *>(utf8.constData()) + utf8.size());
    auto operation = nextOperation();
    if (!operation)
      return std::unexpected(std::move(operation.error()));
    auto identity = nextEvaluation(operation->sourceJob);
    if (!identity)
      return std::unexpected(std::move(identity.error()));
    auto recognized =
        sourceEditor.replace(operation->sourceJob, bytes,
                             state->address.functionName, state->source.digest);
    if (!recognized)
      return std::unexpected(std::move(recognized.error()));
    auto edited = workflow.replaceSource(
        *state, *operation, std::move(recognized->source),
        std::move(recognized->definition), *identity);
    if (!edited)
      return std::unexpected(std::move(edited.error()));
    publishState(std::move(*edited));
    return projection();
  }

  Result<LocalSketchProjection> undo() {
    if (!state)
      return std::unexpected(diagnostic("desktop.sketch.not-created",
                                        "there is no Sketch to undo"));
    const auto current = parents.find(state->revision);
    if (current == parents.end() || !current->second)
      return std::unexpected(
          diagnostic("engineering.history.no-undo",
                     "workspace has no Sketch revision to undo"));
    if (auto moved = engineering->undo(); !moved)
      return std::unexpected(std::move(moved.error()));
    return restoreCachedHead();
  }

  Result<LocalSketchProjection> redo() {
    if (!state)
      return std::unexpected(diagnostic("desktop.sketch.not-created",
                                        "there is no Sketch to redo"));
    const std::vector<RevisionId> choices = engineering->redoChoices();
    if (choices.empty())
      return std::unexpected(diagnostic("engineering.history.no-redo",
                                        "there is no revision to redo"));
    if (choices.size() != 1U)
      return std::unexpected(diagnostic(
          "engineering.history.ambiguous-redo",
          "redo requires an explicit revision on a branched history"));
    if (std::ranges::none_of(states, [&choices](const auto &candidate) {
          return candidate->revision == choices.front();
        }))
      return std::unexpected(diagnostic(
          "desktop.sketch.history-cache",
          "redo revision is unavailable in the local evaluation cache",
          Severity::Fatal));
    if (auto moved = engineering->redo(choices.front()); !moved)
      return std::unexpected(std::move(moved.error()));
    return restoreCachedHead();
  }

  void shutdown() { sourceEditor.stop(); }

private:
  Result<LocalSketchProjection> applyEdits(sketch::AppliedEdits edits) {
    auto operation = nextOperation();
    if (!operation)
      return std::unexpected(std::move(operation.error()));
    auto identity = nextEvaluation(operation->sourceJob);
    if (!identity)
      return std::unexpected(std::move(identity.error()));
    auto edited =
        workflow.applyEdits(*state, *operation, std::move(edits), *identity);
    if (!edited)
      return std::unexpected(std::move(edited.error()));
    publishState(std::move(*edited));
    return projection();
  }

  void publishState(sketch_workflow::SketchState next) {
    auto published =
        std::make_shared<const sketch_workflow::SketchState>(std::move(next));
    const std::optional<RevisionId> parent =
        state ? std::optional<RevisionId>{state->revision} : std::nullopt;
    const auto existing =
        std::ranges::find_if(states, [&published](const auto &candidate) {
          return candidate->revision == published->revision;
        });
    if (existing == states.end())
      states.push_back(published);
    else
      *existing = published;
    parents.insert_or_assign(published->revision, parent);
    state = std::move(published);
  }

  Result<LocalSketchProjection> projection() const {
    auto projected = frontendProjection(*state, plane);
    if (!projected)
      return std::unexpected(std::move(projected.error()));
    const auto current = parents.find(state->revision);
    projected->canUndo =
        current != parents.end() && current->second.has_value();
    const std::vector<RevisionId> choices = engineering->redoChoices();
    projected->canRedo =
        choices.size() == 1U &&
        std::ranges::any_of(states, [&choices](const auto &candidate) {
          return candidate->revision == choices.front();
        });
    return projected;
  }

  Result<LocalSketchProjection> restoreCachedHead() {
    const RevisionId &head = engineering->headSnapshot()->revisionId();
    const auto found =
        std::ranges::find_if(states, [&head](const auto &candidate) {
          return candidate->revision == head;
        });
    if (found == states.end())
      return std::unexpected(diagnostic(
          "desktop.sketch.history-cache",
          "Sketch revision is unavailable in the local evaluation cache",
          Severity::Fatal));
    state = *found;
    return projection();
  }

  Backend(sketch_workflow::ActorContext actorContext,
          ModelBindingId attachmentBinding,
          std::shared_ptr<document::InMemoryContentStore> contentStore,
          std::unique_ptr<engineering::InMemoryEngineeringService>
              engineeringService,
          adapters::FramedWorkerProcessConfig process, WorkerInstanceId worker)
      : actor(std::move(actorContext)), binding(std::move(attachmentBinding)),
        content(std::move(contentStore)),
        engineering(std::move(engineeringService)),
        sourceEditor(std::move(process), std::move(worker)),
        workflow(this->actor, *this->content, *this->engineering, sourceEditor,
                 solver) {}

  Result<sketch_workflow::OperationContext> nextOperation() {
    auto request = makeId<RequestId>();
    auto transaction = makeId<TransactionId>();
    auto job = makeId<JobId>();
    auto gesture = makeId<GestureId>();
    if (!request)
      return std::unexpected(std::move(request.error()));
    if (!transaction)
      return std::unexpected(std::move(transaction.error()));
    if (!job)
      return std::unexpected(std::move(job.error()));
    if (!gesture)
      return std::unexpected(std::move(gesture.error()));
    return sketch_workflow::OperationContext{
        *request,
        *transaction,
        *job,
        engineering->headSnapshot()->revisionId(),
        Origin::Human,
        *gesture,
        static_cast<std::uint64_t>(QDateTime::currentMSecsSinceEpoch()),
    };
  }

  Result<sketch_workflow::EvaluationIdentity> nextEvaluation(const JobId &job) {
    if (sceneGeneration == std::numeric_limits<std::uint64_t>::max())
      return std::unexpected(diagnostic(
          "desktop.sketch.scene-generation-exhausted",
          "local Sketch scene generation is exhausted", Severity::Fatal));
    auto session = render::RenderSessionHandle::create(1U);
    auto generation = render::SceneGeneration::create(++sceneGeneration);
    auto evaluation = operationDigest<EvaluationKey>(
        "kearne.desktop.sketch-evaluation.v1", job);
    auto digest = operationDigest<render::SceneDigest>(
        "kearne.desktop.sketch-scene.v1", job);
    if (!session)
      return std::unexpected(std::move(session.error()));
    if (!generation)
      return std::unexpected(std::move(generation.error()));
    if (!evaluation)
      return std::unexpected(std::move(evaluation.error()));
    if (!digest)
      return std::unexpected(std::move(digest.error()));
    return sketch_workflow::EvaluationIdentity{*session, binding, *evaluation,
                                               *generation, *digest};
  }

  sketch_workflow::ActorContext actor;
  ModelBindingId binding;
  std::shared_ptr<document::InMemoryContentStore> content;
  std::unique_ptr<engineering::InMemoryEngineeringService> engineering;
  adapters::SketchSourceWorker sourceEditor;
  adapters::CeresSketchSolver solver;
  sketch_workflow::Workflow workflow;
  std::vector<std::shared_ptr<const sketch_workflow::SketchState>> states;
  std::map<RevisionId, std::optional<RevisionId>> parents;
  std::shared_ptr<const sketch_workflow::SketchState> state;
  LocalSketchPlane plane = LocalSketchPlane::XY;
  std::uint64_t sceneGeneration = 0U;
};

class SessionWorker final : public QObject {
public:
  explicit SessionWorker(adapters::FramedWorkerProcessConfig process)
      : process_(std::move(process)) {}

  Result<void> prepare() { return ensureBackend(); }

  Result<LocalSketchProjection> createSketch(LocalSketchCreation creation) {
    if (auto ready = ensureBackend(); !ready)
      return std::unexpected(std::move(ready.error()));
    return backend_->createSketch(creation);
  }

  Result<LocalSketchProjection>
  applyRectangle(const LocalRectangleGesture &gesture) {
    if (auto ready = ensureBackend(); !ready)
      return std::unexpected(std::move(ready.error()));
    return backend_->applyRectangle(gesture);
  }

  Result<LocalSketchProjection>
  toggleConstruction(const LocalSketchConstructionToggle &toggle) {
    if (auto ready = ensureBackend(); !ready)
      return std::unexpected(std::move(ready.error()));
    return backend_->toggleConstruction(toggle);
  }

  Result<LocalSketchProjection> dragCurve(const LocalSketchCurveDrag &drag) {
    if (auto ready = ensureBackend(); !ready)
      return std::unexpected(std::move(ready.error()));
    return backend_->dragCurve(drag);
  }

  Result<LocalSketchProjection>
  replaceSource(const LocalSourceReplacement &replacement) {
    if (auto ready = ensureBackend(); !ready)
      return std::unexpected(std::move(ready.error()));
    return backend_->replaceSource(replacement);
  }

  Result<LocalSketchProjection> undo() {
    if (auto ready = ensureBackend(); !ready)
      return std::unexpected(std::move(ready.error()));
    return backend_->undo();
  }

  Result<LocalSketchProjection> redo() {
    if (auto ready = ensureBackend(); !ready)
      return std::unexpected(std::move(ready.error()));
    return backend_->redo();
  }

  void shutdown() {
    if (backend_)
      backend_->shutdown();
  }

private:
  Result<void> ensureBackend() {
    if (backend_)
      return {};
    auto created = Backend::create(process_);
    if (!created)
      return std::unexpected(std::move(created.error()));
    backend_ = std::move(*created);
    return {};
  }

  adapters::FramedWorkerProcessConfig process_;
  std::unique_ptr<Backend> backend_;
};

} // namespace

QString localSketchPlaneId(LocalSketchPlane plane) {
  switch (plane) {
  case LocalSketchPlane::XY:
    return QStringLiteral("reference.plane.xy");
  case LocalSketchPlane::XZ:
    return QStringLiteral("reference.plane.xz");
  case LocalSketchPlane::YZ:
    return QStringLiteral("reference.plane.yz");
  }
  return {};
}

std::optional<LocalSketchPlane> localSketchPlaneFromId(QStringView id) {
  if (id == QStringLiteral("reference.plane.xy"))
    return LocalSketchPlane::XY;
  if (id == QStringLiteral("reference.plane.xz"))
    return LocalSketchPlane::XZ;
  if (id == QStringLiteral("reference.plane.yz"))
    return LocalSketchPlane::YZ;
  return std::nullopt;
}

struct LocalSketchSession::Impl final {
  Impl(LocalSketchSession &session, LocalSketchSessionConfig config)
      : owner(session), maximumPending(config.maximumPendingOperations),
        worker(new SessionWorker{sourceEditorConfig(std::move(config))}) {
    worker->moveToThread(&thread);
    QObject::connect(&thread, &QThread::finished, worker,
                     &QObject::deleteLater);
    thread.setObjectName(QStringLiteral("Kearne Sketch engineering"));
    thread.start();
  }

  void startPreparation() {
    if (preparationQueued || preparationReady || preparationError || !worker)
      return;
    preparationQueued = true;
    const QPointer<LocalSketchSession> lifetime{&owner};
    const bool queued = QMetaObject::invokeMethod(
        worker,
        [target = worker, lifetime] {
          auto result = std::make_shared<Result<void>>(target->prepare());
          if (!lifetime)
            return;
          static_cast<void>(QMetaObject::invokeMethod(
              lifetime,
              [lifetime, result = std::move(result)]() mutable {
                if (lifetime)
                  lifetime->impl_->finishPreparation(std::move(*result));
              },
              Qt::QueuedConnection));
        },
        Qt::QueuedConnection);
    if (!queued)
      finishPreparation(std::unexpected(diagnostic(
          "desktop.sketch.preparation-dispatch",
          "Sketch engineering preparation could not be dispatched")));
  }

  void finishPreparation(Result<void> result) {
    preparationQueued = false;
    preparationReady = result.has_value();
    if (!result)
      preparationError = std::move(result.error());
    auto completions = std::move(readinessCompletions);
    readinessCompletions.clear();
    for (auto &completion : completions) {
      if (preparationReady)
        completion({});
      else
        completion(std::unexpected(*preparationError));
    }
  }

  void whenReady(ReadinessCompletion completion) {
    if (!completion)
      return;
    if (preparationReady) {
      completion({});
      return;
    }
    if (preparationError) {
      completion(std::unexpected(*preparationError));
      return;
    }
    readinessCompletions.push_back(std::move(completion));
    startPreparation();
  }

  ~Impl() {
    stopping = true;
    if (worker && thread.isRunning())
      static_cast<void>(QMetaObject::invokeMethod(
          worker, [target = worker] { target->shutdown(); },
          Qt::BlockingQueuedConnection));
    thread.quit();
    thread.wait();
    worker = nullptr;
  }

  template <typename Operation>
  bool submit(Operation operation, Completion completion) {
    if (stopping || !completion || maximumPending == 0U ||
        pending.load(std::memory_order_acquire) >= maximumPending || !worker)
      return false;
    pending.fetch_add(1U, std::memory_order_acq_rel);
    const QPointer<LocalSketchSession> lifetime{&owner};
    const auto delivered = std::make_shared<Completion>(std::move(completion));
    const bool queued = QMetaObject::invokeMethod(
        worker,
        [target = worker, lifetime, operation = std::move(operation),
         delivered]() mutable {
          auto result = std::make_shared<Result<LocalSketchProjection>>(
              operation(*target));
          if (!lifetime)
            return;
          static_cast<void>(QMetaObject::invokeMethod(
              lifetime,
              [lifetime, delivered, result = std::move(result)]() mutable {
                if (!lifetime)
                  return;
                lifetime->impl_->pending.fetch_sub(1U,
                                                   std::memory_order_acq_rel);
                (*delivered)(std::move(*result));
              },
              Qt::QueuedConnection));
        },
        Qt::QueuedConnection);
    if (!queued)
      pending.fetch_sub(1U, std::memory_order_acq_rel);
    return queued;
  }

  LocalSketchSession &owner;
  const std::size_t maximumPending;
  QThread thread;
  SessionWorker *worker;
  std::atomic_size_t pending = 0U;
  std::vector<ReadinessCompletion> readinessCompletions;
  std::optional<Diagnostic> preparationError;
  bool preparationQueued = false;
  bool preparationReady = false;
  bool stopping = false;
};

LocalSketchSession::LocalSketchSession(LocalSketchSessionConfig config,
                                       QObject *parent)
    : QObject(parent), impl_(std::make_unique<Impl>(*this, std::move(config))) {
  impl_->startPreparation();
}

LocalSketchSession::~LocalSketchSession() = default;

void LocalSketchSession::whenReady(ReadinessCompletion completion) {
  impl_->whenReady(std::move(completion));
}

bool LocalSketchSession::create(LocalSketchCreation creation,
                                Completion completion) {
  return impl_->submit(
      [creation](SessionWorker &worker) {
        return worker.createSketch(creation);
      },
      std::move(completion));
}

bool LocalSketchSession::applyRectangle(LocalRectangleGesture gesture,
                                        Completion completion) {
  return impl_->submit(
      [gesture](SessionWorker &worker) {
        return worker.applyRectangle(gesture);
      },
      std::move(completion));
}

bool LocalSketchSession::toggleConstruction(
    LocalSketchConstructionToggle toggle, Completion completion) {
  return impl_->submit(
      [toggle = std::move(toggle)](SessionWorker &worker) {
        return worker.toggleConstruction(toggle);
      },
      std::move(completion));
}

bool LocalSketchSession::dragCurve(LocalSketchCurveDrag drag,
                                   Completion completion) {
  return impl_->submit(
      [drag = std::move(drag)](SessionWorker &worker) {
        return worker.dragCurve(drag);
      },
      std::move(completion));
}

bool LocalSketchSession::replaceSource(LocalSourceReplacement replacement,
                                       Completion completion) {
  return impl_->submit(
      [replacement = std::move(replacement)](SessionWorker &worker) {
        return worker.replaceSource(replacement);
      },
      std::move(completion));
}

bool LocalSketchSession::undo(Completion completion) {
  return impl_->submit([](SessionWorker &worker) { return worker.undo(); },
                       std::move(completion));
}

bool LocalSketchSession::redo(Completion completion) {
  return impl_->submit([](SessionWorker &worker) { return worker.redo(); },
                       std::move(completion));
}

std::size_t LocalSketchSession::pendingOperationCount() const {
  return impl_->pending.load(std::memory_order_acquire);
}

} // namespace kearne::ui
