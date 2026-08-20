#pragma once

#include <QString>

#include <cstdint>
#include <memory>
#include <utility>
#include <variant>
#include <vector>

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
  QString id;
  QString label;
  QString icon;
};

struct PreferenceDescriptor {
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
enum class SketchPrimitiveKind { Point, Line, Circle, Arc };
enum class SketchSelectionKind { Any, Point, Curve };
enum class CommandDraftState {
  None,
  Editing,
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

  QString id;
  QString label;
  FieldKind kind = FieldKind::Text;
  FieldValue value;
  QString effectiveValue;
  std::vector<UiOption> options;
  bool readOnly = false;
};

struct CommandDescriptor {
  QString id;
  QString label;
  QString icon;
  QString group;
  QString workspaceId;
  QString shortcut;
  bool available = true;
  QString unavailableReason;
};

struct CommandDraftSummary {
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
  double xMetres = 0.0;
  double yMetres = 0.0;
};

struct SketchPrimitiveProjection {
  QString id;
  SketchPrimitiveKind kind = SketchPrimitiveKind::Point;
  std::vector<PlanePoint> points;
  std::vector<QString> pointKeys;
  std::vector<QString> selectedPointKeys;
  double radiusMetres = 0.0;
  bool construction = false;
  bool selected = false;
  bool draft = false;
};

struct SketchProjection {
  QString sourceRevision;
  QString functionId;
  QString planeId;
  QString coordinateUnitId;
  QString solveStatus;
  int degreesOfFreedom = -1;
  std::vector<SketchPrimitiveProjection> primitives;
};

struct SketchInteractionSummary {
  QString commandId;
  QString expectedRevision;
  SketchInputKind inputKind = SketchInputKind::None;
  int minimumInputCount = 0;
  int maximumInputCount = 0;
  int inputCount = 0;
  QString prompt;
  std::vector<SketchSelectionKind> selectionSequence;
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
  QString id;
  QString label;
  QString icon;
};

struct StructureItem {
  QString id;
  QString label;
  int depth = 0;
  QString kind;
};

struct RevisionSummary {
  QString id;
  QString label;
  QString detail;
};

struct ParameterSummary {
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
  QString id;
  QString label;
  QString state;
  int progress = -1;
};

struct DiagnosticSummary {
  QString id;
  QString severity;
  QString summary;
};

struct ProposalSummary {
  QString id;
  QString summary;
  QString state;
};

struct ProjectSummary {
  QString id;
  QString name;
  QString detail;
  QString modified;
  QString icon;
  QString workspaceId;
};

struct ProjectTemplateDescriptor {
  QString id;
  QString name;
  QString detail;
  QString icon;
  QString workspaceId;
};

struct RecoverySummary {
  QString id;
  QString name;
  QString detail;
  QString state;
  bool available = false;
};

struct OperationSummary {
  QString id;
  QString name;
  QString kind;
  QString state;
  QString detail;
  int progress = -1;
};

struct InterfaceStateDescriptor {
  QString id;
  QString label;
  QString icon;
};

struct FunctionPortSummary {
  QString id;
  QString label;
  QString type;
  QString value;
  QString state;
};

struct FunctionSummary {
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
  bool backendConnected = false;
  bool sourceEditingAvailable = false;
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
};

using FrontendSnapshotPtr = std::shared_ptr<const FrontendSnapshot>;

class FrontendPort {
public:
  virtual ~FrontendPort() = default;
  // Published generations are immutable and may be retained by asynchronous
  // UI consumers. Implementations return the same pointer until state changes.
  [[nodiscard]] virtual FrontendSnapshotPtr snapshot() const = 0;
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
  virtual void cancelCommandDraft(const QString &commandId) = 0;
  virtual bool submitParameterEdit(const ParameterEditRequest &request) = 0;
  virtual bool submitSourceEdit(const SourceEditRequest &request,
                                SourceEditMode mode) = 0;
};

} // namespace kearne::ui
