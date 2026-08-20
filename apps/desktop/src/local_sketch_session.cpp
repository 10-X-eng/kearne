#include "local_sketch_session.hpp"

#include <kearne/adapters/ceres_sketch_solver.hpp>
#include <kearne/adapters/sketch_source_worker.hpp>
#include <kearne/document/canonical.hpp>
#include <kearne/document/content_store.hpp>
#include <kearne/engineering/service.hpp>
#include <kearne/sketch/tools.hpp>
#include <kearne/sketch_workflow/workflow.hpp>

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
frontendProjection(const sketch_workflow::SketchState &state) {
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
      solveStatus(state.evaluation->solve.status),
      static_cast<int>(state.evaluation->solve.degreesOfFreedom),
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
      return std::unique_ptr<Backend>(new Backend{
          {*project, *actor, *permission},
          *binding,
          std::move(content),
          std::move(*engineering),
          std::move(process),
          *worker,
      });
    } catch (const std::bad_alloc &) {
      return std::unexpected(
          diagnostic("desktop.sketch.session-allocation",
                     "local Sketch session allocation failed"));
    }
  }

  Result<LocalSketchProjection> createSketch() {
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
    state = std::move(*created);
    return frontendProjection(*state);
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
    state = std::move(*edited);
    return frontendProjection(*state);
  }

  void shutdown() { sourceEditor.stop(); }

private:
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
  std::optional<sketch_workflow::SketchState> state;
  std::uint64_t sceneGeneration = 0U;
};

class SessionWorker final : public QObject {
public:
  explicit SessionWorker(adapters::FramedWorkerProcessConfig process)
      : process_(std::move(process)) {}

  Result<LocalSketchProjection> createSketch() {
    if (auto ready = ensureBackend(); !ready)
      return std::unexpected(std::move(ready.error()));
    return backend_->createSketch();
  }

  Result<LocalSketchProjection>
  applyRectangle(const LocalRectangleGesture &gesture) {
    if (auto ready = ensureBackend(); !ready)
      return std::unexpected(std::move(ready.error()));
    return backend_->applyRectangle(gesture);
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
  bool stopping = false;
};

LocalSketchSession::LocalSketchSession(LocalSketchSessionConfig config,
                                       QObject *parent)
    : QObject(parent), impl_(std::make_unique<Impl>(*this, std::move(config))) {
}

LocalSketchSession::~LocalSketchSession() = default;

bool LocalSketchSession::create(Completion completion) {
  return impl_->submit(
      [](SessionWorker &worker) { return worker.createSketch(); },
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

std::size_t LocalSketchSession::pendingOperationCount() const {
  return impl_->pending.load(std::memory_order_acquire);
}

} // namespace kearne::ui
