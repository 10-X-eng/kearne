#include "ui_session.hpp"

#include <QMetaObject>
#include <QLocale>
#include <QPointer>
#include <QVariantMap>

#include <algorithm>
#include <cmath>
#include <utility>

namespace kearne::ui {
namespace {

using Record = QVariantMap;

template <typename Value, typename Convert>
QVariantList recordsFor(const std::vector<Value> &values, Convert convert) {
  QVariantList result;
  result.reserve(static_cast<qsizetype>(values.size()));
  for (const Value &value : values)
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
        {QStringLiteral("workspaceId"), command.workspaceId},
        {QStringLiteral("shortcut"), command.shortcut},
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
    const std::vector<SketchPrimitiveProjection> &primitives) {
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
        {QStringLiteral("draft"), primitive.draft}};
  });
}

QString formatLength(double millimeters, const QString &unitId) {
  if (!std::isfinite(millimeters))
    return QStringLiteral("—");
  double value = millimeters;
  QString symbol = QStringLiteral("mm");
  int decimals = 3;
  if (unitId == QStringLiteral("cm")) {
    value /= 10.0;
    symbol = QStringLiteral("cm");
    decimals = 4;
  } else if (unitId == QStringLiteral("m")) {
    value /= millimetersPerMeter;
    symbol = QStringLiteral("m");
    decimals = 6;
  } else if (unitId == QStringLiteral("in")) {
    value /= 25.4;
    symbol = QStringLiteral("in");
    decimals = 4;
  }
  if (std::abs(value) < std::pow(10.0, -decimals) * 0.5)
    value = 0.0;
  QString number = QLocale::c().toString(value, 'f', decimals);
  while (number.contains(QLatin1Char('.')) && number.endsWith(QLatin1Char('0')))
    number.chop(1);
  if (number.endsWith(QLatin1Char('.')))
    number.chop(1);
  return number + QLatin1Char(' ') + symbol;
}

} // namespace

UiSession::UiSession(std::unique_ptr<FrontendPort> port, QObject *parent)
    : QObject(parent), port_(std::move(port)) {
  QObject::connect(&gesturePreview_, &SketchGesturePreview::previewChanged,
                   this, &UiSession::sketchDragPreviewChanged);
  const QPointer<UiSession> lifetime{this};
  port_->setChangeHandler([lifetime] {
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

UiSession::~UiSession() { port_->setChangeHandler({}); }

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
bool UiSession::backendConnected() const { return snapshot_->backendConnected; }
bool UiSession::projectPersistenceAvailable() const {
  return snapshot_->projectPersistenceAvailable;
}
bool UiSession::sourceEditingAvailable() const {
  return snapshot_->sourceEditingAvailable;
}
bool UiSession::canUndo() const { return snapshot_->canUndo; }
bool UiSession::canRedo() const { return snapshot_->canRedo; }
bool UiSession::sketchDragPreviewVisible() const {
  return gesturePreview_.visible();
}
bool UiSession::sketchDragPreviewConstruction() const {
  return gesturePreview_.construction();
}
QPointF UiSession::sketchDragPreviewFirst() const {
  return gesturePreview_.first();
}
QPointF UiSession::sketchDragPreviewSecond() const {
  return gesturePreview_.second();
}
QPointF UiSession::sketchDragPreviewThird() const {
  return gesturePreview_.third();
}
QPointF UiSession::sketchDragPreviewFourth() const {
  return gesturePreview_.fourth();
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
  emit projectionChanged();
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
  emit projectionChanged();
}

void UiSession::selectInspectorPage(const QString &pageId) {
  const int page = pageId == QStringLiteral("source") ? 1 : 0;
  if (page == inspectorPage_)
    return;
  inspectorPage_ = page;
  ++generation_;
  emit projectionChanged();
}

void UiSession::selectWorkspace(const QString &workspaceId) {
  port_->selectWorkspace(workspaceId);
  refresh();
}

void UiSession::openProject(const QString &workspaceId,
                            const QString &commandId) {
  port_->selectWorkspace(workspaceId);
  port_->requestCommand(commandId);
  activeSurfaceId_ = QStringLiteral("editor");
  refresh();
  emit commandRequested(commandId, generation());
}

void UiSession::selectEntity(const QString &entityId) {
  port_->selectEntity(entityId);
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
  port_->requestCommand(commandId);
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
  port_->setPreference(preferenceId, normalized);
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
  port_->replacePreferenceOptions(preferenceId, std::move(options), value);
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
  port_->editField(fieldId, normalized);
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
  const bool accepted = port_->submitCommandDraft(request, mode);
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
  const bool accepted = port_->submitSketchInput(request);
  refresh();
  return accepted;
}

bool UiSession::submitSketchDrag(qreal firstXMillimeters,
                                 qreal firstYMillimeters,
                                 qreal oppositeXMillimeters,
                                 qreal oppositeYMillimeters) {
  gesturePreview_.clear();
  if (snapshot_->sketchInteraction.inputKind != SketchInputKind::PlanePoint ||
      snapshot_->sketchInteraction.inputCount != 0 ||
      snapshot_->sketchInteraction.maximumInputCount != 2)
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
      snapshot_->sketchInteraction.maximumInputCount != 2 ||
      snapshot_->commandDraft.state != CommandDraftState::Editing)
    return false;
  const auto construction =
      std::ranges::find_if(snapshot_->fields, [](const FieldDescriptor &field) {
        return field.id.endsWith(QStringLiteral(".construction"));
      });
  const bool enabled = construction != snapshot_->fields.end() &&
                       std::holds_alternative<bool>(construction->value) &&
                       std::get<bool>(construction->value);
  return gesturePreview_.updateDrag(
      snapshot_->activeCommandId, firstXMillimeters, firstYMillimeters,
      oppositeXMillimeters, oppositeYMillimeters, enabled);
}

QString UiSession::formatProjectLength(qreal lengthMillimeters) const {
  return formatLength(lengthMillimeters, snapshot_->projectLengthUnitId);
}

void UiSession::clearSketchDragPreview() { gesturePreview_.clear(); }

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
  const bool accepted = port_->submitSketchInput(request);
  refresh();
  return accepted;
}

void UiSession::cancelActiveCommand() {
  gesturePreview_.clear();
  if (snapshot_->activeCommandId.isEmpty())
    return;
  port_->cancelCommandDraft(snapshot_->activeCommandId);
  refresh();
}

bool UiSession::submitParameterEdit(const QString &parameterId,
                                    const QString &expression) {
  const ParameterEditRequest request{
      parameterId,
      snapshot_->projectRevision,
      expression,
  };
  const bool accepted = port_->submitParameterEdit(request);
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
  const bool accepted = port_->submitSourceEdit(request, mode);
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
  const bool accepted = port_->undo();
  refresh();
  if (accepted)
    emit commandRequested(QStringLiteral("version.undo"), generation());
  return accepted;
}

bool UiSession::redo() {
  gesturePreview_.clear();
  const bool accepted = port_->redo();
  refresh();
  if (accepted)
    emit commandRequested(QStringLiteral("version.redo"), generation());
  return accepted;
}

void UiSession::refresh() {
  FrontendSnapshotPtr next = port_->snapshot();
  Q_ASSERT(next);
  snapshot_ = std::move(next);
  if (snapshot_->activeCommandId != QStringLiteral("sketch.rectangle") ||
      snapshot_->commandDraft.state != CommandDraftState::Editing)
    gesturePreview_.clear();
  generation_ =
      std::max(generation_ + 1, static_cast<qulonglong>(snapshot_->generation));
  emit projectionChanged();
}

} // namespace kearne::ui
