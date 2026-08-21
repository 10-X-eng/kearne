#include "desktop_controller.hpp"
#include "sketch_gesture_preview.hpp"

#include <QCoreApplication>
#include <QRandomGenerator>
#include <QSet>

#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <vector>

namespace {

using kearne::ui::CommandDescriptor;
using kearne::ui::CommandDraftMode;
using kearne::ui::CommandDraftRequest;
using kearne::ui::CommandDraftState;
using kearne::ui::FieldKind;
using kearne::ui::FieldValue;
using kearne::ui::FrontendController;
using kearne::ui::FrontendSnapshot;
using kearne::ui::ParameterEditRequest;
using kearne::ui::PreferenceKind;
using kearne::ui::PreferenceValue;
using kearne::ui::SketchInputKind;
using kearne::ui::SketchInputRequest;
using kearne::ui::SketchSelectionKind;
using kearne::ui::SourceEditMode;
using kearne::ui::SourceEditRequest;
using kearne::ui::UiOption;

void require(bool condition, const char *message) {
  if (!condition)
    throw std::runtime_error(message);
}

void verifyLengthBoundary() {
  QRandomGenerator random{0x6b656172U};
  for (int index = 0; index < 10'000; ++index) {
    const double millimeters =
        (static_cast<double>(random.generate64()) /
             static_cast<double>(std::numeric_limits<quint64>::max()) -
         0.5) *
        2.0e9;
    const double roundTrip = kearne::ui::millimetersFromMetres(
        kearne::ui::metresFromMillimeters(millimeters));
    require(std::abs(roundTrip - millimeters) <=
                std::numeric_limits<double>::epsilon() *
                    std::max(1.0, std::abs(millimeters)),
            "millimeter/SI conversion did not round trip");
  }
}

template <typename Range, typename Id>
void requireUniqueIds(const Range &range, Id id, const char *message) {
  QSet<QString> ids;
  for (const auto &value : range) {
    const QString identity = id(value);
    require(!identity.isEmpty(), message);
    require(!ids.contains(identity), message);
    ids.insert(identity);
  }
}

bool containsCommand(const std::vector<CommandDescriptor> &commands,
                     const QString &id) {
  return std::ranges::any_of(
      commands, [&id](const auto &command) { return command.id == id; });
}

bool commandAvailable(const std::vector<CommandDescriptor> &commands,
                      const QString &id) {
  const auto command = std::ranges::find_if(
      commands, [&id](const auto &candidate) { return candidate.id == id; });
  return command != commands.end() && command->available;
}

void verifyImmutablePublication(FrontendController &port) {
  const auto first = port.snapshot();
  const auto duplicate = port.snapshot();
  require(first && duplicate == first,
          "unchanged frontend state did not reuse its immutable snapshot");
  const std::uint64_t firstGeneration = first->generation;
  const QString firstWorkspace = first->activeWorkspaceId;

  port.selectWorkspace(QStringLiteral("sketch"));
  const auto changed = port.snapshot();
  require(changed && changed != first &&
              changed->generation > firstGeneration &&
              changed->activeWorkspaceId == QStringLiteral("sketch") &&
              first->generation == firstGeneration &&
              first->activeWorkspaceId == firstWorkspace,
          "frontend publication mutated or reused a prior generation");
  port.selectWorkspace(firstWorkspace);
}

void verifySnapshot(const FrontendSnapshot &snapshot) {
  require(snapshot.generation > 0, "frontend generation is zero");
  require(!snapshot.projectName.isEmpty(), "project name is empty");
  require(!snapshot.projectRevision.isEmpty(), "project revision is empty");
  require(!snapshot.activeWorkspaceId.isEmpty(), "workspace is empty");
  require(!snapshot.workspaces.empty(), "workspace registry is empty");
  require(!snapshot.commandCatalog.empty(), "command registry is empty");
  require(!snapshot.selectedFunction.id.isEmpty(),
          "selected function identity is empty");
  require(!snapshot.selectedFunction.signature.isEmpty(),
          "selected function signature is empty");
  require(!snapshot.selectedFunction.sourcePath.isEmpty(),
          "selected function source path is empty");
  requireUniqueIds(
      snapshot.workspaces, [](const auto &item) { return item.id; },
      "workspace identity is empty or duplicated");
  requireUniqueIds(
      snapshot.commandCatalog, [](const auto &item) { return item.id; },
      "command identity is empty or duplicated");
  requireUniqueIds(
      snapshot.structure, [](const auto &item) { return item.id; },
      "structure identity is empty or duplicated");
  requireUniqueIds(
      snapshot.revisions, [](const auto &item) { return item.id; },
      "revision identity is empty or duplicated");
  requireUniqueIds(
      snapshot.preferences, [](const auto &item) { return item.id; },
      "preference identity is empty or duplicated");
  requireUniqueIds(
      snapshot.parameters, [](const auto &item) { return item.id; },
      "parameter identity is empty or duplicated");
  requireUniqueIds(
      snapshot.fields, [](const auto &item) { return item.id; },
      "field identity is empty or duplicated");
  requireUniqueIds(
      snapshot.selectedFunction.inputs,
      [](const auto &item) { return item.id; },
      "function input identity is empty or duplicated");
  requireUniqueIds(
      snapshot.selectedFunction.outputs,
      [](const auto &item) { return item.id; },
      "function output identity is empty or duplicated");
  requireUniqueIds(
      snapshot.sketchProjection.primitives,
      [](const auto &item) { return item.id; },
      "sketch projection identity is empty or duplicated");
  require(!snapshot.sketchProjection.sourceRevision.isEmpty(),
          "sketch projection has no source revision");
  require(!snapshot.sketchProjection.functionId.isEmpty(),
          "sketch projection has no function identity");
  require(!snapshot.sketchProjection.planeId.isEmpty(),
          "sketch projection has no attachment plane");
  require(snapshot.gridSpacingMillimeters > 0.0,
          "grid projection has no positive canonical spacing");
  for (const auto &primitive : snapshot.sketchProjection.primitives) {
    require(!primitive.points.empty(),
            "sketch projection primitive has no points");
    for (const auto &point : primitive.points)
      require(std::isfinite(point.xMetres) && std::isfinite(point.yMetres),
              "sketch projection contains a non-finite point");
    require(primitive.pointKeys.size() == primitive.points.size(),
            "sketch projection point keys are incomplete");
    if (primitive.kind == kearne::ui::SketchPrimitiveKind::BSpline) {
      require(primitive.pointKeys.front() == QStringLiteral("start") &&
                  primitive.pointKeys.back() == QStringLiteral("end"),
              "B-spline semantic endpoints are missing");
    } else {
      requireUniqueIds(
          primitive.pointKeys, [](const auto &key) { return key; },
          "sketch projection point key is empty or duplicated");
    }
  }
  for (const CommandDescriptor &command : snapshot.commands) {
    require(containsCommand(snapshot.commandCatalog, command.id),
            "workspace command is absent from command registry");
    require(!command.label.isEmpty(), "command label is empty");
    require(!command.icon.isEmpty(), "command icon is empty");
    require(!command.group.isEmpty(), "command group is empty");
    require(command.available || !command.unavailableReason.isEmpty(),
            "unavailable command has no reason");
    require(!command.available || command.unavailableReason.isEmpty(),
            "available command has an unavailable reason");
    require(command.workspaceId == snapshot.activeWorkspaceId,
            "workspace command has the wrong context");
  }
}

void verifySketchInteractionContracts(FrontendController &port) {
  port.selectEntity(QStringLiteral("contract.entity"));
  port.selectWorkspace(QStringLiteral("sketch"));
  require(!port.snapshot()->sketchEditing &&
              commandAvailable(port.snapshot()->commands,
                               QStringLiteral("model.sketch.create")) &&
              !commandAvailable(port.snapshot()->commands,
                                QStringLiteral("sketch.rectangle")),
          "Sketch entry context exposed editing tools without an open Sketch");

  port.selectWorkspace(QStringLiteral("model"));
  port.selectEntity(QStringLiteral("function.mounting_profile"));
  port.selectWorkspace(QStringLiteral("sketch"));
  require(port.snapshot()->sketchEditing,
          "preselected Sketch did not enter its edit context");
  require(port.snapshot()->selectedFunction.id ==
              QStringLiteral("function.mounting_profile"),
          "Sketch edit context lost its selected source function");
  const auto commands = port.snapshot()->commands;
  for (const CommandDescriptor &command : commands) {
    if (!command.available)
      continue;
    port.requestCommand(command.id);
    FrontendSnapshot snapshot = *port.snapshot();
    const auto interaction = snapshot.sketchInteraction;
    require(
        interaction.commandId == command.id &&
            interaction.expectedRevision == snapshot.projectRevision &&
            interaction.inputKind != SketchInputKind::None &&
            interaction.minimumInputCount > 0 &&
            (interaction.maximumInputCount == 0 ||
             interaction.maximumInputCount >= interaction.minimumInputCount),
        "sketch command has no valid interaction descriptor");

    SketchInputRequest stale{
        command.id,
        QStringLiteral("stale.revision"),
        interaction.inputKind,
        {0.0, 0.0},
        snapshot.sketchProjection.primitives.front().id,
        {},
    };
    require(!port.submitSketchInput(stale), "stale sketch input was accepted");

    if (interaction.inputKind == SketchInputKind::Entity &&
        !interaction.selectionSequence.empty()) {
      const auto &primitive = snapshot.sketchProjection.primitives.front();
      SketchInputRequest wrongSelection{
          command.id,
          snapshot.projectRevision,
          SketchInputKind::Entity,
          {},
          primitive.id,
          interaction.selectionSequence.front() == SketchSelectionKind::Point
              ? QString{}
              : primitive.pointKeys.front(),
      };
      if (interaction.selectionSequence.front() != SketchSelectionKind::Any)
        require(!port.submitSketchInput(wrongSelection),
                "sketch input ignored its selection predicate");
    }

    std::size_t entityIndex = 0U;
    for (int index = 0; index < interaction.minimumInputCount; ++index) {
      snapshot = *port.snapshot();
      const auto &currentInteraction = snapshot.sketchInteraction;
      SketchInputRequest input{
          command.id,
          snapshot.projectRevision,
          currentInteraction.inputKind,
          {0.01 * index, 0.01 * (index + 1)},
          {},
          {},
      };
      if (currentInteraction.inputKind == SketchInputKind::Entity) {
        require(entityIndex < snapshot.sketchProjection.primitives.size(),
                "sketch fixture has too few selectable entities");
        input.entityId = snapshot.sketchProjection.primitives[entityIndex++].id;
        const auto &sequence = currentInteraction.selectionSequence;
        const SketchSelectionKind selection =
            sequence.empty()
                ? SketchSelectionKind::Any
                : sequence[std::min(
                      static_cast<std::size_t>(currentInteraction.inputCount),
                      sequence.size() - 1)];
        if (selection == SketchSelectionKind::Point)
          input.subElementKey =
              snapshot.sketchProjection.primitives[index].pointKeys.front();
      }
      require(port.submitSketchInput(input), "valid sketch input was rejected");
      snapshot = *port.snapshot();
      require(snapshot.sketchInteraction.inputCount == index + 1,
              "sketch input count was not projected");
      const bool ready = index + 1 >= interaction.minimumInputCount;
      require(snapshot.commandDraft.previewSupported == ready &&
                  snapshot.commandDraft.applySupported == ready,
              "sketch draft readiness disagrees with its descriptor");
    }

    snapshot = *port.snapshot();
    CommandDraftRequest draft{
        command.id, snapshot.commandDraft.baseRevision, {}};
    for (const auto &field : snapshot.fields)
      draft.fields.push_back({field.id, field.value});
    require(port.submitCommandDraft(draft, CommandDraftMode::Preview) &&
                port.snapshot()->commandDraft.state ==
                    CommandDraftState::Preview,
            "ready sketch draft did not use the command preview path");
    require(port.submitCommandDraft(draft, CommandDraftMode::Apply) &&
                port.snapshot()->commandDraft.state ==
                    CommandDraftState::Unavailable,
            "ready sketch draft did not use the command apply path");

    port.cancelCommandDraft(command.id);
    snapshot = *port.snapshot();
    require(snapshot.sketchInteraction.inputKind == SketchInputKind::None &&
                std::ranges::none_of(snapshot.sketchProjection.primitives,
                                     [](const auto &primitive) {
                                       return primitive.draft ||
                                              primitive.selected;
                                     }),
            "cancel retained ephemeral sketch input");
  }
  port.selectWorkspace(QStringLiteral("model"));
  require(!port.snapshot()->sketchEditing &&
              port.snapshot()->selectedFunction.id ==
                  QStringLiteral("function.mounting_profile"),
          "workspace navigation changed the selected design object");
}

void verifyParameterEditing(FrontendController &port) {
  const FrontendSnapshot before = *port.snapshot();
  require(!before.parameters.empty(), "parameter projection is empty");
  const auto parameter = before.parameters.front();
  const ParameterEditRequest request{
      parameter.id,
      before.projectRevision,
      QStringLiteral("120 mm"),
  };
  require(port.submitParameterEdit(request),
          "valid parameter edit was rejected");
  const FrontendSnapshot applied = *port.snapshot();
  const auto updated = std::ranges::find_if(
      applied.parameters, [&parameter](const auto &candidate) {
        return candidate.id == parameter.id;
      });
  require(updated != applied.parameters.end() &&
              updated->expression == QStringLiteral("120 mm") &&
              updated->value == QStringLiteral("pending evaluation") &&
              applied.projectRevision != before.projectRevision &&
              applied.selectedFunction.revision ==
                  before.selectedFunction.revision,
          "parameter edit was not projected atomically");
  require(!port.submitParameterEdit(request),
          "stale parameter edit was accepted");
  require(!port.submitParameterEdit({QStringLiteral("unknown"),
                                     applied.selectedFunction.revision,
                                     QStringLiteral("1 mm")}),
          "unknown parameter edit was accepted");
}

void verifyDeclaredTransitions(FrontendController &port) {
  FrontendSnapshot snapshot = *port.snapshot();
  verifySnapshot(snapshot);

  std::vector<QString> workspaceIds;
  workspaceIds.reserve(snapshot.workspaces.size());
  for (const auto &workspace : snapshot.workspaces)
    workspaceIds.push_back(workspace.id);
  for (const QString &workspaceId : workspaceIds) {
    const std::uint64_t before = port.snapshot()->generation;
    port.selectWorkspace(workspaceId);
    snapshot = *port.snapshot();
    require(snapshot.activeWorkspaceId == workspaceId,
            "declared workspace could not be selected");
    require(snapshot.generation >= before,
            "workspace selection moved generation backward");
    verifySnapshot(snapshot);
    std::vector<CommandDescriptor> commands = snapshot.commands;
    for (const CommandDescriptor &command : commands) {
      const std::uint64_t commandBefore = snapshot.generation;
      port.requestCommand(command.id);
      snapshot = *port.snapshot();
      require(snapshot.generation > commandBefore,
              "declared command did not advance the projection");
      if (command.available) {
        require(snapshot.activeCommandId == command.id &&
                    snapshot.commandDraft.commandId == command.id,
                "available command exposed no typed draft");
      } else {
        require(snapshot.activeCommandId.isEmpty() && snapshot.fields.empty() &&
                    snapshot.commandDraft.state == CommandDraftState::None,
                "unavailable command created a draft");
      }
      verifySnapshot(snapshot);
    }
  }

  snapshot = *port.snapshot();
  const std::uint64_t invalidBefore = snapshot.generation;
  const QString workspaceBefore = snapshot.activeWorkspaceId;
  port.selectWorkspace(QStringLiteral("not-a-workspace"));
  snapshot = *port.snapshot();
  require(snapshot.generation == invalidBefore &&
              snapshot.activeWorkspaceId == workspaceBefore,
          "unknown workspace changed projection state");

  port.selectEntity(QStringLiteral("contract.entity"));
  snapshot = *port.snapshot();
  require(snapshot.selectionSummary == QStringLiteral("Model geometry"),
          "unknown entity identity leaked into the human selection summary");
  require(!snapshot.fields.empty() &&
              std::ranges::none_of(snapshot.fields,
                                   [](const auto &field) {
                                     return field.label ==
                                                QStringLiteral("Identity") ||
                                            field.label ==
                                                QStringLiteral("Revision");
                                   }),
          "entity selection exposed machine-facing inspector fields");
}

void verifyCommandDraftContracts(FrontendController &port) {
  const std::vector<CommandDescriptor> catalog =
      port.snapshot()->commandCatalog;
  for (const CommandDescriptor &command : catalog) {
    if (!command.available)
      continue;
    port.requestCommand(command.id);
    FrontendSnapshot snapshot = *port.snapshot();
    require(snapshot.commandDraft.state == CommandDraftState::Editing &&
                snapshot.commandDraft.commandId == command.id &&
                !snapshot.commandDraft.baseRevision.isEmpty() &&
                snapshot.activeWorkspaceId == command.workspaceId,
            "command draft did not enter editing state");
    requireUniqueIds(
        snapshot.fields, [](const auto &field) { return field.id; },
        "command field identity is empty or duplicated");
    const auto fields = snapshot.fields;
    for (const auto &field : fields) {
      require((field.kind == FieldKind::Toggle) ==
                  std::holds_alternative<bool>(field.value),
              "command field value has the wrong type");
      if (field.readOnly)
        continue;
      FieldValue replacement = field.value;
      if (field.kind == FieldKind::Toggle) {
        replacement = !std::get<bool>(field.value);
      } else if (field.kind == FieldKind::Choice) {
        const QString current = std::get<QString>(field.value);
        const auto alternate = std::ranges::find_if(
            field.options, [&current](const UiOption &option) {
              return option.id != current;
            });
        if (alternate == field.options.end())
          continue;
        replacement = alternate->id;
      } else {
        replacement =
            std::get<QString>(field.value) + QStringLiteral(" contract");
      }
      port.editField(field.id, replacement);
      snapshot = *port.snapshot();
      const auto updated = std::ranges::find_if(
          snapshot.fields,
          [&field](const auto &candidate) { return candidate.id == field.id; });
      require(updated != snapshot.fields.end() &&
                  updated->value == replacement &&
                  snapshot.commandDraft.state == CommandDraftState::Editing,
              "command field rejected a declared value");
    }

    snapshot = *port.snapshot();
    CommandDraftRequest request{
        command.id, snapshot.commandDraft.baseRevision, {}};
    request.fields.reserve(snapshot.fields.size());
    for (const auto &field : snapshot.fields)
      request.fields.push_back({field.id, field.value});
    if (snapshot.commandDraft.previewSupported) {
      require(port.submitCommandDraft(request, CommandDraftMode::Preview),
              "declared command preview was rejected");
      require(port.snapshot()->commandDraft.state == CommandDraftState::Preview,
              "command preview state was not projected");
    } else {
      require(!port.submitCommandDraft(request, CommandDraftMode::Preview),
              "unsupported command preview was accepted");
    }
    if (snapshot.commandDraft.applySupported) {
      require(port.submitCommandDraft(request, CommandDraftMode::Apply),
              "declared command apply was rejected");
      require(port.snapshot()->commandDraft.state ==
                  CommandDraftState::Unavailable,
              "disconnected command apply state was not projected");
    }
    port.cancelCommandDraft(command.id);
    require(port.snapshot()->activeCommandId.isEmpty(),
            "command draft did not cancel");
  }
}

void verifySettings(FrontendController &port) {
  FrontendSnapshot snapshot = *port.snapshot();
  std::vector<QString> preferenceIds;
  preferenceIds.reserve(snapshot.preferences.size());
  for (const auto &preference : snapshot.preferences)
    preferenceIds.push_back(preference.id);
  for (const QString &preferenceId : preferenceIds) {
    const auto currentPreference = std::ranges::find_if(
        snapshot.preferences, [&preferenceId](const auto &candidate) {
          return candidate.id == preferenceId;
        });
    require(currentPreference != snapshot.preferences.end(),
            "preference disappeared from projection");
    if (!currentPreference->enabled)
      continue;
    PreferenceValue replacement = currentPreference->value;
    if (currentPreference->kind == PreferenceKind::Choice) {
      const auto current = std::get<QString>(currentPreference->value);
      const auto alternate = std::ranges::find_if(
          currentPreference->options,
          [&current](const UiOption &option) { return option.id != current; });
      if (alternate == currentPreference->options.end())
        continue;
      replacement = alternate->id;
    } else if (currentPreference->kind == PreferenceKind::Toggle) {
      replacement = !std::get<bool>(currentPreference->value);
    } else {
      replacement =
          std::get<QString>(currentPreference->value) + QStringLiteral("1");
    }
    const std::uint64_t before = snapshot.generation;
    port.setPreference(preferenceId, replacement);
    snapshot = *port.snapshot();
    const auto updated = std::ranges::find_if(
        snapshot.preferences, [&preferenceId](const auto &candidate) {
          return candidate.id == preferenceId;
        });
    require(updated != snapshot.preferences.end() &&
                updated->value == replacement && snapshot.generation > before,
            "enabled preference rejected a valid declared value");
  }
}

void verifySourceEditing(FrontendController &port) {
  FrontendSnapshot before = *port.snapshot();
  require(before.sourceEditingAvailable,
          "capture controller does not expose source editing");
  const QString proposed =
      before.modelSource + QStringLiteral("\n# contract revision\n");
  const SourceEditRequest request{
      before.selectedFunction.id,
      before.selectedFunction.sourcePath,
      before.selectedFunction.revision,
      proposed,
  };
  require(port.submitSourceEdit(request, SourceEditMode::Preview),
          "valid source preview was rejected");
  FrontendSnapshot preview = *port.snapshot();
  require(preview.modelSource == before.modelSource,
          "source preview mutated canonical source");
  require(preview.selectedFunction.revision == before.selectedFunction.revision,
          "source preview advanced the revision");

  require(port.submitSourceEdit(request, SourceEditMode::Apply),
          "valid source revision was rejected");
  FrontendSnapshot applied = *port.snapshot();
  require(applied.modelSource == proposed &&
              applied.selectedFunction.revision !=
                  before.selectedFunction.revision &&
              applied.projectRevision != before.projectRevision,
          "source apply did not replace source and revision atomically");
  require(!port.submitSourceEdit(request, SourceEditMode::Apply),
          "stale source revision was accepted");
  require(port.snapshot()->modelSource == proposed,
          "stale source rejection changed canonical source");
}

void verifyGesturePreview() {
  kearne::ui::SketchGesturePreview preview;
  int changes = 0;
  QObject::connect(&preview, &kearne::ui::SketchGesturePreview::previewChanged,
                   [&changes] { ++changes; });
  const std::array dragPoints{QPointF{0.0, 0.0}, QPointF{40.0, 25.0}};
  require(!preview.updateGesture(QStringLiteral("sketch.unknown"), dragPoints,
                                 false) &&
              !preview.visible() && changes == 0,
          "unknown gesture preview changed transient state");
  require(preview.updateGesture(QStringLiteral("sketch.circle"), dragPoints,
                                false, QStringLiteral("center-radius")) &&
              preview.primitives().size() == 1U &&
              preview.primitives().front().kind ==
                  kearne::ui::SketchPrimitiveKind::Circle &&
              preview.measurements().size() == 1U &&
              changes == 1,
          "circle drag preview did not use shared provisional geometry");
  preview.clear();
  const std::array rectanglePoints{QPointF{-40.0, -25.0},
                                   QPointF{40.0, 25.0}};
  require(preview.updateGesture(QStringLiteral("sketch.rectangle"),
                                rectanglePoints, true,
                                QStringLiteral("corner")) &&
              preview.visible() && preview.construction() &&
              std::ranges::equal(preview.inputPoints(), rectanglePoints) &&
              preview.primitives().size() == 4U &&
              preview.measurements().size() == 2U && changes == 3,
          "rectangle gesture preview did not project geometry and sizes");
  require(preview.updateGesture(QStringLiteral("sketch.rectangle"),
                                rectanglePoints, true,
                                QStringLiteral("corner")) &&
              changes == 3,
          "unchanged gesture preview emitted redundant work");
  preview.clear();
  require(!preview.visible() && preview.primitives().empty() &&
              preview.measurements().empty() && changes == 4,
          "gesture preview did not clear exactly once");
}

} // namespace

int main(int argc, char *argv[]) {
  try {
    QCoreApplication application(argc, argv);
    std::vector<UiOption> themes{
        {QStringLiteral("system"), QStringLiteral("System")},
        {QStringLiteral("light"), QStringLiteral("Light")},
        {QStringLiteral("dark"), QStringLiteral("Dark")},
    };
    auto port = kearne::ui::makeCaptureDesktopController(
        std::move(themes), QStringLiteral("light"), QStringLiteral("mm"),
        QStringLiteral("compact"), QStringLiteral("fusion"),
        QStringLiteral("standard"));
    verifyLengthBoundary();
    verifyImmutablePublication(*port);
    verifyDeclaredTransitions(*port);
    verifyCommandDraftContracts(*port);
    verifySketchInteractionContracts(*port);
    verifySettings(*port);
    verifyParameterEditing(*port);
    verifySourceEditing(*port);
    verifyGesturePreview();
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
