#pragma once

#include <QString>

#include <cstdint>
#include <functional>
#include <memory>
#include <utility>
#include <variant>
#include <vector>

namespace kearne::render {
class SketchSceneSnapshot;
}

namespace kearne::ui {

inline constexpr double millimetersPerMeter = 1'000.0;

[[nodiscard]] constexpr double
metresFromMillimeters(double millimeters) noexcept {
  return millimeters / millimetersPerMeter;
}

[[nodiscard]] constexpr double millimetersFromMetres(double metres) noexcept {
  return metres * millimetersPerMeter;
}

struct UiOption {
  UiOption() = default;
  UiOption(QString optionId, QString optionLabel, QString optionSymbol = {})
      : id(std::move(optionId)), label(std::move(optionLabel)),
        symbol(std::move(optionSymbol)) {}
  bool operator==(const UiOption &) const = default;

  QString id;
  QString label;
  QString symbol;
};

enum class PreferenceKind { Choice, Toggle, Text };
using PreferenceValue = std::variant<QString, bool>;

struct PreferenceCategory {
  bool operator==(const PreferenceCategory &) const = default;

  QString id;
  QString label;
  QString icon;
};

struct PreferenceDescriptor {
  bool operator==(const PreferenceDescriptor &) const = default;

  QString id;
  QString categoryId;
  QString label;
  QString detail;
  PreferenceKind kind = PreferenceKind::Text;
  PreferenceValue value;
  std::vector<UiOption> options;
  bool enabled = true;
};

enum class FieldKind { Text, Choice, Reference, Expression, Toggle };
enum class SourceEditMode { Preview, Apply };
enum class CommandDraftMode { Preview, Apply };
enum class SketchInputKind { None, PlanePoint, Entity };
enum class SketchPrimitiveKind {
  Point,
  Line,
  Circle,
  Arc,
  Ellipse,
  EllipticalArc,
  HyperbolicArc,
  ParabolicArc,
  BSpline
};
enum class SketchSelectionKind { Any, Point, Curve };
enum class CommandDraftState {
  None,
  Editing,
  Pending,
  Preview,
  Stale,
  Unavailable,
  Rejected
};
using FieldValue = std::variant<QString, bool>;

struct FieldDescriptor {
  FieldDescriptor() = default;
  FieldDescriptor(QString fieldId, QString fieldLabel, FieldKind fieldKind,
                  FieldValue fieldValue, QString fieldEffectiveValue = {},
                  std::vector<UiOption> fieldOptions = {},
                  bool fieldReadOnly = false)
      : id(std::move(fieldId)), label(std::move(fieldLabel)), kind(fieldKind),
        value(std::move(fieldValue)),
        effectiveValue(std::move(fieldEffectiveValue)),
        options(std::move(fieldOptions)), readOnly(fieldReadOnly) {}

  bool operator==(const FieldDescriptor &) const = default;

  QString id;
  QString label;
  FieldKind kind = FieldKind::Text;
  FieldValue value;
  QString effectiveValue;
  std::vector<UiOption> options;
  bool readOnly = false;
};

struct CommandDescriptor {
  bool operator==(const CommandDescriptor &) const = default;

  QString id;
  QString label;
  QString icon;
  QString group;
  QString menu;
  QString workspaceId;
  QString shortcut;
  bool primary = false;
  bool available = true;
  QString unavailableReason;
};

struct CommandDraftSummary {
  bool operator==(const CommandDraftSummary &) const = default;

  QString commandId;
  QString baseRevision;
  CommandDraftState state = CommandDraftState::None;
  bool previewSupported = false;
  bool applySupported = false;
};

struct CommandFieldInput {
  QString id;
  FieldValue value;
};

struct CommandDraftRequest {
  QString commandId;
  QString expectedRevision;
  std::vector<CommandFieldInput> fields;
};

struct PlanePoint {
  bool operator==(const PlanePoint &) const = default;

  double xMetres = 0.0;
  double yMetres = 0.0;
};

struct SketchPrimitiveProjection {
  bool operator==(const SketchPrimitiveProjection &) const = default;

  QString id;
  SketchPrimitiveKind kind = SketchPrimitiveKind::Point;
  std::vector<PlanePoint> points;
  std::vector<QString> pointKeys;
  std::vector<QString> selectedPointKeys;
  double radiusMetres = 0.0;
  bool construction = false;
  bool selected = false;
  bool draft = false;
  double startAngleRadians = 0.0;
  double sweepAngleRadians = 0.0;
  double secondaryRadiusMetres = 0.0;
  double rotationAngleRadians = 0.0;
};

struct SketchProjection {
  bool operator==(const SketchProjection &) const = default;

  QString sourceRevision;
  QString functionId;
  QString planeId;
  QString coordinateUnitId;
  QString solveStatus;
  int degreesOfFreedom = -1;
  std::vector<SketchPrimitiveProjection> primitives;
};

struct SketchInteractionSummary {
  bool operator==(const SketchInteractionSummary &) const = default;

  QString commandId;
  QString expectedRevision;
  SketchInputKind inputKind = SketchInputKind::None;
  int minimumInputCount = 0;
  int maximumInputCount = 0;
  int inputCount = 0;
  QString prompt;
  std::vector<SketchSelectionKind> selectionSequence;
  std::vector<SketchInputKind> inputSequence;
};

struct SketchInputRequest {
  QString commandId;
  QString expectedRevision;
  SketchInputKind kind = SketchInputKind::None;
  PlanePoint planePoint;
  QString entityId;
  QString subElementKey;
};

struct WorkspaceDescriptor {
  bool operator==(const WorkspaceDescriptor &) const = default;

  QString id;
  QString label;
  QString icon;
};

struct StructureItem {
  bool operator==(const StructureItem &) const = default;

  QString id;
  QString label;
  int depth = 0;
  QString kind;
};

struct RevisionSummary {
  bool operator==(const RevisionSummary &) const = default;

  QString id;
  QString label;
  QString detail;
};

struct ParameterSummary {
  bool operator==(const ParameterSummary &) const = default;

  QString id;
  QString name;
  QString expression;
  QString value;
};

struct ParameterEditRequest {
  QString parameterId;
  QString expectedRevision;
  QString expression;
};

struct JobSummary {
  bool operator==(const JobSummary &) const = default;

  QString id;
  QString label;
  QString state;
  int progress = -1;
};

struct DiagnosticSummary {
  bool operator==(const DiagnosticSummary &) const = default;

  QString id;
  QString severity;
  QString summary;
};

struct ProposalSummary {
  bool operator==(const ProposalSummary &) const = default;

  QString id;
  QString summary;
  QString state;
};

struct ProjectSummary {
  bool operator==(const ProjectSummary &) const = default;

  QString id;
  QString name;
  QString detail;
  QString modified;
  QString icon;
  QString workspaceId;
};

struct ProjectTemplateDescriptor {
  bool operator==(const ProjectTemplateDescriptor &) const = default;

  QString id;
  QString name;
  QString detail;
  QString icon;
  QString workspaceId;
};

struct RecoverySummary {
  bool operator==(const RecoverySummary &) const = default;

  QString id;
  QString name;
  QString detail;
  QString state;
  bool available = false;
};

struct OperationSummary {
  bool operator==(const OperationSummary &) const = default;

  QString id;
  QString name;
  QString kind;
  QString state;
  QString detail;
  int progress = -1;
};

struct InterfaceStateDescriptor {
  bool operator==(const InterfaceStateDescriptor &) const = default;

  QString id;
  QString label;
  QString icon;
};

struct FunctionPortSummary {
  bool operator==(const FunctionPortSummary &) const = default;

  QString id;
  QString label;
  QString type;
  QString value;
  QString state;
};

struct FunctionSummary {
  bool operator==(const FunctionSummary &) const = default;

  QString id;
  QString name;
  QString signature;
  QString sourcePath;
  QString language;
  QString recognition;
  QString revision;
  std::vector<FunctionPortSummary> inputs;
  std::vector<FunctionPortSummary> outputs;
};

struct SourceEditRequest {
  QString functionId;
  QString sourcePath;
  QString expectedRevision;
  QString source;
};

struct FrontendSnapshot {
  std::uint64_t generation = 0;
  QString projectName;
  QString branchLabel;
  QString revisionLabel;
  QString projectRevision;
  QString activeWorkspaceId;
  QString activeCommandId;
  QString viewportState;
  QString inspectorTitle;
  QString inspectorStatus;
  QString viewportHeadline;
  QString viewportDetail;
  QString modelHealth;
  QString selectionSummary;
  QString selectedEntityId;
  QString selectedSketchEntityId;
  std::vector<QString> selectedSketchEntityIds;
  QString agentStatus;
  QString modelSource;
  FunctionSummary selectedFunction;
  CommandDraftSummary commandDraft;
  QString defaultLengthUnitId;
  QString projectLengthUnitId;
  QString interfaceDensityId;
  QString gridPlaneLabel;
  QString gridSpacingLabel;
  double gridSpacingMillimeters = 10.0;
  bool sketchEditing = false;
  bool backendConnected = false;
  bool projectPersistenceAvailable = false;
  bool sourceEditingAvailable = false;
  bool canUndo = false;
  bool canRedo = false;
  std::vector<UiOption> lengthUnits;
  std::vector<PreferenceCategory> preferenceCategories;
  std::vector<PreferenceDescriptor> preferences;
  std::vector<WorkspaceDescriptor> workspaces;
  std::vector<CommandDescriptor> commands;
  std::vector<CommandDescriptor> commandCatalog;
  std::vector<StructureItem> structure;
  std::vector<RevisionSummary> revisions;
  std::vector<CommandDescriptor> historyCommands;
  std::vector<FieldDescriptor> fields;
  std::vector<ParameterSummary> parameters;
  std::vector<JobSummary> jobs;
  std::vector<DiagnosticSummary> diagnostics;
  std::vector<ProposalSummary> proposals;
  std::vector<ProjectSummary> recentProjects;
  std::vector<ProjectTemplateDescriptor> projectTemplates;
  std::vector<RecoverySummary> recoveryItems;
  std::vector<OperationSummary> operations;
  std::vector<InterfaceStateDescriptor> interfaceStates;
  SketchProjection sketchProjection;
  SketchInteractionSummary sketchInteraction;
  std::vector<PlanePoint> sketchInputPlanePoints;
  std::shared_ptr<const render::SketchSceneSnapshot> sketchScene;
};

using FrontendSnapshotPtr = std::shared_ptr<const FrontendSnapshot>;

class FrontendController {
public:
  using ChangeHandler = std::function<void()>;

  virtual ~FrontendController() = default;
  // Published generations are immutable and may be retained by asynchronous
  // UI consumers. Implementations return the same pointer until state changes.
  [[nodiscard]] virtual FrontendSnapshotPtr snapshot() const = 0;
  virtual void setChangeHandler(ChangeHandler handler) = 0;
  virtual void selectWorkspace(const QString &workspaceId) = 0;
  virtual void selectEntity(const QString &entityId) = 0;
  virtual void requestCommand(const QString &commandId) = 0;
  virtual void setPreference(const QString &preferenceId,
                             const PreferenceValue &value) = 0;
  virtual void replacePreferenceOptions(const QString &preferenceId,
                                        std::vector<UiOption> options,
                                        const QString &value) = 0;
  virtual void editField(const QString &fieldId, const FieldValue &value) = 0;
  virtual bool submitCommandDraft(const CommandDraftRequest &request,
                                  CommandDraftMode mode) = 0;
  virtual bool submitSketchInput(const SketchInputRequest &request) = 0;
  virtual bool removeLastSketchInput() = 0;
  virtual bool toggleSketchConstruction() = 0;
  virtual bool dragSketchCurve(const QString &entityId, PlanePoint first,
                               PlanePoint current) = 0;
  virtual bool previewSketchCurve(const QString &entityId, PlanePoint first,
                                  PlanePoint current) = 0;
  virtual void clearSketchCurvePreview() = 0;
  virtual void cancelCommandDraft(const QString &commandId) = 0;
  virtual bool submitParameterEdit(const ParameterEditRequest &request) = 0;
  virtual bool submitSourceEdit(const SourceEditRequest &request,
                                SourceEditMode mode) = 0;
  virtual bool undo() = 0;
  virtual bool redo() = 0;
};

} // namespace kearne::ui
