#pragma once

#include <QHash>
#include <QObject>
#include <QString>
#include <QTimer>
#include <QtQml/qqmlregistration.h>

namespace kearne::ui {

struct WorkspaceLayoutState {
  int structureWidth = 256;
  int inspectorWidth = 336;
  bool structureVisible = true;
  bool inspectorVisible = true;
  bool gridVisible = true;
  bool gridSnapEnabled = true;
  QString displayMode = QStringLiteral("shaded-edges");
};

class WorkspaceState : public QObject {
  Q_OBJECT
  QML_NAMED_ELEMENT(WorkspaceState)
  QML_UNCREATABLE("Available through App.workspace")
  Q_PROPERTY(
      QString activeWorkspaceId READ activeWorkspaceId NOTIFY stateChanged)
  Q_PROPERTY(int structureWidth READ structureWidth WRITE setStructureWidth
                 NOTIFY stateChanged)
  Q_PROPERTY(int inspectorWidth READ inspectorWidth WRITE setInspectorWidth
                 NOTIFY stateChanged)
  Q_PROPERTY(bool structureVisible READ structureVisible WRITE
                 setStructureVisible NOTIFY stateChanged)
  Q_PROPERTY(bool inspectorVisible READ inspectorVisible WRITE
                 setInspectorVisible NOTIFY stateChanged)
  Q_PROPERTY(bool gridVisible READ gridVisible WRITE setGridVisible NOTIFY
                 stateChanged)
  Q_PROPERTY(bool gridSnapEnabled READ gridSnapEnabled WRITE setGridSnapEnabled
                 NOTIFY stateChanged)
  Q_PROPERTY(QString displayMode READ displayMode WRITE setDisplayMode NOTIFY
                 stateChanged)
  Q_PROPERTY(QString lastError READ lastError NOTIFY stateChanged)

public:
  explicit WorkspaceState(QString path = {}, QObject *parent = nullptr);
  ~WorkspaceState() override;

  [[nodiscard]] QString activeWorkspaceId() const;
  [[nodiscard]] int structureWidth() const;
  [[nodiscard]] int inspectorWidth() const;
  [[nodiscard]] bool structureVisible() const;
  [[nodiscard]] bool inspectorVisible() const;
  [[nodiscard]] bool gridVisible() const;
  [[nodiscard]] bool gridSnapEnabled() const;
  [[nodiscard]] QString displayMode() const;
  [[nodiscard]] QString lastError() const;
  [[nodiscard]] QString path() const;

  Q_INVOKABLE void selectWorkspace(const QString &workspaceId);
  Q_INVOKABLE bool flush();
  Q_INVOKABLE void setPanelWidths(int structureWidth, int inspectorWidth);

  void setStructureWidth(int width);
  void setInspectorWidth(int width);
  void setStructureVisible(bool visible);
  void setInspectorVisible(bool visible);
  void setGridVisible(bool visible);
  void setGridSnapEnabled(bool enabled);
  void setDisplayMode(const QString &mode);

signals:
  void stateChanged();

private:
  [[nodiscard]] WorkspaceLayoutState current() const;
  void replaceCurrent(const WorkspaceLayoutState &state);
  void load();
  [[nodiscard]] bool save();

  QString path_;
  QString activeWorkspaceId_ = QStringLiteral("model");
  QHash<QString, WorkspaceLayoutState> layouts_;
  QString lastError_;
  QTimer saveTimer_;
};

} // namespace kearne::ui
