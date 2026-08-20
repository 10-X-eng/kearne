#include "workspace_state.hpp"

#include "local_json_store.hpp"

#include <QDir>
#include <QJsonObject>
#include <QRegularExpression>
#include <QStandardPaths>

#include <algorithm>
#include <utility>

namespace kearne::ui {
namespace {

constexpr qsizetype maximumWorkspaceStateBytes = 256 * 1024;
constexpr int minimumStructureWidth = 200;
constexpr int maximumStructureWidth = 420;
constexpr int minimumInspectorWidth = 300;
constexpr int maximumInspectorWidth = 480;

QString defaultPath() {
  return QDir(QStandardPaths::writableLocation(
                  QStandardPaths::AppConfigLocation))
      .filePath(QStringLiteral("workspace-state.json"));
}

bool validWorkspaceId(const QString &id) {
  static const QRegularExpression pattern(
      QStringLiteral("^[a-z0-9][a-z0-9-]{0,63}$"));
  return pattern.match(id).hasMatch();
}

bool validDisplayMode(const QString &mode) {
  static const QStringList modes{
      QStringLiteral("shaded-edges"),
      QStringLiteral("shaded"),
      QStringLiteral("wireframe"),
  };
  return modes.contains(mode);
}

bool readLayout(const QJsonObject &record, WorkspaceLayoutState &state,
                bool displayModeRequired) {
  static const QStringList fields{
      QStringLiteral("structure_width"),   QStringLiteral("inspector_width"),
      QStringLiteral("structure_visible"), QStringLiteral("inspector_visible"),
      QStringLiteral("grid_visible"),      QStringLiteral("grid_snap_enabled"),
      QStringLiteral("display_mode"),
  };
  for (auto field = record.constBegin(); field != record.constEnd(); ++field) {
    if (!fields.contains(field.key()))
      return false;
  }
  if (!record.value(QStringLiteral("structure_width")).isDouble() ||
      !record.value(QStringLiteral("inspector_width")).isDouble() ||
      !record.value(QStringLiteral("structure_visible")).isBool() ||
      !record.value(QStringLiteral("inspector_visible")).isBool() ||
      !record.value(QStringLiteral("grid_visible")).isBool() ||
      !record.value(QStringLiteral("grid_snap_enabled")).isBool() ||
      (displayModeRequired &&
       !record.value(QStringLiteral("display_mode")).isString()))
    return false;
  state.structureWidth =
      record.value(QStringLiteral("structure_width")).toInt();
  state.inspectorWidth =
      record.value(QStringLiteral("inspector_width")).toInt();
  if (state.structureWidth < minimumStructureWidth ||
      state.structureWidth > maximumStructureWidth ||
      state.inspectorWidth < minimumInspectorWidth ||
      state.inspectorWidth > maximumInspectorWidth)
    return false;
  state.structureVisible =
      record.value(QStringLiteral("structure_visible")).toBool();
  state.inspectorVisible =
      record.value(QStringLiteral("inspector_visible")).toBool();
  state.gridVisible = record.value(QStringLiteral("grid_visible")).toBool();
  state.gridSnapEnabled =
      record.value(QStringLiteral("grid_snap_enabled")).toBool();
  if (record.contains(QStringLiteral("display_mode"))) {
    state.displayMode = record.value(QStringLiteral("display_mode")).toString();
    if (!validDisplayMode(state.displayMode))
      return false;
  }
  return true;
}

QJsonObject layoutRecord(const WorkspaceLayoutState &state) {
  return {
      {QStringLiteral("structure_width"), state.structureWidth},
      {QStringLiteral("inspector_width"), state.inspectorWidth},
      {QStringLiteral("structure_visible"), state.structureVisible},
      {QStringLiteral("inspector_visible"), state.inspectorVisible},
      {QStringLiteral("grid_visible"), state.gridVisible},
      {QStringLiteral("grid_snap_enabled"), state.gridSnapEnabled},
      {QStringLiteral("display_mode"), state.displayMode},
  };
}

} // namespace

WorkspaceState::WorkspaceState(QString path, QObject *parent)
    : QObject(parent), path_(path.isEmpty() ? defaultPath() : std::move(path)) {
  saveTimer_.setSingleShot(true);
  saveTimer_.setInterval(200);
  connect(&saveTimer_, &QTimer::timeout, this, [this] {
    if (!save())
      emit stateChanged();
  });
  load();
}

WorkspaceState::~WorkspaceState() {
  if (saveTimer_.isActive())
    static_cast<void>(save());
}

QString WorkspaceState::activeWorkspaceId() const { return activeWorkspaceId_; }
int WorkspaceState::structureWidth() const { return current().structureWidth; }
int WorkspaceState::inspectorWidth() const { return current().inspectorWidth; }
bool WorkspaceState::structureVisible() const {
  return current().structureVisible;
}
bool WorkspaceState::inspectorVisible() const {
  return current().inspectorVisible;
}
bool WorkspaceState::gridVisible() const { return current().gridVisible; }
bool WorkspaceState::gridSnapEnabled() const {
  return current().gridSnapEnabled;
}
QString WorkspaceState::displayMode() const { return current().displayMode; }
QString WorkspaceState::lastError() const { return lastError_; }
QString WorkspaceState::path() const { return path_; }

void WorkspaceState::selectWorkspace(const QString &workspaceId) {
  if (!validWorkspaceId(workspaceId) || workspaceId == activeWorkspaceId_)
    return;
  activeWorkspaceId_ = workspaceId;
  emit stateChanged();
}

bool WorkspaceState::flush() {
  saveTimer_.stop();
  const bool saved = save();
  if (!saved)
    emit stateChanged();
  return saved;
}

void WorkspaceState::setPanelWidths(int structureWidth, int inspectorWidth) {
  WorkspaceLayoutState state = current();
  const int boundedStructure =
      std::clamp(structureWidth, minimumStructureWidth, maximumStructureWidth);
  const int boundedInspector =
      std::clamp(inspectorWidth, minimumInspectorWidth, maximumInspectorWidth);
  if (state.structureWidth == boundedStructure &&
      state.inspectorWidth == boundedInspector)
    return;
  state.structureWidth = boundedStructure;
  state.inspectorWidth = boundedInspector;
  replaceCurrent(state);
}

void WorkspaceState::setStructureWidth(int width) {
  setPanelWidths(width, inspectorWidth());
}

void WorkspaceState::setInspectorWidth(int width) {
  setPanelWidths(structureWidth(), width);
}

void WorkspaceState::setStructureVisible(bool visible) {
  WorkspaceLayoutState state = current();
  if (state.structureVisible == visible)
    return;
  state.structureVisible = visible;
  replaceCurrent(state);
}

void WorkspaceState::setInspectorVisible(bool visible) {
  WorkspaceLayoutState state = current();
  if (state.inspectorVisible == visible)
    return;
  state.inspectorVisible = visible;
  replaceCurrent(state);
}

void WorkspaceState::setGridVisible(bool visible) {
  WorkspaceLayoutState state = current();
  if (state.gridVisible == visible)
    return;
  state.gridVisible = visible;
  replaceCurrent(state);
}

void WorkspaceState::setGridSnapEnabled(bool enabled) {
  WorkspaceLayoutState state = current();
  if (state.gridSnapEnabled == enabled)
    return;
  state.gridSnapEnabled = enabled;
  replaceCurrent(state);
}

void WorkspaceState::setDisplayMode(const QString &mode) {
  WorkspaceLayoutState state = current();
  if (!validDisplayMode(mode) || state.displayMode == mode)
    return;
  state.displayMode = mode;
  replaceCurrent(state);
}

WorkspaceLayoutState WorkspaceState::current() const {
  return layouts_.value(activeWorkspaceId_);
}

void WorkspaceState::replaceCurrent(const WorkspaceLayoutState &state) {
  layouts_.insert(activeWorkspaceId_, state);
  saveTimer_.start();
  emit stateChanged();
}

void WorkspaceState::load() {
  const auto document =
      local_json::read(path_, maximumWorkspaceStateBytes, lastError_);
  if (!document)
    return;
  for (auto field = document->constBegin(); field != document->constEnd();
       ++field) {
    if (field.key() != QStringLiteral("schema") &&
        field.key() != QStringLiteral("workspaces")) {
      lastError_ = QStringLiteral("Workspace state contains an unknown field");
      return;
    }
  }
  const QString schema = document->value(QStringLiteral("schema")).toString();
  if ((schema != QStringLiteral("kearne.workspace-state/v1") &&
       schema != QStringLiteral("kearne.workspace-state/v2")) ||
      !document->value(QStringLiteral("workspaces")).isObject()) {
    lastError_ = QStringLiteral("Workspace state schema is unsupported");
    return;
  }
  QHash<QString, WorkspaceLayoutState> loaded;
  const QJsonObject workspaces =
      document->value(QStringLiteral("workspaces")).toObject();
  for (auto workspace = workspaces.constBegin();
       workspace != workspaces.constEnd(); ++workspace) {
    WorkspaceLayoutState state;
    if (!validWorkspaceId(workspace.key()) || !workspace.value().isObject() ||
        !readLayout(workspace.value().toObject(), state,
                    schema == QStringLiteral("kearne.workspace-state/v2"))) {
      lastError_ = QStringLiteral("Workspace state contains an invalid layout");
      return;
    }
    loaded.insert(workspace.key(), state);
  }
  layouts_ = std::move(loaded);
  lastError_.clear();
}

bool WorkspaceState::save() {
  QJsonObject workspaces;
  for (auto layout = layouts_.constBegin(); layout != layouts_.constEnd();
       ++layout)
    workspaces.insert(layout.key(), layoutRecord(layout.value()));
  return local_json::write(
      path_,
      {{QStringLiteral("schema"), QStringLiteral("kearne.workspace-state/v2")},
       {QStringLiteral("workspaces"), workspaces}},
      lastError_);
}

} // namespace kearne::ui
