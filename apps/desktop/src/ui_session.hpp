#pragma once

#include "frontend_contract.hpp"
#include "sketch_gesture_preview.hpp"

#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <vector>

namespace kearne::ui {

struct SketchPickSelection {
  QString entityId;
  QString pointKey;
  QPointF closestPointMillimeters;
  bool operator==(const SketchPickSelection &) const = default;
};

using SketchPickHandler = std::function<std::optional<SketchPickSelection>(
    QPointF itemPoint, double tolerancePixels, SketchSelectionKind targets)>;
using SketchHoverHandler =
    std::function<void(std::optional<SketchPickSelection>)>;

class UiSession : public QObject {
  Q_OBJECT
  QML_NAMED_ELEMENT(UiSession)
  QML_UNCREATABLE("Available through App.ui")
  Q_PROPERTY(qulonglong generation READ generation NOTIFY projectionChanged)
  Q_PROPERTY(
      QString projectName READ projectName NOTIFY projectProjectionChanged)
  Q_PROPERTY(
      QString branchLabel READ branchLabel NOTIFY projectProjectionChanged)
  Q_PROPERTY(
      QString revisionLabel READ revisionLabel NOTIFY projectProjectionChanged)
  Q_PROPERTY(QString projectRevision READ projectRevision NOTIFY
                 projectProjectionChanged)
  Q_PROPERTY(QString activeWorkspaceId READ activeWorkspaceId NOTIFY
                 navigationProjectionChanged)
  Q_PROPERTY(QString activeCommandId READ activeCommandId NOTIFY
                 commandProjectionChanged)
  Q_PROPERTY(
      bool sketchEditing READ sketchEditing NOTIFY sketchProjectionChanged)
  Q_PROPERTY(QString commandDraftState READ commandDraftState NOTIFY
                 commandProjectionChanged)
  Q_PROPERTY(QString commandDraftBaseRevision READ commandDraftBaseRevision
                 NOTIFY commandProjectionChanged)
  Q_PROPERTY(bool commandPreviewSupported READ commandPreviewSupported NOTIFY
                 commandProjectionChanged)
  Q_PROPERTY(bool commandApplySupported READ commandApplySupported NOTIFY
                 commandProjectionChanged)
  Q_PROPERTY(QString activeSurfaceId READ activeSurfaceId NOTIFY
                 navigationProjectionChanged)
  Q_PROPERTY(QString settingsCategoryId READ settingsCategoryId NOTIFY
                 navigationProjectionChanged)
  Q_PROPERTY(
      int inspectorPage READ inspectorPage NOTIFY navigationProjectionChanged)
  Q_PROPERTY(
      QString viewportState READ viewportState NOTIFY sketchProjectionChanged)
  Q_PROPERTY(QString inspectorTitle READ inspectorTitle NOTIFY
                 commandProjectionChanged)
  Q_PROPERTY(QString inspectorStatus READ inspectorStatus NOTIFY
                 commandProjectionChanged)
  Q_PROPERTY(QString viewportHeadline READ viewportHeadline NOTIFY
                 commandProjectionChanged)
  Q_PROPERTY(QString viewportDetail READ viewportDetail NOTIFY
                 commandProjectionChanged)
  Q_PROPERTY(
      QString modelHealth READ modelHealth NOTIFY projectProjectionChanged)
  Q_PROPERTY(QString selectionSummary READ selectionSummary NOTIFY
                 projectProjectionChanged)
  Q_PROPERTY(
      QString agentStatus READ agentStatus NOTIFY activityProjectionChanged)
  Q_PROPERTY(
      QString modelSource READ modelSource NOTIFY projectProjectionChanged)
  Q_PROPERTY(QVariantMap selectedFunction READ selectedFunction NOTIFY
                 projectProjectionChanged)
  Q_PROPERTY(QString defaultLengthUnitId READ defaultLengthUnitId NOTIFY
                 catalogProjectionChanged)
  Q_PROPERTY(QString projectLengthUnitId READ projectLengthUnitId NOTIFY
                 catalogProjectionChanged)
  Q_PROPERTY(QString interfaceDensityId READ interfaceDensityId NOTIFY
                 catalogProjectionChanged)
  Q_PROPERTY(
      QString gridPlaneLabel READ gridPlaneLabel NOTIFY sketchProjectionChanged)
  Q_PROPERTY(QString gridSpacingLabel READ gridSpacingLabel NOTIFY
                 sketchProjectionChanged)
  Q_PROPERTY(qreal gridSpacingMillimeters READ gridSpacingMillimeters NOTIFY
                 sketchProjectionChanged)
  Q_PROPERTY(QString sketchSolveStatus READ sketchSolveStatus NOTIFY
                 sketchProjectionChanged)
  Q_PROPERTY(int sketchDegreesOfFreedom READ sketchDegreesOfFreedom NOTIFY
                 sketchProjectionChanged)
  Q_PROPERTY(QVariantList sketchPrimitives READ sketchPrimitives NOTIFY
                 sketchProjectionChanged)
  Q_PROPERTY(QString sketchInputKind READ sketchInputKind NOTIFY
                 sketchProjectionChanged)
  Q_PROPERTY(QString sketchSelectionKind READ sketchSelectionKind NOTIFY
                 sketchProjectionChanged)
  Q_PROPERTY(int sketchMinimumInputCount READ sketchMinimumInputCount NOTIFY
                 sketchProjectionChanged)
  Q_PROPERTY(int sketchMaximumInputCount READ sketchMaximumInputCount NOTIFY
                 sketchProjectionChanged)
  Q_PROPERTY(
      int sketchInputCount READ sketchInputCount NOTIFY sketchProjectionChanged)
  Q_PROPERTY(QString sketchInputPrompt READ sketchInputPrompt NOTIFY
                 sketchProjectionChanged)
  Q_PROPERTY(QString sketchHoveredEntityId READ sketchHoveredEntityId NOTIFY
                 sketchHoverChanged)
  Q_PROPERTY(QString sketchHoveredPointKey READ sketchHoveredPointKey NOTIFY
                 sketchHoverChanged)
  Q_PROPERTY(bool backendConnected READ backendConnected NOTIFY
                 catalogProjectionChanged)
  Q_PROPERTY(bool projectPersistenceAvailable READ projectPersistenceAvailable
                 NOTIFY catalogProjectionChanged)
  Q_PROPERTY(bool sourceEditingAvailable READ sourceEditingAvailable NOTIFY
                 projectProjectionChanged)
  Q_PROPERTY(bool canUndo READ canUndo NOTIFY projectProjectionChanged)
  Q_PROPERTY(bool canRedo READ canRedo NOTIFY projectProjectionChanged)
  Q_PROPERTY(bool sketchGesturePreviewVisible READ sketchGesturePreviewVisible
                 NOTIFY sketchGesturePreviewChanged)
  Q_PROPERTY(QVariantList sketchPreviewMeasurements READ
                 sketchPreviewMeasurements NOTIFY sketchGesturePreviewChanged)
  Q_PROPERTY(QVariantList sketchPreviewPrimitives READ sketchPreviewPrimitives
                 NOTIFY sketchGesturePreviewChanged)
  Q_PROPERTY(
      QVariantList lengthUnits READ lengthUnits NOTIFY catalogProjectionChanged)
  Q_PROPERTY(QVariantList preferenceCategories READ preferenceCategories NOTIFY
                 catalogProjectionChanged)
  Q_PROPERTY(
      QVariantList preferences READ preferences NOTIFY catalogProjectionChanged)
  Q_PROPERTY(
      QVariantList workspaces READ workspaces NOTIFY catalogProjectionChanged)
  Q_PROPERTY(
      QVariantList commands READ commands NOTIFY commandListProjectionChanged)
  Q_PROPERTY(QVariantList commandCatalog READ commandCatalog NOTIFY
                 commandCatalogProjectionChanged)
  Q_PROPERTY(
      QVariantList structure READ structure NOTIFY projectProjectionChanged)
  Q_PROPERTY(
      QVariantList revisions READ revisions NOTIFY projectProjectionChanged)
  Q_PROPERTY(QVariantList historyCommands READ historyCommands NOTIFY
                 projectProjectionChanged)
  Q_PROPERTY(
      QVariantList fields READ fields NOTIFY commandFieldsProjectionChanged)
  Q_PROPERTY(
      QVariantList parameters READ parameters NOTIFY projectProjectionChanged)
  Q_PROPERTY(QVariantList jobs READ jobs NOTIFY activityProjectionChanged)
  Q_PROPERTY(QVariantList diagnostics READ diagnostics NOTIFY
                 activityProjectionChanged)
  Q_PROPERTY(
      QVariantList proposals READ proposals NOTIFY activityProjectionChanged)
  Q_PROPERTY(QVariantList recentProjects READ recentProjects NOTIFY
                 hubProjectionChanged)
  Q_PROPERTY(QVariantList projectTemplates READ projectTemplates NOTIFY
                 hubProjectionChanged)
  Q_PROPERTY(
      QVariantList recoveryItems READ recoveryItems NOTIFY hubProjectionChanged)
  Q_PROPERTY(
      QVariantList operations READ operations NOTIFY activityProjectionChanged)
  Q_PROPERTY(QVariantList interfaceStates READ interfaceStates NOTIFY
                 catalogProjectionChanged)

public:
  explicit UiSession(std::unique_ptr<FrontendController> controller,
                     QObject *parent = nullptr);
  ~UiSession() override;

  [[nodiscard]] qulonglong generation() const;
  [[nodiscard]] QString projectName() const;
  [[nodiscard]] QString branchLabel() const;
  [[nodiscard]] QString revisionLabel() const;
  [[nodiscard]] QString projectRevision() const;
  [[nodiscard]] QString activeWorkspaceId() const;
  [[nodiscard]] QString activeCommandId() const;
  [[nodiscard]] bool sketchEditing() const;
  [[nodiscard]] QString commandDraftState() const;
  [[nodiscard]] QString commandDraftBaseRevision() const;
  [[nodiscard]] bool commandPreviewSupported() const;
  [[nodiscard]] bool commandApplySupported() const;
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
  [[nodiscard]] QVariantMap selectedFunction() const;
  [[nodiscard]] QString defaultLengthUnitId() const;
  [[nodiscard]] QString projectLengthUnitId() const;
  [[nodiscard]] QString interfaceDensityId() const;
  [[nodiscard]] QString gridPlaneLabel() const;
  [[nodiscard]] QString gridSpacingLabel() const;
  [[nodiscard]] qreal gridSpacingMillimeters() const;
  [[nodiscard]] QString sketchSolveStatus() const;
  [[nodiscard]] int sketchDegreesOfFreedom() const;
  [[nodiscard]] QVariantList sketchPrimitives() const;
  [[nodiscard]] QString sketchInputKind() const;
  [[nodiscard]] QString sketchSelectionKind() const;
  [[nodiscard]] int sketchMinimumInputCount() const;
  [[nodiscard]] int sketchMaximumInputCount() const;
  [[nodiscard]] int sketchInputCount() const;
  [[nodiscard]] QString sketchInputPrompt() const;
  [[nodiscard]] QString sketchHoveredEntityId() const;
  [[nodiscard]] QString sketchHoveredPointKey() const;
  [[nodiscard]] bool backendConnected() const;
  [[nodiscard]] bool projectPersistenceAvailable() const;
  [[nodiscard]] bool sourceEditingAvailable() const;
  [[nodiscard]] bool canUndo() const;
  [[nodiscard]] bool canRedo() const;
  [[nodiscard]] bool sketchGesturePreviewVisible() const;
  [[nodiscard]] QVariantList sketchPreviewMeasurements() const;
  [[nodiscard]] QVariantList sketchPreviewPrimitives() const;
  [[nodiscard]] QVariantList lengthUnits() const;
  [[nodiscard]] QVariantList preferenceCategories() const;
  [[nodiscard]] QVariantList preferences() const;
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
  [[nodiscard]] std::shared_ptr<const render::SketchSceneSnapshot>
  sketchScene() const;

  Q_INVOKABLE void navigateTo(const QString &surfaceId);
  Q_INVOKABLE void selectSettingsCategory(const QString &categoryId);
  Q_INVOKABLE void selectInspectorPage(const QString &pageId);
  Q_INVOKABLE void selectWorkspace(const QString &workspaceId);
  Q_INVOKABLE void openProject(const QString &workspaceId,
                               const QString &commandId);
  Q_INVOKABLE void selectEntity(const QString &entityId);
  Q_INVOKABLE void requestCommand(const QString &commandId);
  Q_INVOKABLE void setPreference(const QString &preferenceId,
                                 const QVariant &value);
  Q_INVOKABLE void editField(const QString &fieldId, const QVariant &value);
  Q_INVOKABLE bool submitActiveCommand(bool preview);
  Q_INVOKABLE bool submitSketchPoint(qreal xMillimeters, qreal yMillimeters);
  Q_INVOKABLE bool removeLastSketchInput();
  Q_INVOKABLE bool submitSketchDrag(qreal firstXMillimeters,
                                    qreal firstYMillimeters,
                                    qreal oppositeXMillimeters,
                                    qreal oppositeYMillimeters);
  Q_INVOKABLE bool previewSketchDrag(qreal firstXMillimeters,
                                     qreal firstYMillimeters,
                                     qreal oppositeXMillimeters,
                                     qreal oppositeYMillimeters);
  Q_INVOKABLE bool previewSketchPoint(qreal xMillimeters,
                                      qreal yMillimeters);
  Q_INVOKABLE QString formatProjectLength(qreal lengthMillimeters) const;
  Q_INVOKABLE void clearSketchGesturePreview();
  Q_INVOKABLE bool submitSketchEntity(const QString &entityId,
                                      const QString &subElementKey);
  Q_INVOKABLE bool submitSketchPointerClick(qreal itemX, qreal itemY);
  Q_INVOKABLE bool updateSketchPointerHover(qreal itemX, qreal itemY);
  Q_INVOKABLE void clearSketchPointerHover();
  Q_INVOKABLE bool beginSketchCurveDrag(qreal itemPressX, qreal itemPressY);
  Q_INVOKABLE bool previewSketchCurveDrag(qreal currentXMillimeters,
                                          qreal currentYMillimeters);
  Q_INVOKABLE bool submitSketchCurveDrag(qreal itemPressX, qreal itemPressY,
                                         qreal currentXMillimeters,
                                         qreal currentYMillimeters);
  Q_INVOKABLE void cancelSketchCurveDrag();
  Q_INVOKABLE bool toggleSketchConstruction();
  Q_INVOKABLE void cancelActiveCommand();
  Q_INVOKABLE bool submitParameterEdit(const QString &parameterId,
                                       const QString &expression);
  Q_INVOKABLE bool submitSourceEdit(const QString &source,
                                    const QString &expectedRevision,
                                    bool preview);
  Q_INVOKABLE bool undo();
  Q_INVOKABLE bool redo();

signals:
  void projectProjectionChanged();
  void navigationProjectionChanged();
  void commandProjectionChanged();
  void commandListProjectionChanged();
  void commandCatalogProjectionChanged();
  void commandFieldsProjectionChanged();
  void sketchProjectionChanged();
  void catalogProjectionChanged();
  void activityProjectionChanged();
  void hubProjectionChanged();
  void projectionChanged();
  void sketchGesturePreviewChanged();
  void sketchHoverChanged();
  void commandRequested(const QString &commandId, qulonglong generation);
  void preferenceChanged(const QString &preferenceId, const QVariant &value);

public:
  void setSketchPickHandler(SketchPickHandler handler);
  void clearSketchPickHandler();
  void setSketchHoverHandler(SketchHoverHandler handler);
  void clearSketchHoverHandler();
  [[nodiscard]] const std::vector<QString> &selectedSketchEntityIds() const {
    return snapshot_->selectedSketchEntityIds;
  }
  [[nodiscard]] std::span<const SketchPrimitiveProjection>
  sketchPrimitiveProjections() const {
    return snapshot_->sketchProjection.primitives;
  }
  void replacePreferenceOptions(const QString &preferenceId,
                                std::vector<UiOption> options,
                                const QString &value);

private:
  [[nodiscard]] bool
  previewSketchGesture(std::span<const QPointF> pointsMillimeters);
  void refresh();
  void queueProjectionNotification(std::uint32_t groups);

  std::unique_ptr<FrontendController> controller_;
  FrontendSnapshotPtr snapshot_;
  SketchGesturePreview gesturePreview_;
  SketchPickHandler sketchPickHandler_;
  SketchHoverHandler sketchHoverHandler_;
  QString activeSurfaceId_ = QStringLiteral("editor");
  QString settingsCategoryId_ = QStringLiteral("appearance");
  QString sketchHoveredEntityId_;
  QString sketchHoveredPointKey_;
  struct SketchCurveDragTarget {
    QString entityId;
    PlanePoint first;
  };
  std::optional<SketchCurveDragTarget> sketchCurveDragTarget_;
  int inspectorPage_ = 0;
  qulonglong generation_ = 0;
  std::uint32_t pendingProjectionGroups_ = 0;
  bool projectionNotificationQueued_ = false;
};

} // namespace kearne::ui
