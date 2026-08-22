#include "local_sketch_session.hpp"
#include "sketch_tool_gesture.hpp"

#include <kearne/adapters/ceres_sketch_solver.hpp>
#include <kearne/adapters/occ_bspline.hpp>
#include <kearne/adapters/occ_curve_geometry.hpp>
#include <kearne/adapters/sketch_source_worker.hpp>
#include <kearne/document/canonical.hpp>
#include <kearne/document/content_store.hpp>
#include <kearne/engineering/service.hpp>
#include <kearne/sketch/modify.hpp>
#include <kearne/sketch/tools.hpp>
#include <kearne/sketch/transform.hpp>
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
#include <cmath>
#include <cstdint>
#include <limits>
#include <numbers>
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

template <typename Id> Result<std::vector<Id>> makeIdVector(std::size_t size) {
  std::vector<Id> result;
  result.reserve(size);
  for (std::size_t index = 0U; index < size; ++index) {
    auto value = makeId<Id>();
    if (!value)
      return std::unexpected(std::move(value.error()));
    result.push_back(std::move(*value));
  }
  return result;
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

Result<sketch::AngleValue> angle(double radians) {
  return sketch::AngleValue::fromSi(radians);
}

Result<sketch::DimensionlessValue> dimensionless(double value) {
  return sketch::DimensionlessValue::fromSi(value);
}

Result<sketch::PointKey> pointKey(QStringView value) {
  if (value == QStringLiteral("point"))
    return sketch::PointKey::Point;
  if (value == QStringLiteral("start"))
    return sketch::PointKey::Start;
  if (value == QStringLiteral("end"))
    return sketch::PointKey::End;
  if (value == QStringLiteral("center"))
    return sketch::PointKey::Center;
  if (value == QStringLiteral("major"))
    return sketch::PointKey::Major;
  if (value == QStringLiteral("minor"))
    return sketch::PointKey::Minor;
  if (value == QStringLiteral("focus"))
    return sketch::PointKey::Focus;
  return std::unexpected(
      diagnostic("desktop.sketch.constraint-point",
                 "Constraint needs a selected Sketch point"));
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
      state.definition.objects,
      state.definition.constraints,
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
  applyTool(const LocalSketchToolGesture &gesture) {
    if (!state)
      return std::unexpected(
          diagnostic("desktop.sketch.not-created",
                     "create a Sketch before applying a geometry tool"));
    auto projected = projectLocalSketchToolGesture(gesture, true);
    if (!projected)
      return std::unexpected(std::move(projected.error()));
    std::vector<sketch::Point2> points;
    points.reserve(gesture.points.size());
    for (const LocalSketchToolPoint &point : gesture.points) {
      auto x = length(point.xMetres);
      auto y = length(point.yMetres);
      if (!x)
        return std::unexpected(std::move(x.error()));
      if (!y)
        return std::unexpected(std::move(y.error()));
      points.push_back({*x, *y});
    }

    std::optional<sketch::ToolInput> input;
    if (isLocalSketchBSpline(gesture.kind)) {
      auto object = makeId<SketchObjectId>();
      auto entity = makeId<SketchEntityId>();
      if (!object)
        return std::unexpected(std::move(object.error()));
      if (!entity)
        return std::unexpected(std::move(entity.error()));
      auto geometry = adapters::createBSplineGeometry(
          points,
          isLocalSketchInterpolatedBSpline(gesture.kind)
              ? adapters::BSplineCreation::Interpolation
              : adapters::BSplineCreation::ControlPoints,
          gesture.degree, isLocalSketchPeriodicBSpline(gesture.kind));
      if (!geometry)
        return std::unexpected(std::move(geometry.error()));
      input.emplace(sketch::BSplineToolInput{{*object, *entity},
                                             std::move(geometry->controlPoints),
                                             std::move(geometry->knots),
                                             std::move(geometry->weights),
                                             geometry->degree,
                                             geometry->periodic,
                                             gesture.construction});
    } else if (isLocalSketchPolygon(gesture.kind)) {
      const std::size_t sideCount =
          localSketchPolygonSideCount(gesture.kind, gesture.sideCount);
      auto object = makeId<SketchObjectId>();
      auto sides = makeIdVector<SketchEntityId>(sideCount);
      auto constraints = makeIdVector<SketchConstraintId>(3U * sideCount - 2U);
      if (!object)
        return std::unexpected(std::move(object.error()));
      if (!sides)
        return std::unexpected(std::move(sides.error()));
      if (!constraints)
        return std::unexpected(std::move(constraints.error()));
      input.emplace(sketch::RegularPolygonToolInput{
          {*object, std::move(*sides), std::move(*constraints)},
          points[0],
          points[1],
          sideCount,
          gesture.construction});
    } else if (gesture.kind == LocalSketchToolKind::Polyline) {
      auto object = makeId<SketchObjectId>();
      auto segments = makeIdVector<SketchEntityId>(projected->size());
      const std::size_t constraintCount =
          gesture.closed ? projected->size() : projected->size() - 1U;
      auto constraints = makeIdVector<SketchConstraintId>(constraintCount);
      if (!object)
        return std::unexpected(std::move(object.error()));
      if (!segments)
        return std::unexpected(std::move(segments.error()));
      if (!constraints)
        return std::unexpected(std::move(constraints.error()));
      input.emplace(sketch::PolylineToolInput{
          {*object, std::move(*segments), std::move(*constraints)},
          std::move(points),
          gesture.closed,
          gesture.construction});
    } else if (gesture.kind == LocalSketchToolKind::Rectangle ||
               gesture.kind == LocalSketchToolKind::CenterRectangle) {
      auto object = makeId<SketchObjectId>();
      auto edges = makeIds<SketchEntityId, 4U>();
      auto constraints = makeIds<SketchConstraintId, 8U>();
      if (!object)
        return std::unexpected(std::move(object.error()));
      if (!edges)
        return std::unexpected(std::move(edges.error()));
      if (!constraints)
        return std::unexpected(std::move(constraints.error()));
      sketch::Point2 firstCorner = points[0];
      if (gesture.kind == LocalSketchToolKind::CenterRectangle) {
        auto firstX =
            length(2.0 * gesture.points[0].xMetres - gesture.points[1].xMetres);
        auto firstY =
            length(2.0 * gesture.points[0].yMetres - gesture.points[1].yMetres);
        if (!firstX)
          return std::unexpected(std::move(firstX.error()));
        if (!firstY)
          return std::unexpected(std::move(firstY.error()));
        firstCorner = {*firstX, *firstY};
      }
      input.emplace(sketch::RectangleToolInput{
          {*object, std::move(*edges), std::move(*constraints)},
          firstCorner,
          points[1],
          gesture.construction});
    } else if (gesture.kind == LocalSketchToolKind::Slot ||
               gesture.kind == LocalSketchToolKind::Oblong) {
      auto object = makeId<SketchObjectId>();
      auto curves = makeIds<SketchEntityId, 4U>();
      auto constraints = makeIds<SketchConstraintId, 9U>();
      if (!object)
        return std::unexpected(std::move(object.error()));
      if (!curves)
        return std::unexpected(std::move(curves.error()));
      if (!constraints)
        return std::unexpected(std::move(constraints.error()));
      auto radius = length(projected->front().radiusMetres);
      if (!radius)
        return std::unexpected(std::move(radius.error()));
      input.emplace(sketch::SlotToolInput{
          {*object, std::move(*curves), std::move(*constraints)},
          points[0],
          points[1],
          *radius,
          gesture.construction,
          gesture.kind == LocalSketchToolKind::Oblong
              ? sketch::SketchObjectKind::Oblong
              : sketch::SketchObjectKind::Slot});
    } else if (gesture.kind == LocalSketchToolKind::ArcSlot) {
      auto object = makeId<SketchObjectId>();
      auto curves = makeIds<SketchEntityId, 4U>();
      auto constraints = makeIds<SketchConstraintId, 10U>();
      if (!object)
        return std::unexpected(std::move(object.error()));
      if (!curves)
        return std::unexpected(std::move(curves.error()));
      if (!constraints)
        return std::unexpected(std::move(constraints.error()));
      const SketchPrimitiveProjection &outer = (*projected)[0];
      const SketchPrimitiveProjection &inner = (*projected)[2];
      auto centerlineRadius =
          length((outer.radiusMetres + inner.radiusMetres) / 2.0);
      auto start = angle(outer.startAngleRadians);
      auto sweep = angle(outer.sweepAngleRadians);
      auto slotRadius = length((outer.radiusMetres - inner.radiusMetres) / 2.0);
      if (!centerlineRadius)
        return std::unexpected(std::move(centerlineRadius.error()));
      if (!start)
        return std::unexpected(std::move(start.error()));
      if (!sweep)
        return std::unexpected(std::move(sweep.error()));
      if (!slotRadius)
        return std::unexpected(std::move(slotRadius.error()));
      input.emplace(sketch::ArcSlotToolInput{
          {*object, std::move(*curves), std::move(*constraints)},
          points[0],
          *centerlineRadius,
          *start,
          *sweep,
          *slotRadius,
          gesture.construction});
    } else {
      auto object = makeId<SketchObjectId>();
      auto entity = makeId<SketchEntityId>();
      if (!object)
        return std::unexpected(std::move(object.error()));
      if (!entity)
        return std::unexpected(std::move(entity.error()));
      const sketch::PrimitiveToolIds ids{*object, *entity};
      switch (gesture.kind) {
      case LocalSketchToolKind::Point:
        input.emplace(
            sketch::PointToolInput{ids, points[0], gesture.construction});
        break;
      case LocalSketchToolKind::Line:
        input.emplace(sketch::LineToolInput{ids, points[0], points[1],
                                            gesture.construction});
        break;
      case LocalSketchToolKind::Circle:
      case LocalSketchToolKind::ThreePointCircle: {
        auto radius = length(projected->front().radiusMetres);
        auto centerX = length(projected->front().points.front().xMetres);
        auto centerY = length(projected->front().points.front().yMetres);
        if (!radius)
          return std::unexpected(std::move(radius.error()));
        if (!centerX)
          return std::unexpected(std::move(centerX.error()));
        if (!centerY)
          return std::unexpected(std::move(centerY.error()));
        input.emplace(sketch::CircleToolInput{
            ids, {*centerX, *centerY}, *radius, gesture.construction});
        break;
      }
      case LocalSketchToolKind::Arc:
      case LocalSketchToolKind::ThreePointArc: {
        const SketchPrimitiveProjection &arc = projected->front();
        auto radius = length(arc.radiusMetres);
        auto start = angle(arc.startAngleRadians);
        auto end = angle(arc.startAngleRadians + arc.sweepAngleRadians);
        auto centerX = length(arc.points.front().xMetres);
        auto centerY = length(arc.points.front().yMetres);
        if (!radius)
          return std::unexpected(std::move(radius.error()));
        if (!start)
          return std::unexpected(std::move(start.error()));
        if (!end)
          return std::unexpected(std::move(end.error()));
        if (!centerX)
          return std::unexpected(std::move(centerX.error()));
        if (!centerY)
          return std::unexpected(std::move(centerY.error()));
        input.emplace(sketch::ArcToolInput{ids,
                                           {*centerX, *centerY},
                                           *radius,
                                           *start,
                                           *end,
                                           gesture.construction});
        break;
      }
      case LocalSketchToolKind::Ellipse:
      case LocalSketchToolKind::ThreePointEllipse: {
        const SketchPrimitiveProjection &ellipse = projected->front();
        auto major = length(ellipse.radiusMetres);
        auto minor = length(ellipse.secondaryRadiusMetres);
        auto rotation = angle(ellipse.rotationAngleRadians);
        auto centerX = length(ellipse.points.front().xMetres);
        auto centerY = length(ellipse.points.front().yMetres);
        if (!major || !minor || !rotation || !centerX || !centerY) {
          if (!major)
            return std::unexpected(std::move(major.error()));
          if (!minor)
            return std::unexpected(std::move(minor.error()));
          if (!rotation)
            return std::unexpected(std::move(rotation.error()));
          if (!centerX)
            return std::unexpected(std::move(centerX.error()));
          return std::unexpected(std::move(centerY.error()));
        }
        input.emplace(sketch::EllipseToolInput{ids,
                                               {*centerX, *centerY},
                                               *major,
                                               *minor,
                                               *rotation,
                                               gesture.construction});
        break;
      }
      case LocalSketchToolKind::EllipticalArc: {
        const SketchPrimitiveProjection &arc = projected->front();
        auto major = length(arc.radiusMetres);
        auto minor = length(arc.secondaryRadiusMetres);
        auto rotation = angle(arc.rotationAngleRadians);
        auto start = angle(arc.startAngleRadians);
        auto end = angle(arc.startAngleRadians + arc.sweepAngleRadians);
        auto centerX = length(arc.points.front().xMetres);
        auto centerY = length(arc.points.front().yMetres);
        if (!major || !minor || !rotation || !start || !end || !centerX ||
            !centerY) {
          if (!major)
            return std::unexpected(std::move(major.error()));
          if (!minor)
            return std::unexpected(std::move(minor.error()));
          if (!rotation)
            return std::unexpected(std::move(rotation.error()));
          if (!start)
            return std::unexpected(std::move(start.error()));
          if (!end)
            return std::unexpected(std::move(end.error()));
          if (!centerX)
            return std::unexpected(std::move(centerX.error()));
          return std::unexpected(std::move(centerY.error()));
        }
        input.emplace(sketch::EllipticalArcToolInput{ids,
                                                     {*centerX, *centerY},
                                                     *major,
                                                     *minor,
                                                     *rotation,
                                                     *start,
                                                     *end,
                                                     gesture.construction});
        break;
      }
      case LocalSketchToolKind::HyperbolicArc: {
        const SketchPrimitiveProjection &arc = projected->front();
        auto major = length(arc.radiusMetres);
        auto minor = length(arc.secondaryRadiusMetres);
        auto rotation = angle(arc.rotationAngleRadians);
        auto start = dimensionless(arc.startAngleRadians);
        auto end = dimensionless(arc.startAngleRadians + arc.sweepAngleRadians);
        auto centerX = length(arc.points.front().xMetres);
        auto centerY = length(arc.points.front().yMetres);
        if (!major || !minor || !rotation || !start || !end || !centerX ||
            !centerY) {
          if (!major)
            return std::unexpected(std::move(major.error()));
          if (!minor)
            return std::unexpected(std::move(minor.error()));
          if (!rotation)
            return std::unexpected(std::move(rotation.error()));
          if (!start)
            return std::unexpected(std::move(start.error()));
          if (!end)
            return std::unexpected(std::move(end.error()));
          if (!centerX)
            return std::unexpected(std::move(centerX.error()));
          return std::unexpected(std::move(centerY.error()));
        }
        input.emplace(sketch::HyperbolicArcToolInput{ids,
                                                     {*centerX, *centerY},
                                                     *major,
                                                     *minor,
                                                     *rotation,
                                                     *start,
                                                     *end,
                                                     gesture.construction});
        break;
      }
      case LocalSketchToolKind::ParabolicArc: {
        const SketchPrimitiveProjection &arc = projected->front();
        auto focal = length(arc.radiusMetres);
        auto rotation = angle(arc.rotationAngleRadians);
        auto start = length(arc.startAngleRadians);
        auto end = length(arc.startAngleRadians + arc.sweepAngleRadians);
        auto vertexX = length(arc.points.front().xMetres);
        auto vertexY = length(arc.points.front().yMetres);
        if (!focal || !rotation || !start || !end || !vertexX || !vertexY) {
          if (!focal)
            return std::unexpected(std::move(focal.error()));
          if (!rotation)
            return std::unexpected(std::move(rotation.error()));
          if (!start)
            return std::unexpected(std::move(start.error()));
          if (!end)
            return std::unexpected(std::move(end.error()));
          if (!vertexX)
            return std::unexpected(std::move(vertexX.error()));
          return std::unexpected(std::move(vertexY.error()));
        }
        input.emplace(sketch::ParabolicArcToolInput{ids,
                                                    {*vertexX, *vertexY},
                                                    *focal,
                                                    *rotation,
                                                    *start,
                                                    *end,
                                                    gesture.construction});
        break;
      }
      case LocalSketchToolKind::Rectangle:
      case LocalSketchToolKind::CenterRectangle:
      case LocalSketchToolKind::Polyline:
      case LocalSketchToolKind::Slot:
      case LocalSketchToolKind::ArcSlot:
      case LocalSketchToolKind::Oblong:
      case LocalSketchToolKind::Triangle:
      case LocalSketchToolKind::Square:
      case LocalSketchToolKind::Pentagon:
      case LocalSketchToolKind::Hexagon:
      case LocalSketchToolKind::Heptagon:
      case LocalSketchToolKind::Octagon:
      case LocalSketchToolKind::RegularPolygon:
      case LocalSketchToolKind::BSpline:
      case LocalSketchToolKind::PeriodicBSpline:
      case LocalSketchToolKind::InterpolatedBSpline:
      case LocalSketchToolKind::PeriodicInterpolatedBSpline:
        break;
      }
    }
    if (!input)
      return std::unexpected(diagnostic(
          "desktop.sketch.tool-kind",
          "Sketch geometry tool kind is unsupported", Severity::Fatal));
    auto operation = nextOperation();
    if (!operation)
      return std::unexpected(std::move(operation.error()));
    auto identity = nextEvaluation(operation->sourceJob);
    if (!identity)
      return std::unexpected(std::move(identity.error()));
    auto edited = workflow.applyTool(*state, *operation, *input, *identity);
    if (!edited)
      return std::unexpected(std::move(edited.error()));
    publishState(std::move(*edited));
    return projection();
  }

  Result<LocalSketchProjection>
  applyConstraint(const LocalSketchConstraintGesture &gesture) {
    if (!state)
      return std::unexpected(
          diagnostic("desktop.sketch.not-created",
                     "create a Sketch before applying a constraint"));
    auto constraintId = makeId<SketchConstraintId>();
    if (!constraintId)
      return std::unexpected(std::move(constraintId.error()));
    const auto entity = [&](std::size_t index) -> Result<SketchEntityId> {
      if (index >= gesture.selections.size())
        return std::unexpected(
            diagnostic("desktop.sketch.constraint-selection-count",
                       "Constraint received the wrong number of selections"));
      return SketchEntityId::parse(
          gesture.selections[index].entityId.toStdString());
    };
    const auto point = [&](std::size_t index) -> Result<sketch::PointRef> {
      auto selectedEntity = entity(index);
      if (!selectedEntity)
        return std::unexpected(std::move(selectedEntity.error()));
      auto selectedKey = pointKey(gesture.selections[index].pointKey);
      if (!selectedKey)
        return std::unexpected(std::move(selectedKey.error()));
      return sketch::PointRef{*selectedEntity, *selectedKey};
    };
    const LocalSketchConstraintDefinition *definition =
        localSketchConstraintDefinition(gesture.kind);
    const std::size_t minimum =
        definition == nullptr ? 0U : definition->minimumSelectionCount;
    const std::size_t maximum =
        definition == nullptr || definition->maximumSelectionCount == 0U
            ? minimum
            : definition->maximumSelectionCount;
    if (definition == nullptr || gesture.selections.size() < minimum ||
        gesture.selections.size() > maximum)
      return std::unexpected(
          diagnostic("desktop.sketch.constraint-selection-count",
                     "Constraint received the wrong number of selections"));

    if (gesture.kind == LocalSketchConstraintKind::RemoveAxisAlignment) {
      std::vector<SketchEntityId> entities;
      entities.reserve(gesture.selections.size());
      for (std::size_t index = 0U; index < gesture.selections.size(); ++index) {
        auto selected = entity(index);
        if (!selected)
          return std::unexpected(std::move(selected.error()));
        entities.push_back(*selected);
      }
      auto edited = sketch::removeAxisAlignment(state->definition, entities);
      if (!edited)
        return std::unexpected(std::move(edited.error()));
      return applyEdits(std::move(*edited));
    }

    std::optional<sketch::Constraint> constraint;
    if (gesture.kind == LocalSketchConstraintKind::Coincident) {
      auto first = point(0U);
      auto second = point(1U);
      if (!first)
        return std::unexpected(std::move(first.error()));
      if (!second)
        return std::unexpected(std::move(second.error()));
      constraint.emplace(sketch::Coincident{*constraintId, *first, *second});
    } else if (gesture.kind == LocalSketchConstraintKind::Midpoint) {
      auto selectedPoint = point(0U);
      auto line = entity(1U);
      if (!selectedPoint)
        return std::unexpected(std::move(selectedPoint.error()));
      if (!line)
        return std::unexpected(std::move(line.error()));
      constraint.emplace(
          sketch::Midpoint{*constraintId, *selectedPoint, *line});
    } else if (gesture.kind == LocalSketchConstraintKind::PointOnObject) {
      auto selectedPoint = point(0U);
      auto curve = entity(1U);
      if (!selectedPoint)
        return std::unexpected(std::move(selectedPoint.error()));
      if (!curve)
        return std::unexpected(std::move(curve.error()));
      constraint.emplace(
          sketch::PointOnObject{*constraintId, *selectedPoint, *curve});
    } else if (gesture.kind == LocalSketchConstraintKind::Symmetric) {
      auto first = point(0U);
      auto second = point(1U);
      if (!first)
        return std::unexpected(std::move(first.error()));
      if (!second)
        return std::unexpected(std::move(second.error()));
      if (gesture.selections[2].pointKey.isEmpty()) {
        auto axis = entity(2U);
        if (!axis)
          return std::unexpected(std::move(axis.error()));
        constraint.emplace(
            sketch::Symmetric{*constraintId, *first, *second, *axis});
      } else {
        auto center = point(2U);
        if (!center)
          return std::unexpected(std::move(center.error()));
        constraint.emplace(sketch::SymmetricAboutPoint{*constraintId, *first,
                                                       *second, *center});
      }
    } else if (gesture.kind == LocalSketchConstraintKind::Lock) {
      auto selected = point(0U);
      if (!selected)
        return std::unexpected(std::move(selected.error()));
      auto position = sketch::resolvePoint(state->definition, *selected);
      if (!position)
        return std::unexpected(std::move(position.error()));
      constraint.emplace(sketch::Lock{*constraintId, *selected, *position});
    } else if (gesture.kind >= LocalSketchConstraintKind::Distance &&
               gesture.kind <= LocalSketchConstraintKind::Angle) {
      if (!gesture.valueSi || !std::isfinite(*gesture.valueSi))
        return std::unexpected(
            diagnostic("desktop.sketch.dimension-value",
                       "Dimension needs a finite value with compatible units"));
      if (gesture.kind == LocalSketchConstraintKind::Angle) {
        auto first = entity(0U);
        auto second = entity(1U);
        auto value = angle(*gesture.valueSi);
        if (!first)
          return std::unexpected(std::move(first.error()));
        if (!second)
          return std::unexpected(std::move(second.error()));
        if (!value)
          return std::unexpected(std::move(value.error()));
        constraint.emplace(
            sketch::AngleBetween{*constraintId, *first, *second, *value});
      } else {
        if ((gesture.kind == LocalSketchConstraintKind::Distance &&
             *gesture.valueSi < 0.0) ||
            ((gesture.kind == LocalSketchConstraintKind::Radius ||
              gesture.kind == LocalSketchConstraintKind::Diameter) &&
             *gesture.valueSi <= 0.0))
          return std::unexpected(
              diagnostic("desktop.sketch.dimension-range",
                         "Dimension value is outside the supported range"));
        auto value = length(*gesture.valueSi);
        if (!value)
          return std::unexpected(std::move(value.error()));
        if (gesture.kind == LocalSketchConstraintKind::Radius ||
            gesture.kind == LocalSketchConstraintKind::Diameter) {
          auto curve = entity(0U);
          if (!curve)
            return std::unexpected(std::move(curve.error()));
          if (gesture.kind == LocalSketchConstraintKind::Radius)
            constraint.emplace(sketch::Radius{*constraintId, *curve, *value});
          else
            constraint.emplace(sketch::Diameter{*constraintId, *curve, *value});
        } else {
          auto linePoint =
              [&](sketch::PointKey key) -> Result<sketch::PointRef> {
            auto line = entity(0U);
            if (!line)
              return std::unexpected(std::move(line.error()));
            return sketch::PointRef{*line, key};
          };
          auto first = gesture.selections.size() == 1U
                           ? linePoint(sketch::PointKey::Start)
                           : point(0U);
          auto second = gesture.selections.size() == 1U
                            ? linePoint(sketch::PointKey::End)
                            : point(1U);
          if (!first)
            return std::unexpected(std::move(first.error()));
          if (!second)
            return std::unexpected(std::move(second.error()));
          if (gesture.kind == LocalSketchConstraintKind::Distance)
            constraint.emplace(
                sketch::Distance{*constraintId, *first, *second, *value});
          else if (gesture.kind ==
                   LocalSketchConstraintKind::HorizontalDistance)
            constraint.emplace(sketch::HorizontalDistance{*constraintId, *first,
                                                          *second, *value});
          else
            constraint.emplace(sketch::VerticalDistance{*constraintId, *first,
                                                        *second, *value});
        }
      }
    } else if (gesture.kind == LocalSketchConstraintKind::Group) {
      std::vector<SketchEntityId> entities;
      entities.reserve(gesture.selections.size());
      for (std::size_t index = 0U; index < gesture.selections.size(); ++index) {
        auto selected = entity(index);
        if (!selected)
          return std::unexpected(std::move(selected.error()));
        entities.push_back(*selected);
      }
      constraint.emplace(sketch::Group{*constraintId, std::move(entities)});
    } else if (gesture.kind == LocalSketchConstraintKind::HorizontalVertical) {
      auto selected = entity(0U);
      if (!selected)
        return std::unexpected(std::move(selected.error()));
      const auto found = std::ranges::find(state->definition.entities,
                                           *selected, sketch::entityId);
      if (found == state->definition.entities.end())
        return std::unexpected(
            diagnostic("sketch.reference.missing-entity",
                       "constraint references a missing entity"));
      const auto *line = std::get_if<sketch::LineEntity>(&*found);
      if (!line)
        return std::unexpected(
            diagnostic("sketch.constraint.requires-line",
                       "Horizontal / Vertical needs a selected line"));
      const double deltaX = line->end.x.si() - line->start.x.si();
      const double deltaY = line->end.y.si() - line->start.y.si();
      if (std::abs(deltaX) >= std::abs(deltaY))
        constraint.emplace(sketch::Horizontal{*constraintId, *selected});
      else
        constraint.emplace(sketch::Vertical{*constraintId, *selected});
    } else {
      auto first = entity(0U);
      if (!first)
        return std::unexpected(std::move(first.error()));
      if (gesture.kind == LocalSketchConstraintKind::Horizontal)
        constraint.emplace(sketch::Horizontal{*constraintId, *first});
      else if (gesture.kind == LocalSketchConstraintKind::Vertical)
        constraint.emplace(sketch::Vertical{*constraintId, *first});
      else if (gesture.kind == LocalSketchConstraintKind::Block)
        constraint.emplace(sketch::Block{*constraintId, *first});
      else {
        auto second = entity(1U);
        if (!second)
          return std::unexpected(std::move(second.error()));
        switch (gesture.kind) {
        case LocalSketchConstraintKind::Parallel:
          constraint.emplace(sketch::Parallel{*constraintId, *first, *second});
          break;
        case LocalSketchConstraintKind::Perpendicular:
          constraint.emplace(
              sketch::Perpendicular{*constraintId, *first, *second});
          break;
        case LocalSketchConstraintKind::Tangent:
          constraint.emplace(sketch::Tangent{*constraintId, *first, *second});
          break;
        case LocalSketchConstraintKind::Equal:
          constraint.emplace(sketch::Equal{*constraintId, *first, *second});
          break;
        case LocalSketchConstraintKind::Concentric:
          constraint.emplace(
              sketch::Concentric{*constraintId, *first, *second});
          break;
        case LocalSketchConstraintKind::Collinear:
          constraint.emplace(sketch::Collinear{*constraintId, *first, *second});
          break;
        case LocalSketchConstraintKind::Coincident:
        case LocalSketchConstraintKind::Horizontal:
        case LocalSketchConstraintKind::Vertical:
        case LocalSketchConstraintKind::Midpoint:
        case LocalSketchConstraintKind::Block:
        case LocalSketchConstraintKind::PointOnObject:
        case LocalSketchConstraintKind::Symmetric:
        case LocalSketchConstraintKind::Lock:
        case LocalSketchConstraintKind::Distance:
        case LocalSketchConstraintKind::HorizontalDistance:
        case LocalSketchConstraintKind::VerticalDistance:
        case LocalSketchConstraintKind::Radius:
        case LocalSketchConstraintKind::Diameter:
        case LocalSketchConstraintKind::Angle:
        case LocalSketchConstraintKind::HorizontalVertical:
        case LocalSketchConstraintKind::Group:
        case LocalSketchConstraintKind::RemoveAxisAlignment:
          break;
        }
      }
    }
    if (!constraint)
      return std::unexpected(diagnostic("desktop.sketch.constraint-kind",
                                        "Sketch constraint is unsupported",
                                        Severity::Fatal));
    const std::array edits{
        sketch::Edit{sketch::AppendConstraint{std::move(*constraint)}}};
    auto edited = sketch::applyEdits(state->definition, edits);
    if (!edited)
      return std::unexpected(std::move(edited.error()));
    return applyEdits(std::move(*edited));
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

  Result<LocalSketchProjection> editBSpline(const LocalBSplineEdit &edit) {
    if (!state)
      return std::unexpected(
          diagnostic("desktop.sketch.not-created",
                     "create a Sketch before editing its B-splines"));
    auto id = SketchEntityId::parse(edit.entityId.toStdString());
    if (!id)
      return std::unexpected(std::move(id.error()));
    const auto found =
        std::ranges::find(state->definition.entities, *id, sketch::entityId);
    if (found == state->definition.entities.end() ||
        !std::holds_alternative<sketch::BSplineEntity>(*found))
      return std::unexpected(
          diagnostic("desktop.sketch.bspline-required",
                     "Select a B-spline for this operation"));
    const auto operation = [kind = edit.kind] {
      switch (kind) {
      case LocalBSplineEditKind::IncreaseDegree:
        return adapters::BSplineEdit::IncreaseDegree;
      case LocalBSplineEditKind::DecreaseDegree:
        return adapters::BSplineEdit::DecreaseDegree;
      case LocalBSplineEditKind::IncreaseKnotMultiplicity:
        return adapters::BSplineEdit::IncreaseKnotMultiplicity;
      case LocalBSplineEditKind::DecreaseKnotMultiplicity:
        return adapters::BSplineEdit::DecreaseKnotMultiplicity;
      case LocalBSplineEditKind::InsertKnot:
        return adapters::BSplineEdit::InsertKnot;
      case LocalBSplineEditKind::SetPoleWeight:
        return adapters::BSplineEdit::SetPoleWeight;
      }
      std::unreachable();
    }();
    auto replacement = adapters::editBSpline(
        {std::get<sketch::BSplineEntity>(*found), operation, edit.index,
         edit.value, edit.maximumDeviationMetres});
    if (!replacement)
      return std::unexpected(std::move(replacement.error()));
    const std::array edits{
        sketch::Edit{sketch::ReplaceEntity{std::move(*replacement)}}};
    auto applied = sketch::applyEdits(state->definition, edits);
    if (!applied)
      return std::unexpected(std::move(applied.error()));
    return applyEdits(std::move(*applied));
  }

  Result<LocalSketchProjection> transform(const LocalSketchTransform &request) {
    if (!state)
      return std::unexpected(
          diagnostic("desktop.sketch.not-created",
                     "create a Sketch before transforming its geometry"));
    if (request.transforms.empty())
      return std::unexpected(diagnostic("desktop.sketch.transform-count",
                                        "Sketch transform count is invalid"));
    std::vector<SketchEntityId> entities;
    entities.reserve(request.entityIds.size());
    for (const QString &text : request.entityIds) {
      auto id = SketchEntityId::parse(text.toStdString());
      if (!id)
        return std::unexpected(std::move(id.error()));
      entities.push_back(*id);
    }
    const auto convert = [](const LocalSimilarityTransform &source)
        -> Result<sketch::SimilarityTransform2d> {
      auto pivotX = length(source.pivotXMetres);
      auto pivotY = length(source.pivotYMetres);
      auto translationX = length(source.translationXMetres);
      auto translationY = length(source.translationYMetres);
      if (!pivotX)
        return std::unexpected(std::move(pivotX.error()));
      if (!pivotY)
        return std::unexpected(std::move(pivotY.error()));
      if (!translationX)
        return std::unexpected(std::move(translationX.error()));
      if (!translationY)
        return std::unexpected(std::move(translationY.error()));
      return sketch::SimilarityTransform2d{{*pivotX, *pivotY},
                                           {*translationX, *translationY},
                                           source.rotationRadians,
                                           source.scale,
                                           source.reflected};
    };

    if (request.mode == LocalSketchTransformMode::Replace) {
      if (request.transforms.size() != 1U)
        return std::unexpected(diagnostic("desktop.sketch.transform-count",
                                          "Replace requires one transform"));
      auto operation = convert(request.transforms.front());
      if (!operation)
        return std::unexpected(std::move(operation.error()));
      if (request.externalConstraints !=
              LocalExternalConstraintPolicy::Refuse &&
          request.externalConstraints != LocalExternalConstraintPolicy::Detach)
        return std::unexpected(
            diagnostic("desktop.sketch.transform-constraint-policy",
                       "Sketch transform constraint policy is invalid"));
      const auto policy =
          request.externalConstraints == LocalExternalConstraintPolicy::Detach
              ? sketch::ExternalConstraintPolicy::Detach
              : sketch::ExternalConstraintPolicy::Refuse;
      auto edited = sketch::transformSelection(state->definition, entities,
                                               *operation, policy);
      if (!edited)
        return std::unexpected(std::move(edited.error()));
      return applyEdits(std::move(*edited));
    }
    if (request.mode != LocalSketchTransformMode::Copy)
      return std::unexpected(diagnostic("desktop.sketch.transform-mode",
                                        "Sketch transform mode is invalid"));
    if (request.dimensions != LocalDimensionCopyPolicy::Preserve &&
        request.dimensions != LocalDimensionCopyPolicy::Equalize)
      return std::unexpected(
          diagnostic("desktop.sketch.transform-dimension-policy",
                     "Sketch transform dimension policy is invalid"));

    std::vector<sketch::TransformCopy> copies;
    copies.reserve(request.transforms.size());
    sketch::Definition labelState = state->definition;
    for (const LocalSimilarityTransform &source : request.transforms) {
      auto operation = convert(source);
      if (!operation)
        return std::unexpected(std::move(operation.error()));
      auto requirements =
          sketch::copyRequirements(state->definition, entities, *operation);
      if (!requirements)
        return std::unexpected(std::move(requirements.error()));
      sketch::TransformCopy copy{*operation, {}, {}, {}};
      for (SketchEntityId sourceId : requirements->entities) {
        auto target = makeId<SketchEntityId>();
        if (!target)
          return std::unexpected(std::move(target.error()));
        copy.entities.push_back({sourceId, *target});
      }
      for (SketchConstraintId sourceId : requirements->constraints) {
        auto target = makeId<SketchConstraintId>();
        if (!target)
          return std::unexpected(std::move(target.error()));
        copy.constraints.push_back({sourceId, *target});
      }
      for (SketchObjectId sourceId : requirements->objects) {
        const auto found = std::ranges::find(labelState.objects, sourceId,
                                             &sketch::SketchObject::id);
        if (found == labelState.objects.end())
          return std::unexpected(
              diagnostic("desktop.sketch.transform-object",
                         "Sketch transform object is missing"));
        auto target = makeId<SketchObjectId>();
        if (!target)
          return std::unexpected(std::move(target.error()));
        std::string label = sketch::nextSketchObjectLabel(
            labelState, sketch::sketchObjectLabelPrefix(found->kind));
        copy.objects.push_back({sourceId, *target, label});
        sketch::SketchObject reserved = *found;
        reserved.id = *target;
        reserved.label = std::move(label);
        labelState.objects.push_back(std::move(reserved));
      }
      copies.push_back(std::move(copy));
    }
    const auto dimensions =
        request.dimensions == LocalDimensionCopyPolicy::Equalize
            ? sketch::DimensionCopyPolicy::Equalize
            : sketch::DimensionCopyPolicy::Preserve;
    auto edited =
        sketch::copySelection(state->definition, entities, copies, dimensions);
    if (!edited)
      return std::unexpected(std::move(edited.error()));
    return applyEdits(std::move(*edited));
  }

  Result<LocalSketchProjection> modifyCorner(const LocalCornerEdit &request) {
    if (!state)
      return std::unexpected(
          diagnostic("desktop.sketch.not-created",
                     "create a Sketch before modifying its geometry"));
    if (request.kind != LocalCornerEditKind::Fillet &&
        request.kind != LocalCornerEditKind::Chamfer)
      return std::unexpected(diagnostic("desktop.sketch.corner-kind",
                                        "Corner edit kind is invalid"));
    const auto curvePick =
        [](const LocalCurvePick &input) -> Result<sketch::CurvePick> {
      auto entity = SketchEntityId::parse(input.entityId.toStdString());
      auto x = length(input.referenceXMetres);
      auto y = length(input.referenceYMetres);
      if (!entity)
        return std::unexpected(std::move(entity.error()));
      if (!x)
        return std::unexpected(std::move(x.error()));
      if (!y)
        return std::unexpected(std::move(y.error()));
      return sketch::CurvePick{*entity, {*x, *y}};
    };
    auto first = curvePick(request.first);
    auto second = curvePick(request.second);
    auto size = length(request.sizeMetres);
    auto object = makeId<SketchObjectId>();
    auto curve = makeId<SketchEntityId>();
    if (!first)
      return std::unexpected(std::move(first.error()));
    if (!second)
      return std::unexpected(std::move(second.error()));
    if (!size)
      return std::unexpected(std::move(size.error()));
    if (!object)
      return std::unexpected(std::move(object.error()));
    if (!curve)
      return std::unexpected(std::move(curve.error()));
    if (request.constraints != LocalExternalConstraintPolicy::Refuse &&
        request.constraints != LocalExternalConstraintPolicy::Detach)
      return std::unexpected(
          diagnostic("desktop.sketch.corner-constraint-policy",
                     "Corner edit constraint policy is invalid"));
    std::vector<SketchConstraintId> generatedConstraints;
    generatedConstraints.reserve(5U);
    for (std::size_t index = 0U; index < 5U; ++index) {
      auto constraint = makeId<SketchConstraintId>();
      if (!constraint)
        return std::unexpected(std::move(constraint.error()));
      generatedConstraints.push_back(*constraint);
    }
    const std::array constraints{
        generatedConstraints[0], generatedConstraints[1],
        generatedConstraints[2], generatedConstraints[3],
        generatedConstraints[4]};
    const auto constraintPolicy =
        request.constraints == LocalExternalConstraintPolicy::Detach
            ? sketch::ExternalConstraintPolicy::Detach
            : sketch::ExternalConstraintPolicy::Refuse;
    auto edited = sketch::editLineCorner(
        state->definition, {request.kind == LocalCornerEditKind::Fillet
                                ? sketch::CornerEditKind::Fillet
                                : sketch::CornerEditKind::Chamfer,
                            *first,
                            *second,
                            *size,
                            {*object, *curve, constraints},
                            constraintPolicy});
    if (!edited)
      return std::unexpected(std::move(edited.error()));
    return applyEdits(std::move(*edited));
  }

  Result<LocalSketchProjection> offset(const LocalOffsetEdit &request) {
    if (!state)
      return std::unexpected(
          diagnostic("desktop.sketch.not-created",
                     "create a Sketch before offsetting its geometry"));
    if (request.sourceMode != LocalOffsetSourceMode::Keep &&
        request.sourceMode != LocalOffsetSourceMode::Delete)
      return std::unexpected(diagnostic("desktop.sketch.offset-mode",
                                        "Offset source mode is invalid"));
    if (request.constraints != LocalExternalConstraintPolicy::Refuse &&
        request.constraints != LocalExternalConstraintPolicy::Detach)
      return std::unexpected(
          diagnostic("desktop.sketch.offset-constraint-policy",
                     "Offset constraint policy is invalid"));
    auto distance = length(request.distanceMetres);
    if (!distance)
      return std::unexpected(std::move(distance.error()));
    const auto sourceMode = request.sourceMode == LocalOffsetSourceMode::Delete
                                ? sketch::OffsetSourceMode::Delete
                                : sketch::OffsetSourceMode::Keep;
    const auto constraintPolicy =
        request.constraints == LocalExternalConstraintPolicy::Detach
            ? sketch::ExternalConstraintPolicy::Detach
            : sketch::ExternalConstraintPolicy::Refuse;
    sketch::OffsetEdit edit{{}, *distance, sourceMode, {}, constraintPolicy};
    edit.curves.reserve(request.entityIds.size());
    edit.outputs.reserve(request.entityIds.size());
    for (const QString &text : request.entityIds) {
      auto source = SketchEntityId::parse(text.toStdString());
      auto object = makeId<SketchObjectId>();
      auto curve = makeId<SketchEntityId>();
      if (!source)
        return std::unexpected(std::move(source.error()));
      if (!object)
        return std::unexpected(std::move(object.error()));
      if (!curve)
        return std::unexpected(std::move(curve.error()));
      edit.curves.push_back(*source);
      edit.outputs.push_back({*object, *curve});
    }
    auto edited = sketch::offsetCurves(state->definition, edit);
    if (!edited)
      return std::unexpected(std::move(edited.error()));
    return applyEdits(std::move(*edited));
  }

  Result<LocalSketchProjection> extend(const LocalExtendEdit &request) {
    if (!state)
      return std::unexpected(
          diagnostic("desktop.sketch.not-created",
                     "create a Sketch before extending its geometry"));
    if (request.constraints != LocalExternalConstraintPolicy::Refuse &&
        request.constraints != LocalExternalConstraintPolicy::Detach)
      return std::unexpected(
          diagnostic("desktop.sketch.extend-constraint-policy",
                     "Extend constraint policy is invalid"));
    auto entity = SketchEntityId::parse(request.curve.entityId.toStdString());
    auto referenceX = length(request.curve.referenceXMetres);
    auto referenceY = length(request.curve.referenceYMetres);
    auto targetX = length(request.targetXMetres);
    auto targetY = length(request.targetYMetres);
    if (!entity)
      return std::unexpected(std::move(entity.error()));
    if (!referenceX)
      return std::unexpected(std::move(referenceX.error()));
    if (!referenceY)
      return std::unexpected(std::move(referenceY.error()));
    if (!targetX)
      return std::unexpected(std::move(targetX.error()));
    if (!targetY)
      return std::unexpected(std::move(targetY.error()));
    const auto constraintPolicy =
        request.constraints == LocalExternalConstraintPolicy::Detach
            ? sketch::ExternalConstraintPolicy::Detach
            : sketch::ExternalConstraintPolicy::Refuse;
    auto edited = sketch::extendCurve(state->definition,
                                      {{*entity, {*referenceX, *referenceY}},
                                       {*targetX, *targetY},
                                       constraintPolicy});
    if (!edited)
      return std::unexpected(std::move(edited.error()));
    return applyEdits(std::move(*edited));
  }

  Result<LocalSketchProjection> trim(const LocalTrimEdit &request) {
    if (!state)
      return std::unexpected(
          diagnostic("desktop.sketch.not-created",
                     "create a Sketch before trimming its geometry"));
    if (request.constraints != LocalExternalConstraintPolicy::Refuse &&
        request.constraints != LocalExternalConstraintPolicy::Detach)
      return std::unexpected(
          diagnostic("desktop.sketch.trim-constraint-policy",
                     "Trim constraint policy is invalid"));
    auto entity = SketchEntityId::parse(request.curve.entityId.toStdString());
    auto referenceX = length(request.curve.referenceXMetres);
    auto referenceY = length(request.curve.referenceYMetres);
    auto split = makeId<SketchEntityId>();
    auto firstBoundary = makeId<SketchConstraintId>();
    auto secondBoundary = makeId<SketchConstraintId>();
    if (!entity)
      return std::unexpected(std::move(entity.error()));
    if (!referenceX)
      return std::unexpected(std::move(referenceX.error()));
    if (!referenceY)
      return std::unexpected(std::move(referenceY.error()));
    if (!split)
      return std::unexpected(std::move(split.error()));
    if (!firstBoundary)
      return std::unexpected(std::move(firstBoundary.error()));
    if (!secondBoundary)
      return std::unexpected(std::move(secondBoundary.error()));
    const auto constraintPolicy =
        request.constraints == LocalExternalConstraintPolicy::Detach
            ? sketch::ExternalConstraintPolicy::Detach
            : sketch::ExternalConstraintPolicy::Refuse;
    auto edited = adapters::trimCurve(
        state->definition,
        {{*entity, {*referenceX, *referenceY}},
         {*split, {*firstBoundary, *secondBoundary}}, constraintPolicy});
    if (!edited)
      return std::unexpected(std::move(edited.error()));
    return applyEdits(std::move(*edited));
  }

  Result<LocalTrimPreview> previewTrim(const LocalCurvePick &request) const {
    if (!state)
      return std::unexpected(
          diagnostic("desktop.sketch.not-created",
                     "create a Sketch before previewing Trim"));
    auto entity = SketchEntityId::parse(request.entityId.toStdString());
    auto referenceX = length(request.referenceXMetres);
    auto referenceY = length(request.referenceYMetres);
    if (!entity)
      return std::unexpected(std::move(entity.error()));
    if (!referenceX)
      return std::unexpected(std::move(referenceX.error()));
    if (!referenceY)
      return std::unexpected(std::move(referenceY.error()));
    auto preview = adapters::previewTrim(
        state->definition, {*entity, {*referenceX, *referenceY}});
    if (!preview)
      return std::unexpected(std::move(preview.error()));
    LocalTrimPreview result;
    result.deletesCurve = preview->deletesCurve;
    result.boundaries.reserve(preview->boundaries.size());
    for (const adapters::TrimBoundary &boundary : preview->boundaries)
      result.boundaries.push_back(
          {boundary.point.x.si(), boundary.point.y.si()});
    return result;
  }

  Result<LocalSketchProjection> split(const LocalSplitEdit &request) {
    if (!state)
      return std::unexpected(
          diagnostic("desktop.sketch.not-created",
                     "create a Sketch before splitting its geometry"));
    if (request.constraints != LocalExternalConstraintPolicy::Refuse &&
        request.constraints != LocalExternalConstraintPolicy::Detach)
      return std::unexpected(
          diagnostic("desktop.sketch.split-constraint-policy",
                     "Split constraint policy is invalid"));
    auto entity = SketchEntityId::parse(request.curve.entityId.toStdString());
    auto referenceX = length(request.curve.referenceXMetres);
    auto referenceY = length(request.curve.referenceYMetres);
    auto second = makeId<SketchEntityId>();
    auto seam = makeId<SketchConstraintId>();
    if (!entity)
      return std::unexpected(std::move(entity.error()));
    if (!referenceX)
      return std::unexpected(std::move(referenceX.error()));
    if (!referenceY)
      return std::unexpected(std::move(referenceY.error()));
    if (!second)
      return std::unexpected(std::move(second.error()));
    if (!seam)
      return std::unexpected(std::move(seam.error()));
    const auto constraintPolicy =
        request.constraints == LocalExternalConstraintPolicy::Detach
            ? sketch::ExternalConstraintPolicy::Detach
            : sketch::ExternalConstraintPolicy::Refuse;
    auto edited = adapters::splitCurve(
        state->definition,
        {{*entity, {*referenceX, *referenceY}}, {*second, *seam},
         constraintPolicy});
    if (!edited)
      return std::unexpected(std::move(edited.error()));
    return applyEdits(std::move(*edited));
  }

  Result<LocalSplitPreview> previewSplit(const LocalCurvePick &request) const {
    if (!state)
      return std::unexpected(
          diagnostic("desktop.sketch.not-created",
                     "create a Sketch before previewing Split"));
    auto entity = SketchEntityId::parse(request.entityId.toStdString());
    auto referenceX = length(request.referenceXMetres);
    auto referenceY = length(request.referenceYMetres);
    if (!entity)
      return std::unexpected(std::move(entity.error()));
    if (!referenceX)
      return std::unexpected(std::move(referenceX.error()));
    if (!referenceY)
      return std::unexpected(std::move(referenceY.error()));
    auto preview = adapters::previewSplit(
        state->definition, {*entity, {*referenceX, *referenceY}});
    if (!preview)
      return std::unexpected(std::move(preview.error()));
    return LocalSplitPreview{
        {preview->point.x.si(), preview->point.y.si()}};
  }

  Result<LocalSketchProjection> join(const LocalJoinEdit &request) {
    if (!state)
      return std::unexpected(
          diagnostic("desktop.sketch.not-created",
                     "create a Sketch before joining its geometry"));
    if (request.constraints != LocalExternalConstraintPolicy::Refuse &&
        request.constraints != LocalExternalConstraintPolicy::Detach)
      return std::unexpected(
          diagnostic("desktop.sketch.join-constraint-policy",
                     "Join constraint policy is invalid"));
    auto firstEntity =
        SketchEntityId::parse(request.first.entityId.toStdString());
    auto firstPoint = pointKey(request.first.pointKey);
    auto object = makeId<SketchObjectId>();
    if (!firstEntity)
      return std::unexpected(std::move(firstEntity.error()));
    if (!firstPoint)
      return std::unexpected(std::move(firstPoint.error()));
    if (!object)
      return std::unexpected(std::move(object.error()));
    const sketch::NumericalProfile profile;
    const sketch::PointRef firstReference{*firstEntity, *firstPoint};
    std::optional<sketch::PointRef> secondReference;
    if (!request.second.entityId.isEmpty()) {
      auto secondEntity =
          SketchEntityId::parse(request.second.entityId.toStdString());
      auto secondPoint = pointKey(request.second.pointKey);
      if (!secondEntity)
        return std::unexpected(std::move(secondEntity.error()));
      if (!secondPoint)
        return std::unexpected(std::move(secondPoint.error()));
      secondReference = sketch::PointRef{*secondEntity, *secondPoint};
    } else {
      auto selected = sketch::resolvePoint(state->definition, firstReference);
      if (!selected)
        return std::unexpected(std::move(selected.error()));
      for (const sketch::Entity &candidate : state->definition.entities) {
        const SketchEntityId candidateId = sketch::entityId(candidate);
        if (candidateId == firstReference.entity)
          continue;
        for (const sketch::PointKey key :
             {sketch::PointKey::Start, sketch::PointKey::End}) {
          auto point =
              sketch::resolvePoint(state->definition, {candidateId, key});
          if (point &&
              std::hypot(point->x.si() - selected->x.si(),
                         point->y.si() - selected->y.si()) <=
                  profile.lengthToleranceMeters) {
            if (secondReference)
              return std::unexpected(diagnostic(
                  "desktop.sketch.join-ambiguous",
                  "Shared endpoint belongs to more than two curves"));
            secondReference = sketch::PointRef{candidateId, key};
          }
        }
      }
      if (!secondReference)
        return std::unexpected(diagnostic(
            "desktop.sketch.join-disconnected",
            "Shared endpoint does not meet another open curve"));
    }
    const auto constraintPolicy =
        request.constraints == LocalExternalConstraintPolicy::Detach
            ? sketch::ExternalConstraintPolicy::Detach
            : sketch::ExternalConstraintPolicy::Refuse;
    auto edited = adapters::joinCurves(
        state->definition,
        {firstReference, *secondReference, *object, constraintPolicy}, profile);
    if (!edited)
      return std::unexpected(std::move(edited.error()));
    return applyEdits(std::move(*edited));
  }

  Result<LocalSketchProjection>
  convertToNurbs(const LocalConvertToNurbsEdit &request) {
    if (!state)
      return std::unexpected(diagnostic(
          "desktop.sketch.not-created",
          "create a Sketch before converting its geometry"));
    if (request.constraints != LocalExternalConstraintPolicy::Refuse &&
        request.constraints != LocalExternalConstraintPolicy::Detach)
      return std::unexpected(diagnostic(
          "desktop.sketch.convert-to-nurbs-constraint-policy",
          "Convert to NURBS constraint policy is invalid"));
    auto entity = SketchEntityId::parse(request.entityId.toStdString());
    if (!entity)
      return std::unexpected(std::move(entity.error()));
    const auto constraintPolicy =
        request.constraints == LocalExternalConstraintPolicy::Detach
            ? sketch::ExternalConstraintPolicy::Detach
            : sketch::ExternalConstraintPolicy::Refuse;
    auto edited = adapters::convertToNurbs(
        state->definition, {*entity, constraintPolicy});
    if (!edited)
      return std::unexpected(std::move(edited.error()));
    return applyEdits(std::move(*edited));
  }

  Result<sketch::AppliedEdits>
  curveDragEdits(const LocalSketchCurveDrag &drag) const {
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
    return edited;
  }

  Result<std::shared_ptr<const render::SketchSceneSnapshot>>
  previewCurveDrag(const LocalSketchCurveDrag &drag) {
    auto edited = curveDragEdits(drag);
    if (!edited)
      return std::unexpected(std::move(edited.error()));
    if (!state || !state->evaluation ||
        !state->evaluation->replacementScene)
      return std::unexpected(diagnostic(
          "desktop.sketch.preview-scene-missing",
          "Sketch drag preview needs the current evaluated scene"));
    if (sceneGeneration == std::numeric_limits<std::uint64_t>::max())
      return std::unexpected(diagnostic(
          "desktop.sketch.scene-generation-exhausted",
          "local Sketch scene generation is exhausted", Severity::Fatal));
    auto generation = render::SceneGeneration::create(++sceneGeneration);
    auto job = makeId<JobId>();
    if (!generation)
      return std::unexpected(std::move(generation.error()));
    if (!job)
      return std::unexpected(std::move(job.error()));
    auto digest = operationDigest<render::SceneDigest>(
        "kearne.desktop.sketch-drag-preview.v1", *job);
    if (!digest)
      return std::unexpected(std::move(digest.error()));
    auto scene = render::projectSketchScene(
        {state->evaluation->replacementScene->stamp().target, *generation,
         *digest},
        edited->target.entities);
    if (!scene)
      return std::unexpected(std::move(scene.error()));
    return std::make_shared<const render::SketchSceneSnapshot>(
        std::move(*scene));
  }

  Result<LocalSketchProjection> dragCurve(const LocalSketchCurveDrag &drag) {
    auto edited = curveDragEdits(drag);
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

struct LocalTrimPreviewRequest {
  LocalCurvePick curve;
};

struct LocalSplitPreviewRequest {
  LocalCurvePick curve;
};

using LocalPreviewRequest = std::variant<LocalSketchCurveDrag,
                                         LocalTrimPreviewRequest,
                                         LocalSplitPreviewRequest>;
using LocalPreviewResult = std::variant<
    std::shared_ptr<const render::SketchSceneSnapshot>, LocalTrimPreview,
    LocalSplitPreview>;
using LocalPreviewCompletion =
    std::function<void(Result<LocalPreviewResult> preview)>;

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
  applyTool(const LocalSketchToolGesture &gesture) {
    if (auto ready = ensureBackend(); !ready)
      return std::unexpected(std::move(ready.error()));
    return backend_->applyTool(gesture);
  }

  Result<LocalSketchProjection>
  applyConstraint(const LocalSketchConstraintGesture &gesture) {
    if (auto ready = ensureBackend(); !ready)
      return std::unexpected(std::move(ready.error()));
    return backend_->applyConstraint(gesture);
  }

  Result<LocalSketchProjection>
  toggleConstruction(const LocalSketchConstructionToggle &toggle) {
    if (auto ready = ensureBackend(); !ready)
      return std::unexpected(std::move(ready.error()));
    return backend_->toggleConstruction(toggle);
  }

  Result<LocalSketchProjection> editBSpline(const LocalBSplineEdit &edit) {
    if (auto ready = ensureBackend(); !ready)
      return std::unexpected(std::move(ready.error()));
    return backend_->editBSpline(edit);
  }

  Result<LocalSketchProjection>
  transform(const LocalSketchTransform &transform) {
    if (auto ready = ensureBackend(); !ready)
      return std::unexpected(std::move(ready.error()));
    return backend_->transform(transform);
  }

  Result<LocalSketchProjection> modifyCorner(const LocalCornerEdit &edit) {
    if (auto ready = ensureBackend(); !ready)
      return std::unexpected(std::move(ready.error()));
    return backend_->modifyCorner(edit);
  }

  Result<LocalSketchProjection> offset(const LocalOffsetEdit &edit) {
    if (auto ready = ensureBackend(); !ready)
      return std::unexpected(std::move(ready.error()));
    return backend_->offset(edit);
  }

  Result<LocalSketchProjection> extend(const LocalExtendEdit &edit) {
    if (auto ready = ensureBackend(); !ready)
      return std::unexpected(std::move(ready.error()));
    return backend_->extend(edit);
  }

  Result<LocalSketchProjection> trim(const LocalTrimEdit &edit) {
    if (auto ready = ensureBackend(); !ready)
      return std::unexpected(std::move(ready.error()));
    return backend_->trim(edit);
  }

  Result<LocalSketchProjection> split(const LocalSplitEdit &edit) {
    if (auto ready = ensureBackend(); !ready)
      return std::unexpected(std::move(ready.error()));
    return backend_->split(edit);
  }

  Result<LocalSketchProjection> join(const LocalJoinEdit &edit) {
    if (auto ready = ensureBackend(); !ready)
      return std::unexpected(std::move(ready.error()));
    return backend_->join(edit);
  }

  Result<LocalSketchProjection>
  convertToNurbs(const LocalConvertToNurbsEdit &edit) {
    if (auto ready = ensureBackend(); !ready)
      return std::unexpected(std::move(ready.error()));
    return backend_->convertToNurbs(edit);
  }

  Result<LocalSketchProjection> dragCurve(const LocalSketchCurveDrag &drag) {
    if (auto ready = ensureBackend(); !ready)
      return std::unexpected(std::move(ready.error()));
    return backend_->dragCurve(drag);
  }

  Result<LocalPreviewResult> preview(const LocalPreviewRequest &request) {
    if (auto ready = ensureBackend(); !ready)
      return std::unexpected(std::move(ready.error()));
    if (const auto *drag = std::get_if<LocalSketchCurveDrag>(&request)) {
      auto result = backend_->previewCurveDrag(*drag);
      if (!result)
        return std::unexpected(std::move(result.error()));
      return LocalPreviewResult{std::move(*result)};
    }
    if (const auto *trim = std::get_if<LocalTrimPreviewRequest>(&request)) {
      auto result = backend_->previewTrim(trim->curve);
      if (!result)
        return std::unexpected(std::move(result.error()));
      return LocalPreviewResult{std::move(*result)};
    }
    auto result =
        backend_->previewSplit(std::get<LocalSplitPreviewRequest>(request).curve);
    if (!result)
      return std::unexpected(std::move(result.error()));
    return LocalPreviewResult{std::move(*result)};
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

  struct PendingPreview {
    LocalPreviewRequest request;
    LocalPreviewCompletion completion;
    std::uint64_t sequence = 0U;
  };

  bool preview(LocalPreviewRequest request, LocalPreviewCompletion completion) {
    if (stopping || !worker || !completion ||
        previewSequence == std::numeric_limits<std::uint64_t>::max())
      return false;
    latestPreview = PendingPreview{std::move(request), std::move(completion),
                                   ++previewSequence};
    if (!previewRunning)
      dispatchPreview();
    return true;
  }

  void cancelPreview() {
    latestPreview.reset();
    if (previewSequence != std::numeric_limits<std::uint64_t>::max())
      ++previewSequence;
  }

  void dispatchPreview() {
    if (previewRunning || !latestPreview || stopping || !worker)
      return;
    PendingPreview request = std::move(*latestPreview);
    latestPreview.reset();
    previewRunning = true;
    LocalPreviewCompletion dispatchFailure = request.completion;
    const QPointer<LocalSketchSession> lifetime{&owner};
    const bool queued = QMetaObject::invokeMethod(
        worker,
        [target = worker, lifetime, request = std::move(request)]() mutable {
          auto result = std::make_shared<Result<LocalPreviewResult>>(
              target->preview(request.request));
          if (!lifetime)
            return;
          static_cast<void>(QMetaObject::invokeMethod(
              lifetime,
              [lifetime, sequence = request.sequence,
               completion = std::move(request.completion),
               result = std::move(result)]() mutable {
                if (lifetime)
                  lifetime->impl_->finishPreview(sequence,
                                                  std::move(completion),
                                                  std::move(*result));
              },
              Qt::QueuedConnection));
        },
        Qt::QueuedConnection);
    if (!queued) {
      previewRunning = false;
      dispatchFailure(std::unexpected(diagnostic(
          "desktop.sketch.preview-dispatch",
          "Sketch preview could not be dispatched")));
    }
  }

  void finishPreview(std::uint64_t sequence,
                     LocalPreviewCompletion completion,
                     Result<LocalPreviewResult> result) {
    previewRunning = false;
    if (sequence == previewSequence && !latestPreview)
      completion(std::move(result));
    if (latestPreview)
      dispatchPreview();
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
  std::optional<PendingPreview> latestPreview;
  std::optional<Diagnostic> preparationError;
  std::uint64_t previewSequence = 0U;
  bool preparationQueued = false;
  bool preparationReady = false;
  bool previewRunning = false;
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

bool LocalSketchSession::applyTool(LocalSketchToolGesture gesture,
                                   Completion completion) {
  return impl_->submit(
      [gesture = std::move(gesture)](SessionWorker &worker) {
        return worker.applyTool(gesture);
      },
      std::move(completion));
}

bool LocalSketchSession::applyConstraint(LocalSketchConstraintGesture gesture,
                                         Completion completion) {
  return impl_->submit(
      [gesture = std::move(gesture)](SessionWorker &worker) {
        return worker.applyConstraint(gesture);
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

bool LocalSketchSession::editBSpline(LocalBSplineEdit edit,
                                     Completion completion) {
  return impl_->submit(
      [edit = std::move(edit)](SessionWorker &worker) {
        return worker.editBSpline(edit);
      },
      std::move(completion));
}

bool LocalSketchSession::transform(LocalSketchTransform transform,
                                   Completion completion) {
  return impl_->submit(
      [transform = std::move(transform)](SessionWorker &worker) {
        return worker.transform(transform);
      },
      std::move(completion));
}

bool LocalSketchSession::modifyCorner(LocalCornerEdit edit,
                                      Completion completion) {
  return impl_->submit(
      [edit = std::move(edit)](SessionWorker &worker) {
        return worker.modifyCorner(edit);
      },
      std::move(completion));
}

bool LocalSketchSession::offset(LocalOffsetEdit edit, Completion completion) {
  return impl_->submit(
      [edit = std::move(edit)](SessionWorker &worker) {
        return worker.offset(edit);
      },
      std::move(completion));
}

bool LocalSketchSession::extend(LocalExtendEdit edit, Completion completion) {
  return impl_->submit(
      [edit = std::move(edit)](SessionWorker &worker) {
        return worker.extend(edit);
      },
      std::move(completion));
}

bool LocalSketchSession::trim(LocalTrimEdit edit, Completion completion) {
  return impl_->submit(
      [edit = std::move(edit)](SessionWorker &worker) {
        return worker.trim(edit);
      },
      std::move(completion));
}

bool LocalSketchSession::split(LocalSplitEdit edit, Completion completion) {
  return impl_->submit(
      [edit = std::move(edit)](SessionWorker &worker) {
        return worker.split(edit);
      },
      std::move(completion));
}

bool LocalSketchSession::join(LocalJoinEdit edit, Completion completion) {
  return impl_->submit(
      [edit = std::move(edit)](SessionWorker &worker) {
        return worker.join(edit);
      },
      std::move(completion));
}

bool LocalSketchSession::convertToNurbs(LocalConvertToNurbsEdit edit,
                                        Completion completion) {
  return impl_->submit(
      [edit = std::move(edit)](SessionWorker &worker) {
        return worker.convertToNurbs(edit);
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

bool LocalSketchSession::previewCurveDrag(
    LocalSketchCurveDrag drag, CurvePreviewCompletion completion) {
  if (!completion)
    return false;
  return impl_->preview(
      std::move(drag),
      [completion = std::move(completion)](
          Result<LocalPreviewResult> result) mutable {
        if (!result) {
          completion(std::unexpected(std::move(result.error())));
          return;
        }
        auto *scene = std::get_if<
            std::shared_ptr<const render::SketchSceneSnapshot>>(&*result);
        if (!scene) {
          completion(std::unexpected(diagnostic(
              "desktop.sketch.preview-kind",
              "Sketch preview returned the wrong result kind")));
          return;
        }
        completion(std::move(*scene));
      });
}

bool LocalSketchSession::previewTrim(LocalCurvePick curve,
                                     TrimPreviewCompletion completion) {
  if (!completion)
    return false;
  return impl_->preview(
      LocalTrimPreviewRequest{std::move(curve)},
      [completion = std::move(completion)](
          Result<LocalPreviewResult> result) mutable {
        if (!result) {
          completion(std::unexpected(std::move(result.error())));
          return;
        }
        auto *preview = std::get_if<LocalTrimPreview>(&*result);
        if (!preview) {
          completion(std::unexpected(diagnostic(
              "desktop.sketch.preview-kind",
              "Sketch preview returned the wrong result kind")));
          return;
        }
        completion(std::move(*preview));
      });
}

bool LocalSketchSession::previewSplit(LocalCurvePick curve,
                                      SplitPreviewCompletion completion) {
  if (!completion)
    return false;
  return impl_->preview(
      LocalSplitPreviewRequest{std::move(curve)},
      [completion = std::move(completion)](
          Result<LocalPreviewResult> result) mutable {
        if (!result) {
          completion(std::unexpected(std::move(result.error())));
          return;
        }
        auto *preview = std::get_if<LocalSplitPreview>(&*result);
        if (!preview) {
          completion(std::unexpected(diagnostic(
              "desktop.sketch.preview-kind",
              "Sketch preview returned the wrong result kind")));
          return;
        }
        completion(std::move(*preview));
      });
}

void LocalSketchSession::cancelPreview() {
  impl_->cancelPreview();
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
