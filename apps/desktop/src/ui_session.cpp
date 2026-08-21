#include "ui_session.hpp"

#include "display_units.hpp"

#include <QMetaObject>
#include <QPointer>
#include <QVariantMap>

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
#include <utility>

namespace kearne::ui {
namespace {

using Record = QVariantMap;

template <typename Values, typename Convert>
QVariantList recordsFor(const Values &values, Convert convert) {
  QVariantList result;
  result.reserve(static_cast<qsizetype>(values.size()));
  for (const auto &value : values)
    result.push_back(convert(value));
  return result;
}

QVariantList optionRecords(const std::vector<UiOption> &options) {
  QVariantList result;
  result.reserve(static_cast<qsizetype>(options.size()));
  for (const UiOption &option : options) {
    result.push_back(Record{{QStringLiteral("id"), option.id},
                            {QStringLiteral("label"), option.label},
                            {QStringLiteral("symbol"), option.symbol}});
  }
  return result;
}

QString preferenceKindName(PreferenceKind kind) {
  switch (kind) {
  case PreferenceKind::Choice:
    return QStringLiteral("choice");
  case PreferenceKind::Toggle:
    return QStringLiteral("toggle");
  case PreferenceKind::Text:
    return QStringLiteral("text");
  }
  return {};
}

QString fieldKindName(FieldKind kind) {
  switch (kind) {
  case FieldKind::Choice:
    return QStringLiteral("choice");
  case FieldKind::Reference:
    return QStringLiteral("reference");
  case FieldKind::Expression:
    return QStringLiteral("expression");
  case FieldKind::Text:
    return QStringLiteral("text");
  case FieldKind::Toggle:
    return QStringLiteral("toggle");
  }
  return {};
}

QString commandDraftStateName(CommandDraftState state) {
  switch (state) {
  case CommandDraftState::None:
    return QStringLiteral("none");
  case CommandDraftState::Editing:
    return QStringLiteral("editing");
  case CommandDraftState::Pending:
    return QStringLiteral("pending");
  case CommandDraftState::Preview:
    return QStringLiteral("preview");
  case CommandDraftState::Stale:
    return QStringLiteral("stale");
  case CommandDraftState::Unavailable:
    return QStringLiteral("unavailable");
  case CommandDraftState::Rejected:
    return QStringLiteral("rejected");
  }
  return {};
}

QString sketchInputKindName(SketchInputKind kind) {
  switch (kind) {
  case SketchInputKind::None:
    return QStringLiteral("none");
  case SketchInputKind::PlanePoint:
    return QStringLiteral("plane-point");
  case SketchInputKind::Entity:
    return QStringLiteral("entity");
  }
  return {};
}

QString sketchPrimitiveKindName(SketchPrimitiveKind kind) {
  switch (kind) {
  case SketchPrimitiveKind::Point:
    return QStringLiteral("point");
  case SketchPrimitiveKind::Line:
    return QStringLiteral("line");
  case SketchPrimitiveKind::Circle:
    return QStringLiteral("circle");
  case SketchPrimitiveKind::Arc:
    return QStringLiteral("arc");
  case SketchPrimitiveKind::Ellipse:
    return QStringLiteral("ellipse");
  case SketchPrimitiveKind::EllipticalArc:
    return QStringLiteral("elliptical-arc");
  case SketchPrimitiveKind::HyperbolicArc:
    return QStringLiteral("hyperbolic-arc");
  case SketchPrimitiveKind::ParabolicArc:
    return QStringLiteral("parabolic-arc");
  case SketchPrimitiveKind::BSpline:
    return QStringLiteral("bspline");
  }
  return {};
}

QString sketchSelectionKindName(SketchSelectionKind kind) {
  switch (kind) {
  case SketchSelectionKind::Any:
    return QStringLiteral("any");
  case SketchSelectionKind::Point:
    return QStringLiteral("point");
  case SketchSelectionKind::Curve:
    return QStringLiteral("curve");
  }
  return {};
}

QStringList optionValues(const std::vector<UiOption> &options, bool labels) {
  QStringList result;
  result.reserve(static_cast<qsizetype>(options.size()));
  for (const UiOption &option : options)
    result.push_back(labels ? option.label : option.id);
  return result;
}

QVariantList
preferenceCategoryRecords(const std::vector<PreferenceCategory> &categories) {
  QVariantList result;
  result.reserve(static_cast<qsizetype>(categories.size()));
  for (const PreferenceCategory &category : categories) {
    result.push_back(Record{{QStringLiteral("id"), category.id},
                            {QStringLiteral("label"), category.label},
                            {QStringLiteral("icon"), category.icon}});
  }
  return result;
}

QVariantList
preferenceRecords(const std::vector<PreferenceDescriptor> &preferences) {
  QVariantList result;
  result.reserve(static_cast<qsizetype>(preferences.size()));
  for (const PreferenceDescriptor &preference : preferences) {
    const QVariant value = std::holds_alternative<bool>(preference.value)
                               ? QVariant(std::get<bool>(preference.value))
                               : QVariant(std::get<QString>(preference.value));
    result.push_back(Record{
        {QStringLiteral("id"), preference.id},
        {QStringLiteral("categoryId"), preference.categoryId},
        {QStringLiteral("label"), preference.label},
        {QStringLiteral("detail"), preference.detail},
        {QStringLiteral("kind"), preferenceKindName(preference.kind)},
        {QStringLiteral("value"), value},
        {QStringLiteral("options"), optionValues(preference.options, true)},
        {QStringLiteral("optionIds"), optionValues(preference.options, false)},
        {QStringLiteral("enabled"), preference.enabled}});
  }
  return result;
}

QVariantList fieldRecords(const std::vector<FieldDescriptor> &fields) {
  return recordsFor(fields, [](const FieldDescriptor &field) {
    const QVariant value = std::holds_alternative<bool>(field.value)
                               ? QVariant(std::get<bool>(field.value))
                               : QVariant(std::get<QString>(field.value));
    return Record{
        {QStringLiteral("id"), field.id},
        {QStringLiteral("label"), field.label},
        {QStringLiteral("kind"), fieldKindName(field.kind)},
        {QStringLiteral("value"), value},
        {QStringLiteral("effectiveValue"), field.effectiveValue},
        {QStringLiteral("options"), optionValues(field.options, true)},
        {QStringLiteral("optionIds"), optionValues(field.options, false)},
        {QStringLiteral("readOnly"), field.readOnly}};
  });
}

QVariantList commandRecords(const std::vector<CommandDescriptor> &commands) {
  return recordsFor(commands, [](const CommandDescriptor &command) {
    return Record{
        {QStringLiteral("id"), command.id},
        {QStringLiteral("label"), command.label},
        {QStringLiteral("icon"), command.icon},
        {QStringLiteral("group"), command.group},
        {QStringLiteral("menu"), command.menu},
        {QStringLiteral("workspaceId"), command.workspaceId},
        {QStringLiteral("shortcut"), command.shortcut},
        {QStringLiteral("primary"), command.primary},
        {QStringLiteral("available"), command.available},
        {QStringLiteral("unavailableReason"), command.unavailableReason}};
  });
}

QVariantList
workspaceRecords(const std::vector<WorkspaceDescriptor> &workspaces) {
  return recordsFor(workspaces, [](const WorkspaceDescriptor &workspace) {
    return Record{{QStringLiteral("id"), workspace.id},
                  {QStringLiteral("label"), workspace.label},
                  {QStringLiteral("icon"), workspace.icon}};
  });
}

QVariantList structureRecords(const std::vector<StructureItem> &items) {
  return recordsFor(items, [](const StructureItem &item) {
    return Record{{QStringLiteral("id"), item.id},
                  {QStringLiteral("label"), item.label},
                  {QStringLiteral("depth"), item.depth},
                  {QStringLiteral("kind"), item.kind}};
  });
}

QVariantList revisionRecords(const std::vector<RevisionSummary> &revisions) {
  return recordsFor(revisions, [](const RevisionSummary &revision) {
    return Record{{QStringLiteral("id"), revision.id},
                  {QStringLiteral("label"), revision.label},
                  {QStringLiteral("detail"), revision.detail}};
  });
}

QVariantList parameterRecords(const std::vector<ParameterSummary> &parameters) {
  return recordsFor(parameters, [](const ParameterSummary &parameter) {
    return Record{{QStringLiteral("id"), parameter.id},
                  {QStringLiteral("name"), parameter.name},
                  {QStringLiteral("expression"), parameter.expression},
                  {QStringLiteral("value"), parameter.value}};
  });
}

QVariantList jobRecords(const std::vector<JobSummary> &jobs) {
  return recordsFor(jobs, [](const JobSummary &job) {
    return Record{{QStringLiteral("id"), job.id},
                  {QStringLiteral("label"), job.label},
                  {QStringLiteral("state"), job.state},
                  {QStringLiteral("progress"), job.progress}};
  });
}

QVariantList
diagnosticRecords(const std::vector<DiagnosticSummary> &diagnostics) {
  return recordsFor(diagnostics, [](const DiagnosticSummary &diagnostic) {
    return Record{{QStringLiteral("id"), diagnostic.id},
                  {QStringLiteral("severity"), diagnostic.severity},
                  {QStringLiteral("summary"), diagnostic.summary}};
  });
}

QVariantList proposalRecords(const std::vector<ProposalSummary> &proposals) {
  return recordsFor(proposals, [](const ProposalSummary &proposal) {
    return Record{{QStringLiteral("id"), proposal.id},
                  {QStringLiteral("summary"), proposal.summary},
                  {QStringLiteral("state"), proposal.state}};
  });
}

QVariantList projectRecords(const std::vector<ProjectSummary> &projects) {
  return recordsFor(projects, [](const ProjectSummary &project) {
    return Record{{QStringLiteral("id"), project.id},
                  {QStringLiteral("name"), project.name},
                  {QStringLiteral("detail"), project.detail},
                  {QStringLiteral("modified"), project.modified},
                  {QStringLiteral("icon"), project.icon},
                  {QStringLiteral("workspaceId"), project.workspaceId}};
  });
}

QVariantList
templateRecords(const std::vector<ProjectTemplateDescriptor> &templates) {
  return recordsFor(templates, [](const ProjectTemplateDescriptor &project) {
    return Record{{QStringLiteral("id"), project.id},
                  {QStringLiteral("name"), project.name},
                  {QStringLiteral("detail"), project.detail},
                  {QStringLiteral("icon"), project.icon},
                  {QStringLiteral("workspaceId"), project.workspaceId}};
  });
}

QVariantList recoveryRecords(const std::vector<RecoverySummary> &items) {
  return recordsFor(items, [](const RecoverySummary &item) {
    return Record{{QStringLiteral("id"), item.id},
                  {QStringLiteral("name"), item.name},
                  {QStringLiteral("detail"), item.detail},
                  {QStringLiteral("state"), item.state},
                  {QStringLiteral("available"), item.available}};
  });
}

QVariantList operationRecords(const std::vector<OperationSummary> &operations) {
  return recordsFor(operations, [](const OperationSummary &operation) {
    return Record{{QStringLiteral("id"), operation.id},
                  {QStringLiteral("name"), operation.name},
                  {QStringLiteral("kind"), operation.kind},
                  {QStringLiteral("state"), operation.state},
                  {QStringLiteral("detail"), operation.detail},
                  {QStringLiteral("progress"), operation.progress}};
  });
}

QVariantList
interfaceStateRecords(const std::vector<InterfaceStateDescriptor> &states) {
  return recordsFor(states, [](const InterfaceStateDescriptor &state) {
    return Record{{QStringLiteral("id"), state.id},
                  {QStringLiteral("label"), state.label},
                  {QStringLiteral("icon"), state.icon}};
  });
}

QVariantList
functionPortRecords(const std::vector<FunctionPortSummary> &ports) {
  return recordsFor(ports, [](const FunctionPortSummary &port) {
    return Record{{QStringLiteral("id"), port.id},
                  {QStringLiteral("label"), port.label},
                  {QStringLiteral("type"), port.type},
                  {QStringLiteral("value"), port.value},
                  {QStringLiteral("state"), port.state}};
  });
}

QVariantMap functionRecord(const FunctionSummary &function) {
  return Record{
      {QStringLiteral("id"), function.id},
      {QStringLiteral("name"), function.name},
      {QStringLiteral("signature"), function.signature},
      {QStringLiteral("sourcePath"), function.sourcePath},
      {QStringLiteral("language"), function.language},
      {QStringLiteral("recognition"), function.recognition},
      {QStringLiteral("revision"), function.revision},
      {QStringLiteral("inputs"), functionPortRecords(function.inputs)},
      {QStringLiteral("outputs"), functionPortRecords(function.outputs)}};
}

QVariantList sketchPrimitiveRecords(
    std::span<const SketchPrimitiveProjection> primitives) {
  return recordsFor(primitives, [](const SketchPrimitiveProjection &primitive) {
    QVariantList points;
    points.reserve(static_cast<qsizetype>(primitive.points.size()));
    for (std::size_t index = 0; index < primitive.points.size(); ++index) {
      const PlanePoint &point = primitive.points[index];
      const QString key = index < primitive.pointKeys.size()
                              ? primitive.pointKeys[index]
                              : QString{};
      points.push_back(
          Record{{QStringLiteral("x"), millimetersFromMetres(point.xMetres)},
                 {QStringLiteral("y"), millimetersFromMetres(point.yMetres)},
                 {QStringLiteral("key"), key},
                 {QStringLiteral("selected"),
                  std::ranges::find(primitive.selectedPointKeys, key) !=
                      primitive.selectedPointKeys.end()}});
    }
    return Record{
        {QStringLiteral("id"), primitive.id},
        {QStringLiteral("kind"), sketchPrimitiveKindName(primitive.kind)},
        {QStringLiteral("points"), points},
        {QStringLiteral("radius"),
         millimetersFromMetres(primitive.radiusMetres)},
        {QStringLiteral("construction"), primitive.construction},
        {QStringLiteral("selected"), primitive.selected},
        {QStringLiteral("draft"), primitive.draft},
        {QStringLiteral("startAngleRadians"), primitive.startAngleRadians},
        {QStringLiteral("sweepAngleRadians"), primitive.sweepAngleRadians},
        {QStringLiteral("secondaryRadius"),
         millimetersFromMetres(primitive.secondaryRadiusMetres)},
        {QStringLiteral("rotationAngleRadians"),
         primitive.rotationAngleRadians}};
  });
}

constexpr std::uint32_t projectProjection = 1U << 0U;
constexpr std::uint32_t navigationProjection = 1U << 1U;
constexpr std::uint32_t commandProjection = 1U << 2U;
constexpr std::uint32_t sketchProjection = 1U << 3U;
constexpr std::uint32_t catalogProjection = 1U << 4U;
constexpr std::uint32_t activityProjection = 1U << 5U;
constexpr std::uint32_t hubProjection = 1U << 6U;
constexpr std::uint32_t commandCatalogProjection = 1U << 7U;
constexpr std::uint32_t commandFieldsProjection = 1U << 8U;
constexpr std::uint32_t commandListProjection = 1U << 9U;
constexpr std::uint32_t allProjections =
    projectProjection | navigationProjection | commandProjection |
    sketchProjection | catalogProjection | activityProjection | hubProjection |
    commandCatalogProjection | commandFieldsProjection | commandListProjection;

std::uint32_t changedProjections(const FrontendSnapshot &previous,
                                 const FrontendSnapshot &next) {
  std::uint32_t changed = 0;
  if (previous.projectName != next.projectName ||
      previous.branchLabel != next.branchLabel ||
      previous.revisionLabel != next.revisionLabel ||
      previous.projectRevision != next.projectRevision ||
      previous.modelHealth != next.modelHealth ||
      previous.selectionSummary != next.selectionSummary ||
      previous.modelSource != next.modelSource ||
      previous.selectedFunction != next.selectedFunction ||
      previous.sourceEditingAvailable != next.sourceEditingAvailable ||
      previous.canUndo != next.canUndo || previous.canRedo != next.canRedo ||
      previous.structure != next.structure ||
      previous.revisions != next.revisions ||
      previous.historyCommands != next.historyCommands ||
      previous.parameters != next.parameters)
    changed |= projectProjection;
  if (previous.activeWorkspaceId != next.activeWorkspaceId)
    changed |= navigationProjection;
  if (previous.activeCommandId != next.activeCommandId ||
      previous.commandDraft != next.commandDraft ||
      previous.inspectorTitle != next.inspectorTitle ||
      previous.inspectorStatus != next.inspectorStatus ||
      previous.viewportHeadline != next.viewportHeadline ||
      previous.viewportDetail != next.viewportDetail)
    changed |= commandProjection;
  if (previous.commands != next.commands)
    changed |= commandListProjection;
  if (previous.commandCatalog != next.commandCatalog)
    changed |= commandCatalogProjection;
  if (previous.fields != next.fields)
    changed |= commandFieldsProjection;
  if (previous.viewportState != next.viewportState ||
      previous.sketchEditing != next.sketchEditing ||
      previous.gridPlaneLabel != next.gridPlaneLabel ||
      previous.gridSpacingLabel != next.gridSpacingLabel ||
      previous.gridSpacingMillimeters != next.gridSpacingMillimeters ||
      previous.sketchProjection != next.sketchProjection ||
      previous.sketchInteraction != next.sketchInteraction ||
      previous.selectedSketchEntityIds != next.selectedSketchEntityIds ||
      previous.sketchScene != next.sketchScene)
    changed |= sketchProjection;
  if (previous.defaultLengthUnitId != next.defaultLengthUnitId ||
      previous.projectLengthUnitId != next.projectLengthUnitId ||
      previous.interfaceDensityId != next.interfaceDensityId ||
      previous.backendConnected != next.backendConnected ||
      previous.projectPersistenceAvailable !=
          next.projectPersistenceAvailable ||
      previous.lengthUnits != next.lengthUnits ||
      previous.preferenceCategories != next.preferenceCategories ||
      previous.preferences != next.preferences ||
      previous.workspaces != next.workspaces ||
      previous.interfaceStates != next.interfaceStates)
    changed |= catalogProjection;
  if (previous.agentStatus != next.agentStatus || previous.jobs != next.jobs ||
      previous.diagnostics != next.diagnostics ||
      previous.proposals != next.proposals ||
      previous.operations != next.operations)
    changed |= activityProjection;
  if (previous.recentProjects != next.recentProjects ||
      previous.projectTemplates != next.projectTemplates ||
      previous.recoveryItems != next.recoveryItems)
    changed |= hubProjection;
  return changed;
}

} // namespace

UiSession::UiSession(std::unique_ptr<FrontendController> controller,
                     QObject *parent)
    : QObject(parent), controller_(std::move(controller)) {
  QObject::connect(&gesturePreview_, &SketchGesturePreview::previewChanged,
                   this, &UiSession::sketchGesturePreviewChanged);
  const QPointer<UiSession> lifetime{this};
  controller_->setChangeHandler([lifetime] {
    if (!lifetime)
      return;
    static_cast<void>(QMetaObject::invokeMethod(
        lifetime,
        [lifetime] {
          if (lifetime)
            lifetime->refresh();
        },
        Qt::QueuedConnection));
  });
  refresh();
}

UiSession::~UiSession() { controller_->setChangeHandler({}); }

qulonglong UiSession::generation() const { return generation_; }
QString UiSession::projectName() const { return snapshot_->projectName; }
QString UiSession::branchLabel() const { return snapshot_->branchLabel; }
QString UiSession::revisionLabel() const { return snapshot_->revisionLabel; }
QString UiSession::projectRevision() const {
  return snapshot_->projectRevision;
}
QString UiSession::activeWorkspaceId() const {
  return snapshot_->activeWorkspaceId;
}
QString UiSession::activeCommandId() const {
  return snapshot_->activeCommandId;
}
bool UiSession::sketchEditing() const { return snapshot_->sketchEditing; }
QString UiSession::commandDraftState() const {
  return commandDraftStateName(snapshot_->commandDraft.state);
}
QString UiSession::commandDraftBaseRevision() const {
  return snapshot_->commandDraft.baseRevision;
}
bool UiSession::commandPreviewSupported() const {
  return snapshot_->commandDraft.previewSupported;
}
bool UiSession::commandApplySupported() const {
  return snapshot_->commandDraft.applySupported;
}
QString UiSession::activeSurfaceId() const { return activeSurfaceId_; }
QString UiSession::settingsCategoryId() const { return settingsCategoryId_; }
int UiSession::inspectorPage() const { return inspectorPage_; }
QString UiSession::viewportState() const { return snapshot_->viewportState; }
QString UiSession::inspectorTitle() const { return snapshot_->inspectorTitle; }
QString UiSession::inspectorStatus() const {
  return snapshot_->inspectorStatus;
}
QString UiSession::viewportHeadline() const {
  return snapshot_->viewportHeadline;
}
QString UiSession::viewportDetail() const { return snapshot_->viewportDetail; }
QString UiSession::modelHealth() const { return snapshot_->modelHealth; }
QString UiSession::selectionSummary() const {
  return snapshot_->selectionSummary;
}
QString UiSession::agentStatus() const { return snapshot_->agentStatus; }
QString UiSession::modelSource() const { return snapshot_->modelSource; }
QVariantMap UiSession::selectedFunction() const {
  return functionRecord(snapshot_->selectedFunction);
}
QString UiSession::defaultLengthUnitId() const {
  return snapshot_->defaultLengthUnitId;
}
QString UiSession::projectLengthUnitId() const {
  return snapshot_->projectLengthUnitId;
}
QString UiSession::interfaceDensityId() const {
  return snapshot_->interfaceDensityId;
}
QString UiSession::gridPlaneLabel() const { return snapshot_->gridPlaneLabel; }
QString UiSession::gridSpacingLabel() const {
  return snapshot_->gridSpacingLabel;
}
qreal UiSession::gridSpacingMillimeters() const {
  return snapshot_->gridSpacingMillimeters;
}
QString UiSession::sketchSolveStatus() const {
  return snapshot_->sketchProjection.solveStatus;
}
int UiSession::sketchDegreesOfFreedom() const {
  return snapshot_->sketchProjection.degreesOfFreedom;
}
QVariantList UiSession::sketchPrimitives() const {
  return sketchPrimitiveRecords(snapshot_->sketchProjection.primitives);
}
QString UiSession::sketchInputKind() const {
  return sketchInputKindName(snapshot_->sketchInteraction.inputKind);
}
QString UiSession::sketchSelectionKind() const {
  const auto &interaction = snapshot_->sketchInteraction;
  if (interaction.inputKind != SketchInputKind::Entity ||
      interaction.selectionSequence.empty())
    return QStringLiteral("any");
  const std::size_t index =
      std::min(static_cast<std::size_t>(std::max(0, interaction.inputCount)),
               interaction.selectionSequence.size() - 1);
  return sketchSelectionKindName(interaction.selectionSequence[index]);
}
int UiSession::sketchMinimumInputCount() const {
  return snapshot_->sketchInteraction.minimumInputCount;
}
int UiSession::sketchMaximumInputCount() const {
  return snapshot_->sketchInteraction.maximumInputCount;
}
int UiSession::sketchInputCount() const {
  return snapshot_->sketchInteraction.inputCount;
}
QString UiSession::sketchInputPrompt() const {
  return snapshot_->sketchInteraction.prompt;
}

QString UiSession::sketchHoveredEntityId() const {
  return sketchHoveredEntityId_;
}

QString UiSession::sketchHoveredPointKey() const {
  return sketchHoveredPointKey_;
}
bool UiSession::backendConnected() const { return snapshot_->backendConnected; }
bool UiSession::projectPersistenceAvailable() const {
  return snapshot_->projectPersistenceAvailable;
}
bool UiSession::sourceEditingAvailable() const {
  return snapshot_->sourceEditingAvailable;
}
bool UiSession::canUndo() const { return snapshot_->canUndo; }
bool UiSession::canRedo() const { return snapshot_->canRedo; }
bool UiSession::sketchGesturePreviewVisible() const {
  return gesturePreview_.visible();
}
QVariantList UiSession::sketchPreviewMeasurements() const {
  QVariantList result;
  const auto measurements = gesturePreview_.measurements();
  result.reserve(static_cast<qsizetype>(measurements.size()));
  for (const SketchPreviewMeasurement &measurement : measurements) {
    const QString value =
        measurement.quantity == SketchPreviewQuantity::Length
            ? formatDisplayedLength(
                  millimetersFromMetres(measurement.valueSi),
                  snapshot_->projectLengthUnitId)
            : QStringLiteral("%1°").arg(
                  measurement.valueSi * 180.0 / std::numbers::pi, 0, 'f', 1);
    result.push_back(Record{
        {QStringLiteral("label"), measurement.prefix + value},
        {QStringLiteral("anchor"), measurement.anchorMillimeters},
        {QStringLiteral("origin"), measurement.originMillimeters},
    });
  }
  return result;
}
QVariantList UiSession::sketchPreviewPrimitives() const {
  return sketchPrimitiveRecords(gesturePreview_.primitives());
}
QVariantList UiSession::lengthUnits() const {
  return optionRecords(snapshot_->lengthUnits);
}
QVariantList UiSession::preferenceCategories() const {
  return preferenceCategoryRecords(snapshot_->preferenceCategories);
}
QVariantList UiSession::preferences() const {
  return preferenceRecords(snapshot_->preferences);
}
QVariantList UiSession::workspaces() const {
  return workspaceRecords(snapshot_->workspaces);
}
QVariantList UiSession::commands() const {
  return commandRecords(snapshot_->commands);
}
QVariantList UiSession::commandCatalog() const {
  return commandRecords(snapshot_->commandCatalog);
}
QVariantList UiSession::structure() const {
  return structureRecords(snapshot_->structure);
}
QVariantList UiSession::revisions() const {
  return revisionRecords(snapshot_->revisions);
}
QVariantList UiSession::historyCommands() const {
  return commandRecords(snapshot_->historyCommands);
}
QVariantList UiSession::fields() const {
  return fieldRecords(snapshot_->fields);
}
QVariantList UiSession::parameters() const {
  return parameterRecords(snapshot_->parameters);
}
QVariantList UiSession::jobs() const { return jobRecords(snapshot_->jobs); }
QVariantList UiSession::diagnostics() const {
  return diagnosticRecords(snapshot_->diagnostics);
}
QVariantList UiSession::proposals() const {
  return proposalRecords(snapshot_->proposals);
}
QVariantList UiSession::recentProjects() const {
  return projectRecords(snapshot_->recentProjects);
}
QVariantList UiSession::projectTemplates() const {
  return templateRecords(snapshot_->projectTemplates);
}
QVariantList UiSession::recoveryItems() const {
  return recoveryRecords(snapshot_->recoveryItems);
}
QVariantList UiSession::operations() const {
  return operationRecords(snapshot_->operations);
}
QVariantList UiSession::interfaceStates() const {
  return interfaceStateRecords(snapshot_->interfaceStates);
}

std::shared_ptr<const render::SketchSceneSnapshot>
UiSession::sketchScene() const {
  return snapshot_->sketchScene;
}

void UiSession::navigateTo(const QString &surfaceId) {
  static const QStringList surfaces{
      QStringLiteral("projects"), QStringLiteral("editor"),
      QStringLiteral("settings"), QStringLiteral("recovery"),
      QStringLiteral("operations")};
  if (!surfaces.contains(surfaceId) || activeSurfaceId_ == surfaceId)
    return;
  activeSurfaceId_ = surfaceId;
  ++generation_;
  queueProjectionNotification(navigationProjection);
}

void UiSession::selectSettingsCategory(const QString &categoryId) {
  const bool exists =
      std::any_of(snapshot_->preferenceCategories.cbegin(),
                  snapshot_->preferenceCategories.cend(),
                  [&categoryId](const PreferenceCategory &category) {
                    return category.id == categoryId;
                  });
  if (!exists || settingsCategoryId_ == categoryId)
    return;
  settingsCategoryId_ = categoryId;
  ++generation_;
  queueProjectionNotification(navigationProjection);
}

void UiSession::selectInspectorPage(const QString &pageId) {
  const int page = pageId == QStringLiteral("source") ? 1 : 0;
  if (page == inspectorPage_)
    return;
  inspectorPage_ = page;
  ++generation_;
  queueProjectionNotification(navigationProjection);
}

void UiSession::selectWorkspace(const QString &workspaceId) {
  controller_->selectWorkspace(workspaceId);
  refresh();
}

void UiSession::openProject(const QString &workspaceId,
                            const QString &commandId) {
  controller_->selectWorkspace(workspaceId);
  controller_->requestCommand(commandId);
  const bool surfaceChanged = activeSurfaceId_ != QStringLiteral("editor");
  activeSurfaceId_ = QStringLiteral("editor");
  refresh();
  if (surfaceChanged)
    queueProjectionNotification(navigationProjection);
  emit commandRequested(commandId, generation());
}

void UiSession::selectEntity(const QString &entityId) {
  controller_->selectEntity(entityId);
  refresh();
}

void UiSession::requestCommand(const QString &commandId) {
  if (commandId == QStringLiteral("version.undo")) {
    static_cast<void>(undo());
    return;
  }
  if (commandId == QStringLiteral("version.redo")) {
    static_cast<void>(redo());
    return;
  }
  controller_->requestCommand(commandId);
  refresh();
  emit commandRequested(commandId, generation());
}

void UiSession::setPreference(const QString &preferenceId,
                              const QVariant &value) {
  const auto found = std::find_if(
      snapshot_->preferences.cbegin(), snapshot_->preferences.cend(),
      [&preferenceId](const PreferenceDescriptor &preference) {
        return preference.id == preferenceId;
      });
  if (found == snapshot_->preferences.cend())
    return;
  const PreferenceValue normalized = found->kind == PreferenceKind::Toggle
                                         ? PreferenceValue{value.toBool()}
                                         : PreferenceValue{value.toString()};
  if (found->value == normalized)
    return;
  controller_->setPreference(preferenceId, normalized);
  refresh();
  const auto updated = std::find_if(
      snapshot_->preferences.cbegin(), snapshot_->preferences.cend(),
      [&preferenceId](const PreferenceDescriptor &preference) {
        return preference.id == preferenceId;
      });
  if (updated != snapshot_->preferences.cend() && updated->value == normalized)
    emit preferenceChanged(preferenceId, value);
}

void UiSession::replacePreferenceOptions(const QString &preferenceId,
                                         std::vector<UiOption> options,
                                         const QString &value) {
  controller_->replacePreferenceOptions(preferenceId, std::move(options),
                                        value);
  refresh();
}

void UiSession::editField(const QString &fieldId, const QVariant &value) {
  const auto field =
      std::find_if(snapshot_->fields.cbegin(), snapshot_->fields.cend(),
                   [&fieldId](const FieldDescriptor &candidate) {
                     return candidate.id == fieldId;
                   });
  if (field == snapshot_->fields.cend())
    return;
  const FieldValue normalized = field->kind == FieldKind::Toggle
                                    ? FieldValue{value.toBool()}
                                    : FieldValue{value.toString()};
  controller_->editField(fieldId, normalized);
  refresh();
}

bool UiSession::submitActiveCommand(bool preview) {
  if (snapshot_->activeCommandId.isEmpty())
    return false;
  CommandDraftRequest request{
      snapshot_->activeCommandId,
      snapshot_->commandDraft.baseRevision,
      {},
  };
  request.fields.reserve(snapshot_->fields.size());
  for (const FieldDescriptor &field : snapshot_->fields)
    request.fields.push_back({field.id, field.value});
  const CommandDraftMode mode =
      preview ? CommandDraftMode::Preview : CommandDraftMode::Apply;
  const bool accepted = controller_->submitCommandDraft(request, mode);
  refresh();
  if (accepted)
    emit commandRequested(request.commandId, generation());
  return accepted;
}

bool UiSession::submitSketchPoint(qreal xMillimeters, qreal yMillimeters) {
  gesturePreview_.clear();
  const SketchInputRequest request{
      snapshot_->activeCommandId,
      snapshot_->sketchInteraction.expectedRevision,
      SketchInputKind::PlanePoint,
      {metresFromMillimeters(xMillimeters),
       metresFromMillimeters(yMillimeters)},
      {},
      {},
  };
  const bool accepted = controller_->submitSketchInput(request);
  refresh();
  return accepted;
}

bool UiSession::removeLastSketchInput() {
  gesturePreview_.clear();
  const bool removed = controller_->removeLastSketchInput();
  refresh();
  return removed;
}

bool UiSession::submitSketchDrag(qreal firstXMillimeters,
                                 qreal firstYMillimeters,
                                 qreal oppositeXMillimeters,
                                 qreal oppositeYMillimeters) {
  gesturePreview_.clear();
  if (snapshot_->sketchInteraction.inputKind != SketchInputKind::PlanePoint ||
      snapshot_->sketchInteraction.inputCount != 0 ||
      snapshot_->sketchInteraction.maximumInputCount == 1)
    return false;
  if (!submitSketchPoint(firstXMillimeters, firstYMillimeters))
    return false;
  return submitSketchPoint(oppositeXMillimeters, oppositeYMillimeters);
}

bool UiSession::previewSketchDrag(qreal firstXMillimeters,
                                  qreal firstYMillimeters,
                                  qreal oppositeXMillimeters,
                                  qreal oppositeYMillimeters) {
  if (snapshot_->sketchInteraction.inputKind != SketchInputKind::PlanePoint ||
      snapshot_->sketchInteraction.inputCount != 0 ||
      snapshot_->sketchInteraction.maximumInputCount == 1 ||
      snapshot_->commandDraft.state != CommandDraftState::Editing)
    return false;
  const std::array points{
      QPointF{firstXMillimeters, firstYMillimeters},
      QPointF{oppositeXMillimeters, oppositeYMillimeters},
  };
  return previewSketchGesture(points);
}

bool UiSession::previewSketchPoint(qreal xMillimeters, qreal yMillimeters) {
  const auto &interaction = snapshot_->sketchInteraction;
  if (interaction.inputKind != SketchInputKind::PlanePoint ||
      snapshot_->commandDraft.state != CommandDraftState::Editing ||
      (interaction.maximumInputCount > 0 &&
       interaction.inputCount >= interaction.maximumInputCount))
    return false;
  if (snapshot_->sketchInputPlanePoints.empty() &&
      snapshot_->activeCommandId != QStringLiteral("sketch.point")) {
    gesturePreview_.clear();
    return false;
  }
  std::vector<QPointF> points;
  points.reserve(snapshot_->sketchInputPlanePoints.size() + 1U);
  for (PlanePoint point : snapshot_->sketchInputPlanePoints)
    points.push_back({millimetersFromMetres(point.xMetres),
                      millimetersFromMetres(point.yMetres)});
  points.push_back({xMillimeters, yMillimeters});
  return previewSketchGesture(points);
}

bool UiSession::previewSketchGesture(
    std::span<const QPointF> pointsMillimeters) {
  const auto construction =
      std::ranges::find_if(snapshot_->fields, [](const FieldDescriptor &field) {
        return field.id.endsWith(QStringLiteral(".construction"));
      });
  const bool enabled = construction != snapshot_->fields.end() &&
                       std::holds_alternative<bool>(construction->value) &&
                       std::get<bool>(construction->value);
  const auto method =
      std::ranges::find_if(snapshot_->fields, [](const FieldDescriptor &field) {
        return field.id.endsWith(QStringLiteral(".method"));
      });
  const QString methodId =
      method != snapshot_->fields.end() &&
              std::holds_alternative<QString>(method->value)
          ? std::get<QString>(method->value)
          : QString{};
  const auto toggle = [this](QStringView suffix) {
    const auto field = std::ranges::find_if(
        snapshot_->fields, [suffix](const FieldDescriptor &candidate) {
          return candidate.id.endsWith(suffix);
        });
    return field != snapshot_->fields.end() &&
           std::holds_alternative<bool>(field->value) &&
           std::get<bool>(field->value);
  };
  const auto unsignedField = [this](QStringView suffix,
                                    std::size_t fallback) {
    const auto field = std::ranges::find_if(
        snapshot_->fields, [suffix](const FieldDescriptor &candidate) {
          return candidate.id.endsWith(suffix);
        });
    if (field == snapshot_->fields.end() ||
        !std::holds_alternative<QString>(field->value))
      return fallback;
    bool valid = false;
    const qulonglong value =
        std::get<QString>(field->value).toULongLong(&valid);
    return valid ? static_cast<std::size_t>(value) : fallback;
  };
  return gesturePreview_.updateGesture(
      snapshot_->activeCommandId, pointsMillimeters, enabled, methodId,
      toggle(QStringLiteral(".close-profile")),
      unsignedField(QStringLiteral(".sides"), 0U),
      static_cast<std::uint32_t>(
          unsignedField(QStringLiteral(".degree"), 3U)));
}

QString UiSession::formatProjectLength(qreal lengthMillimeters) const {
  return formatDisplayedLength(lengthMillimeters,
                               snapshot_->projectLengthUnitId);
}

void UiSession::clearSketchGesturePreview() { gesturePreview_.clear(); }

bool UiSession::submitSketchEntity(const QString &entityId,
                                   const QString &subElementKey) {
  const SketchInputRequest request{
      snapshot_->activeCommandId,
      snapshot_->sketchInteraction.expectedRevision,
      SketchInputKind::Entity,
      {},
      entityId,
      subElementKey,
  };
  const bool accepted = controller_->submitSketchInput(request);
  refresh();
  return accepted;
}

bool UiSession::submitSketchPointerClick(qreal itemX, qreal itemY) {
  cancelSketchCurveDrag();
  if (!sketchPickHandler_ || !snapshot_->sketchEditing)
    return false;
  const SketchSelectionKind targets =
      snapshot_->sketchInteraction.inputKind == SketchInputKind::Entity &&
              !snapshot_->sketchInteraction.selectionSequence.empty()
          ? snapshot_->sketchInteraction.selectionSequence[std::min(
                static_cast<std::size_t>(
                    snapshot_->sketchInteraction.inputCount),
                snapshot_->sketchInteraction.selectionSequence.size() - 1U)]
          : SketchSelectionKind::Any;
  const auto picked = sketchPickHandler_(QPointF{itemX, itemY}, 8.0, targets);
  if (!picked)
    return false;
  if (snapshot_->sketchInteraction.inputKind == SketchInputKind::Entity) {
    const SketchInputRequest request{
        snapshot_->activeCommandId,
        snapshot_->sketchInteraction.expectedRevision,
        SketchInputKind::Entity,
        {metresFromMillimeters(picked->closestPointMillimeters.x()),
         metresFromMillimeters(picked->closestPointMillimeters.y())},
        picked->entityId,
        picked->pointKey,
    };
    const bool accepted = controller_->submitSketchInput(request);
    refresh();
    return accepted;
  }
  controller_->selectEntity(picked->entityId);
  refresh();
  return true;
}

bool UiSession::updateSketchPointerHover(qreal itemX, qreal itemY) {
  if (!sketchPickHandler_ || !sketchHoverHandler_ || !snapshot_->sketchEditing)
    return false;
  const SketchSelectionKind targets =
      snapshot_->sketchInteraction.inputKind == SketchInputKind::Entity &&
              !snapshot_->sketchInteraction.selectionSequence.empty()
          ? snapshot_->sketchInteraction.selectionSequence[std::min(
                static_cast<std::size_t>(
                    snapshot_->sketchInteraction.inputCount),
                snapshot_->sketchInteraction.selectionSequence.size() - 1U)]
          : SketchSelectionKind::Any;
  auto picked = sketchPickHandler_(QPointF{itemX, itemY}, 8.0, targets);
  const bool hit = picked.has_value();
  const QString entity = picked ? picked->entityId : QString{};
  const QString point = picked ? picked->pointKey : QString{};
  if (entity != sketchHoveredEntityId_ || point != sketchHoveredPointKey_) {
    sketchHoveredEntityId_ = entity;
    sketchHoveredPointKey_ = point;
    emit sketchHoverChanged();
  }
  sketchHoverHandler_(std::move(picked));
  return hit;
}

void UiSession::clearSketchPointerHover() {
  if (!sketchHoveredEntityId_.isEmpty() || !sketchHoveredPointKey_.isEmpty()) {
    sketchHoveredEntityId_.clear();
    sketchHoveredPointKey_.clear();
    emit sketchHoverChanged();
  }
  if (sketchHoverHandler_)
    sketchHoverHandler_(std::nullopt);
}

bool UiSession::beginSketchCurveDrag(qreal itemPressX, qreal itemPressY) {
  cancelSketchCurveDrag();
  if (!sketchPickHandler_ || !snapshot_->sketchEditing ||
      !snapshot_->activeCommandId.isEmpty())
    return false;
  const auto picked = sketchPickHandler_(QPointF{itemPressX, itemPressY}, 8.0,
                                         SketchSelectionKind::Curve);
  if (!picked)
    return false;
  sketchCurveDragTarget_ = SketchCurveDragTarget{
      picked->entityId,
      {metresFromMillimeters(picked->closestPointMillimeters.x()),
       metresFromMillimeters(picked->closestPointMillimeters.y())}};
  return true;
}

bool UiSession::previewSketchCurveDrag(qreal currentXMillimeters,
                                       qreal currentYMillimeters) {
  if (!sketchCurveDragTarget_ || !snapshot_->sketchEditing ||
      !snapshot_->activeCommandId.isEmpty())
    return false;
  return controller_->previewSketchCurve(
      sketchCurveDragTarget_->entityId, sketchCurveDragTarget_->first,
      {metresFromMillimeters(currentXMillimeters),
       metresFromMillimeters(currentYMillimeters)});
}

bool UiSession::submitSketchCurveDrag(qreal itemPressX, qreal itemPressY,
                                      qreal currentXMillimeters,
                                      qreal currentYMillimeters) {
  if (!sketchPickHandler_ || !snapshot_->sketchEditing ||
      !snapshot_->activeCommandId.isEmpty())
    return false;
  if (!sketchCurveDragTarget_) {
    const auto picked = sketchPickHandler_(QPointF{itemPressX, itemPressY}, 8.0,
                                           SketchSelectionKind::Curve);
    if (!picked)
      return false;
    sketchCurveDragTarget_ = SketchCurveDragTarget{
        picked->entityId,
        {metresFromMillimeters(picked->closestPointMillimeters.x()),
         metresFromMillimeters(picked->closestPointMillimeters.y())}};
  }
  const SketchCurveDragTarget target = *sketchCurveDragTarget_;
  sketchCurveDragTarget_.reset();
  const bool accepted = controller_->dragSketchCurve(
      target.entityId, target.first,
      {metresFromMillimeters(currentXMillimeters),
       metresFromMillimeters(currentYMillimeters)});
  refresh();
  return accepted;
}

void UiSession::cancelSketchCurveDrag() {
  sketchCurveDragTarget_.reset();
  controller_->clearSketchCurvePreview();
}

bool UiSession::toggleSketchConstruction() {
  if (!snapshot_->sketchEditing)
    return false;
  if (!snapshot_->activeCommandId.isEmpty()) {
    const auto field = std::ranges::find_if(
        snapshot_->fields, [](const FieldDescriptor &candidate) {
          return candidate.kind == FieldKind::Toggle &&
                 candidate.id.endsWith(QStringLiteral(".construction"));
        });
    if (field == snapshot_->fields.end() ||
        !std::holds_alternative<bool>(field->value))
      return false;
    controller_->editField(field->id, !std::get<bool>(field->value));
    refresh();
    return true;
  }
  const bool accepted = controller_->toggleSketchConstruction();
  refresh();
  return accepted;
}

void UiSession::setSketchPickHandler(SketchPickHandler handler) {
  sketchPickHandler_ = std::move(handler);
}

void UiSession::clearSketchPickHandler() { sketchPickHandler_ = {}; }

void UiSession::setSketchHoverHandler(SketchHoverHandler handler) {
  sketchHoverHandler_ = std::move(handler);
}

void UiSession::clearSketchHoverHandler() { sketchHoverHandler_ = {}; }

void UiSession::cancelActiveCommand() {
  gesturePreview_.clear();
  if (snapshot_->activeCommandId.isEmpty())
    return;
  controller_->cancelCommandDraft(snapshot_->activeCommandId);
  refresh();
}

bool UiSession::submitParameterEdit(const QString &parameterId,
                                    const QString &expression) {
  const ParameterEditRequest request{
      parameterId,
      snapshot_->projectRevision,
      expression,
  };
  const bool accepted = controller_->submitParameterEdit(request);
  refresh();
  if (accepted)
    emit commandRequested(QStringLiteral("parameter.set-expression"),
                          generation());
  return accepted;
}

bool UiSession::submitSourceEdit(const QString &source,
                                 const QString &expectedRevision,
                                 bool preview) {
  const SourceEditRequest request{
      snapshot_->selectedFunction.id,
      snapshot_->selectedFunction.sourcePath,
      expectedRevision,
      source,
  };
  const SourceEditMode mode =
      preview ? SourceEditMode::Preview : SourceEditMode::Apply;
  const bool accepted = controller_->submitSourceEdit(request, mode);
  refresh();
  if (accepted) {
    emit commandRequested(preview ? QStringLiteral("source.preview")
                                  : QStringLiteral("source.apply"),
                          generation());
  }
  return accepted;
}

bool UiSession::undo() {
  gesturePreview_.clear();
  const bool accepted = controller_->undo();
  refresh();
  if (accepted)
    emit commandRequested(QStringLiteral("version.undo"), generation());
  return accepted;
}

bool UiSession::redo() {
  gesturePreview_.clear();
  const bool accepted = controller_->redo();
  refresh();
  if (accepted)
    emit commandRequested(QStringLiteral("version.redo"), generation());
  return accepted;
}

void UiSession::refresh() {
  FrontendSnapshotPtr next = controller_->snapshot();
  Q_ASSERT(next);
  const std::uint32_t changed =
      snapshot_ ? changedProjections(*snapshot_, *next) : allProjections;
  snapshot_ = std::move(next);
  if (snapshot_->activeCommandId != QStringLiteral("sketch.rectangle") ||
      snapshot_->commandDraft.state != CommandDraftState::Editing)
    gesturePreview_.clear();
  generation_ =
      std::max(generation_ + 1, static_cast<qulonglong>(snapshot_->generation));
  queueProjectionNotification(changed);
}

void UiSession::queueProjectionNotification(std::uint32_t groups) {
  pendingProjectionGroups_ |= groups;
  if (projectionNotificationQueued_)
    return;
  projectionNotificationQueued_ = true;
  const bool queued = QMetaObject::invokeMethod(
      this,
      [this] {
        projectionNotificationQueued_ = false;
        const std::uint32_t pendingGroups =
            std::exchange(pendingProjectionGroups_, 0U);
        if ((pendingGroups & projectProjection) != 0U)
          emit projectProjectionChanged();
        if ((pendingGroups & navigationProjection) != 0U)
          emit navigationProjectionChanged();
        if ((pendingGroups & commandProjection) != 0U)
          emit commandProjectionChanged();
        if ((pendingGroups & commandListProjection) != 0U)
          emit commandListProjectionChanged();
        if ((pendingGroups & commandCatalogProjection) != 0U)
          emit commandCatalogProjectionChanged();
        if ((pendingGroups & commandFieldsProjection) != 0U)
          emit commandFieldsProjectionChanged();
        if ((pendingGroups & sketchProjection) != 0U)
          emit sketchProjectionChanged();
        if ((pendingGroups & catalogProjection) != 0U)
          emit catalogProjectionChanged();
        if ((pendingGroups & activityProjection) != 0U)
          emit activityProjectionChanged();
        if ((pendingGroups & hubProjection) != 0U)
          emit hubProjectionChanged();
        emit projectionChanged();
      },
      Qt::QueuedConnection);
  Q_ASSERT(queued);
}

} // namespace kearne::ui
