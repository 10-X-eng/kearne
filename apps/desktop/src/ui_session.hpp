#pragma once

#include <QObject>
#include <QString>
#include <QVariantList>

#include <cstdint>
#include <memory>

namespace kearne::ui {

struct FrontendSnapshot {
  std::uint64_t generation = 0;
  QString projectName;
  QString branchLabel;
  QString revisionLabel;
  QString activeWorkspaceId;
  QString activeCommandId;
  QString viewportState;
  QString inspectorTitle;
  QString inspectorStatus;
  QString viewportHeadline;
  QString viewportDetail;
  QString modelHealth;
  QString selectionSummary;
  QString agentStatus;
  QString modelSource;
  QString defaultLengthUnitId;
  QString projectLengthUnitId;
  QString gridPlaneLabel;
  QString gridSpacingLabel;
  bool gridVisible = true;
  bool gridSnapEnabled = true;
  bool backendConnected = false;
  QVariantList lengthUnits;
  QVariantList workspaces;
  QVariantList commands;
  QVariantList commandCatalog;
  QVariantList structure;
  QVariantList revisions;
  QVariantList historyCommands;
  QVariantList fields;
  QVariantList parameters;
  QVariantList jobs;
  QVariantList diagnostics;
  QVariantList proposals;
  QVariantList recentProjects;
  QVariantList projectTemplates;
  QVariantList recoveryItems;
  QVariantList operations;
  QVariantList interfaceStates;
};

class FrontendPort {
public:
  virtual ~FrontendPort() = default;
  [[nodiscard]] virtual FrontendSnapshot snapshot() const = 0;
  virtual void selectWorkspace(const QString &workspaceId) = 0;
  virtual void selectEntity(const QString &entityId) = 0;
  virtual void requestCommand(const QString &commandId) = 0;
  virtual void setDefaultLengthUnit(const QString &unitId) = 0;
  virtual void setProjectLengthUnit(const QString &unitId) = 0;
  virtual void setGridVisible(bool visible) = 0;
  virtual void setGridSnapEnabled(bool enabled) = 0;
};

[[nodiscard]] std::unique_ptr<FrontendPort> makeDevelopmentFrontendPort();

class UiSession final : public QObject {
  Q_OBJECT
  Q_PROPERTY(qulonglong generation READ generation NOTIFY projectionChanged)
  Q_PROPERTY(QString projectName READ projectName NOTIFY projectionChanged)
  Q_PROPERTY(QString branchLabel READ branchLabel NOTIFY projectionChanged)
  Q_PROPERTY(QString revisionLabel READ revisionLabel NOTIFY projectionChanged)
  Q_PROPERTY(QString activeWorkspaceId READ activeWorkspaceId NOTIFY projectionChanged)
  Q_PROPERTY(QString activeCommandId READ activeCommandId NOTIFY projectionChanged)
  Q_PROPERTY(QString activeSurfaceId READ activeSurfaceId NOTIFY projectionChanged)
  Q_PROPERTY(QString settingsCategoryId READ settingsCategoryId NOTIFY projectionChanged)
  Q_PROPERTY(int inspectorPage READ inspectorPage NOTIFY projectionChanged)
  Q_PROPERTY(QString viewportState READ viewportState NOTIFY projectionChanged)
  Q_PROPERTY(QString inspectorTitle READ inspectorTitle NOTIFY projectionChanged)
  Q_PROPERTY(QString inspectorStatus READ inspectorStatus NOTIFY projectionChanged)
  Q_PROPERTY(QString viewportHeadline READ viewportHeadline NOTIFY projectionChanged)
  Q_PROPERTY(QString viewportDetail READ viewportDetail NOTIFY projectionChanged)
  Q_PROPERTY(QString modelHealth READ modelHealth NOTIFY projectionChanged)
  Q_PROPERTY(QString selectionSummary READ selectionSummary NOTIFY projectionChanged)
  Q_PROPERTY(QString agentStatus READ agentStatus NOTIFY projectionChanged)
  Q_PROPERTY(QString modelSource READ modelSource NOTIFY projectionChanged)
  Q_PROPERTY(QString defaultLengthUnitId READ defaultLengthUnitId NOTIFY projectionChanged)
  Q_PROPERTY(QString projectLengthUnitId READ projectLengthUnitId NOTIFY projectionChanged)
  Q_PROPERTY(QString gridPlaneLabel READ gridPlaneLabel NOTIFY projectionChanged)
  Q_PROPERTY(QString gridSpacingLabel READ gridSpacingLabel NOTIFY projectionChanged)
  Q_PROPERTY(bool gridVisible READ gridVisible NOTIFY projectionChanged)
  Q_PROPERTY(bool gridSnapEnabled READ gridSnapEnabled NOTIFY projectionChanged)
  Q_PROPERTY(bool backendConnected READ backendConnected NOTIFY projectionChanged)
  Q_PROPERTY(QVariantList lengthUnits READ lengthUnits NOTIFY projectionChanged)
  Q_PROPERTY(QVariantList workspaces READ workspaces NOTIFY projectionChanged)
  Q_PROPERTY(QVariantList commands READ commands NOTIFY projectionChanged)
  Q_PROPERTY(QVariantList commandCatalog READ commandCatalog NOTIFY projectionChanged)
  Q_PROPERTY(QVariantList structure READ structure NOTIFY projectionChanged)
  Q_PROPERTY(QVariantList revisions READ revisions NOTIFY projectionChanged)
  Q_PROPERTY(QVariantList historyCommands READ historyCommands NOTIFY projectionChanged)
  Q_PROPERTY(QVariantList fields READ fields NOTIFY projectionChanged)
  Q_PROPERTY(QVariantList parameters READ parameters NOTIFY projectionChanged)
  Q_PROPERTY(QVariantList jobs READ jobs NOTIFY projectionChanged)
  Q_PROPERTY(QVariantList diagnostics READ diagnostics NOTIFY projectionChanged)
  Q_PROPERTY(QVariantList proposals READ proposals NOTIFY projectionChanged)
  Q_PROPERTY(QVariantList recentProjects READ recentProjects NOTIFY projectionChanged)
  Q_PROPERTY(QVariantList projectTemplates READ projectTemplates NOTIFY projectionChanged)
  Q_PROPERTY(QVariantList recoveryItems READ recoveryItems NOTIFY projectionChanged)
  Q_PROPERTY(QVariantList operations READ operations NOTIFY projectionChanged)
  Q_PROPERTY(QVariantList interfaceStates READ interfaceStates NOTIFY projectionChanged)

public:
  explicit UiSession(std::unique_ptr<FrontendPort> port, QObject *parent = nullptr);

  [[nodiscard]] qulonglong generation() const;
  [[nodiscard]] QString projectName() const;
  [[nodiscard]] QString branchLabel() const;
  [[nodiscard]] QString revisionLabel() const;
  [[nodiscard]] QString activeWorkspaceId() const;
  [[nodiscard]] QString activeCommandId() const;
  [[nodiscard]] QString activeSurfaceId() const;
  [[nodiscard]] QString settingsCategoryId() const;
  [[nodiscard]] int inspectorPage() const;
  [[nodiscard]] QString viewportState() const;
  [[nodiscard]] QString inspectorTitle() const;
  [[nodiscard]] QString inspectorStatus() const;
  [[nodiscard]] QString viewportHeadline() const;
  [[nodiscard]] QString viewportDetail() const;
  [[nodiscard]] QString modelHealth() const;
  [[nodiscard]] QString selectionSummary() const;
  [[nodiscard]] QString agentStatus() const;
  [[nodiscard]] QString modelSource() const;
  [[nodiscard]] QString defaultLengthUnitId() const;
  [[nodiscard]] QString projectLengthUnitId() const;
  [[nodiscard]] QString gridPlaneLabel() const;
  [[nodiscard]] QString gridSpacingLabel() const;
  [[nodiscard]] bool gridVisible() const;
  [[nodiscard]] bool gridSnapEnabled() const;
  [[nodiscard]] bool backendConnected() const;
  [[nodiscard]] QVariantList lengthUnits() const;
  [[nodiscard]] QVariantList workspaces() const;
  [[nodiscard]] QVariantList commands() const;
  [[nodiscard]] QVariantList commandCatalog() const;
  [[nodiscard]] QVariantList structure() const;
  [[nodiscard]] QVariantList revisions() const;
  [[nodiscard]] QVariantList historyCommands() const;
  [[nodiscard]] QVariantList fields() const;
  [[nodiscard]] QVariantList parameters() const;
  [[nodiscard]] QVariantList jobs() const;
  [[nodiscard]] QVariantList diagnostics() const;
  [[nodiscard]] QVariantList proposals() const;
  [[nodiscard]] QVariantList recentProjects() const;
  [[nodiscard]] QVariantList projectTemplates() const;
  [[nodiscard]] QVariantList recoveryItems() const;
  [[nodiscard]] QVariantList operations() const;
  [[nodiscard]] QVariantList interfaceStates() const;

  Q_INVOKABLE void navigateTo(const QString &surfaceId);
  Q_INVOKABLE void selectSettingsCategory(const QString &categoryId);
  Q_INVOKABLE void selectInspectorPage(const QString &pageId);
  Q_INVOKABLE void selectWorkspace(const QString &workspaceId);
  Q_INVOKABLE void selectEntity(const QString &entityId);
  Q_INVOKABLE void requestCommand(const QString &commandId);
  Q_INVOKABLE void setDefaultLengthUnit(const QString &unitId);
  Q_INVOKABLE void setProjectLengthUnit(const QString &unitId);
  Q_INVOKABLE void setGridVisible(bool visible);
  Q_INVOKABLE void setGridSnapEnabled(bool enabled);

signals:
  void projectionChanged();
  void commandRequested(const QString &commandId, qulonglong generation);

private:
  void refresh();

  std::unique_ptr<FrontendPort> port_;
  FrontendSnapshot snapshot_;
  QString activeSurfaceId_ = QStringLiteral("editor");
  QString settingsCategoryId_ = QStringLiteral("appearance");
  int inspectorPage_ = 0;
  qulonglong generation_ = 0;
};

} // namespace kearne::ui
