#pragma once

#include "frontend_contract.hpp"

#include <algorithm>
#include <array>
#include <iterator>
#include <span>
#include <string_view>
#include <vector>

namespace kearne::ui {

struct CommandDefinition {
  constexpr CommandDefinition(std::string_view commandId,
                              std::string_view commandLabel,
                              std::string_view commandIcon,
                              std::string_view commandGroup,
                              bool commandImplemented = false,
                              std::string_view commandMenu = {},
                              bool commandPrimary = false)
      : id(commandId), label(commandLabel), icon(commandIcon),
        group(commandGroup), implemented(commandImplemented), menu(commandMenu),
        primary(commandPrimary) {}

  std::string_view id;
  std::string_view label;
  std::string_view icon;
  std::string_view group;
  bool implemented = false;
  std::string_view menu;
  bool primary = false;
};

inline constexpr std::array modelCommandDefinitions{
    CommandDefinition{"model.sketch.create", "New Sketch", "sketch", "Create",
                      true, "Sketch", true},
    CommandDefinition{"model.extrude", "Extrude", "extrude", "Create", true,
                      "Create", true},
    CommandDefinition{"model.revolve", "Revolve", "revolve", "Create"},
    CommandDefinition{"model.sweep", "Sweep", "path", "Create"},
    CommandDefinition{"model.loft", "Loft", "layers", "Create"},
    CommandDefinition{"model.fillet", "Fillet", "round", "Modify"},
    CommandDefinition{"model.chamfer", "Chamfer", "chamfer", "Modify"},
    CommandDefinition{"model.shell", "Shell", "shell", "Modify"},
    CommandDefinition{"model.hole", "Hole", "hole", "Modify"},
    CommandDefinition{"model.pattern", "Pattern", "grid", "Transform"},
    CommandDefinition{"model.mirror", "Mirror", "mirror", "Transform"},
    CommandDefinition{"model.plane.create", "Plane", "plane", "Reference",
                      true},
    CommandDefinition{"model.material.assign", "Material", "material",
                      "Inspect"},
    CommandDefinition{"inspect.measure", "Measure", "measure", "Inspect"},
    CommandDefinition{"inspect.section", "Section", "section", "Inspect"},
};

inline constexpr std::array sketchCommandDefinitions{
    CommandDefinition{"sketch.point", "Point", "point", "Geometry", true,
                      "Draw"},
    CommandDefinition{"sketch.line", "Line", "line", "Geometry", true, "Draw",
                      true},
    CommandDefinition{"sketch.polyline", "Polyline", "polyline", "Geometry",
                      true, "Draw"},
    CommandDefinition{"sketch.polygon", "Polygon", "polygon", "Geometry", true,
                      "Shapes"},
    CommandDefinition{"sketch.rectangle", "Rectangle", "rectangle", "Geometry",
                      true, "Shapes", true},
    CommandDefinition{"sketch.circle", "Circle", "circle", "Geometry", true,
                      "Circles", true},
    CommandDefinition{"sketch.arc", "Arc", "arc", "Geometry", true, "Circles"},
    CommandDefinition{"sketch.ellipse", "Ellipse", "ellipse", "Geometry", true,
                      "Conics", true},
    CommandDefinition{"sketch.elliptical-arc", "Elliptical Arc",
                      "elliptical-arc", "Geometry", true, "Conics"},
    CommandDefinition{"sketch.hyperbolic-arc", "Hyperbolic Arc",
                      "hyperbolic-arc", "Geometry", true, "Conics"},
    CommandDefinition{"sketch.parabolic-arc", "Parabolic Arc", "parabolic-arc",
                      "Geometry", true, "Conics"},
    CommandDefinition{"sketch.bspline.control-points", "B-spline",
                      "bspline-control", "Geometry", true, "Splines", true},
    CommandDefinition{"sketch.bspline.periodic-control-points",
                      "Periodic B-spline", "bspline-periodic", "Geometry", true,
                      "Splines"},
    CommandDefinition{"sketch.bspline.interpolation", "Interpolated B-spline",
                      "bspline-interpolate", "Geometry", true, "Splines"},
    CommandDefinition{"sketch.bspline.periodic-interpolation",
                      "Periodic interpolated B-spline",
                      "bspline-periodic-interpolate", "Geometry", true,
                      "Splines"},
    CommandDefinition{"sketch.bspline.increase-degree", "Increase Degree",
                      "bspline-degree-up", "B-spline", true, "Spline Edit",
                      true},
    CommandDefinition{"sketch.bspline.decrease-degree", "Decrease Degree",
                      "bspline-degree-down", "B-spline", true, "Spline Edit"},
    CommandDefinition{"sketch.bspline.increase-knot-multiplicity",
                      "Increase Knot Multiplicity", "bspline-knot-up",
                      "B-spline", true, "Spline Edit"},
    CommandDefinition{"sketch.bspline.decrease-knot-multiplicity",
                      "Decrease Knot Multiplicity", "bspline-knot-down",
                      "B-spline", true, "Spline Edit"},
    CommandDefinition{"sketch.bspline.insert-knot", "Insert Knot",
                      "bspline-knot-insert", "B-spline", true, "Spline Edit"},
    CommandDefinition{"sketch.bspline.pole-weight", "Pole Weight",
                      "bspline-pole-weight", "B-spline", true, "Spline Edit"},
    CommandDefinition{"sketch.bspline.control-polygon", "Control Polygon",
                      "bspline-control-polygon", "B-spline", true,
                      "Spline Edit"},
    CommandDefinition{"sketch.bspline.curvature-comb", "Curvature Comb",
                      "bspline-curvature-comb", "B-spline", true,
                      "Spline Edit"},
    CommandDefinition{"sketch.bspline.degree-labels", "Degree Labels",
                      "bspline-degree-label", "B-spline", true,
                      "Spline Edit"},
    CommandDefinition{"sketch.bspline.knot-labels", "Knot Multiplicity",
                      "bspline-knot-label", "B-spline", true,
                      "Spline Edit"},
    CommandDefinition{"sketch.bspline.weight-labels", "Pole Weights",
                      "bspline-weight-label", "B-spline", true,
                      "Spline Edit"},
    CommandDefinition{"sketch.bspline.convert-to-nurbs", "Convert to NURBS",
                      "convert-to-nurbs", "B-spline", true, "Spline Edit"},
    CommandDefinition{"sketch.slot", "Slot", "slot", "Geometry", true, "Slots",
                      true},
    CommandDefinition{"sketch.arc-slot", "Arc Slot", "arc-slot", "Geometry",
                      true, "Slots"},
    CommandDefinition{"sketch.oblong", "Oblong", "oblong", "Geometry", true,
                      "Slots"},
    CommandDefinition{"sketch.dimension", "Dimension", "dimension", "Constrain",
                      true, "Dimension", true},
    CommandDefinition{"sketch.coincident", "Coincident", "coincident",
                      "Constrain", true, "Relate", true},
    CommandDefinition{"sketch.horizontal", "Horizontal", "horizontal",
                      "Constrain", true, "Align", true},
    CommandDefinition{"sketch.vertical", "Vertical", "vertical", "Constrain",
                      true, "Align"},
    CommandDefinition{"sketch.horizontal-vertical", "Horizontal / Vertical",
                      "horizontal-vertical", "Constrain", true, "Align"},
    CommandDefinition{"sketch.equal", "Equal", "equal", "Constrain", true,
                      "Relate"},
    CommandDefinition{"sketch.parallel", "Parallel", "parallel", "Constrain",
                      true, "Relate"},
    CommandDefinition{"sketch.perpendicular", "Perpendicular", "perpendicular",
                      "Constrain", true, "Relate"},
    CommandDefinition{"sketch.tangent", "Tangent", "tangent", "Constrain", true,
                      "Relate"},
    CommandDefinition{"sketch.concentric", "Concentric", "concentric",
                      "Constrain", true, "Relate"},
    CommandDefinition{"sketch.midpoint", "Midpoint", "midpoint", "Constrain",
                      true, "Relate"},
    CommandDefinition{"sketch.point-on-object", "Point on object", "point",
                      "Constrain", true, "Relate"},
    CommandDefinition{"sketch.symmetric", "Symmetric", "symmetric", "Constrain",
                      true, "Relate"},
    CommandDefinition{"sketch.lock", "Lock", "lock", "Constrain", true, "Fix",
                      true},
    CommandDefinition{"sketch.block", "Block", "block", "Constrain", true,
                      "Fix"},
    CommandDefinition{"sketch.group", "Group", "group", "Constrain", true,
                      "Fix"},
    CommandDefinition{"sketch.collinear", "Collinear", "collinear", "Constrain",
                      true, "Align"},
    CommandDefinition{"sketch.translate", "Translate", "translate", "Modify",
                      true, "Transform", true},
    CommandDefinition{"sketch.rotate", "Rotate", "rotate", "Modify", true,
                      "Transform"},
    CommandDefinition{"sketch.scale", "Scale", "scale", "Modify", true,
                      "Transform"},
    CommandDefinition{"sketch.symmetry", "Symmetry", "symmetry-transform",
                      "Modify", true, "Transform"},
    CommandDefinition{"sketch.fillet", "Fillet", "round", "Modify", true,
                      "Modify", true},
    CommandDefinition{"sketch.chamfer", "Chamfer", "chamfer", "Modify", true,
                      "Modify"},
    CommandDefinition{"sketch.offset", "Offset", "offset", "Modify", true,
                      "Modify"},
    CommandDefinition{"sketch.extend", "Extend", "extend", "Modify", true,
                      "Modify"},
    CommandDefinition{"sketch.trim", "Trim", "trim", "Modify", true,
                      "Modify"},
    CommandDefinition{"sketch.split", "Split", "split", "Modify", true,
                      "Modify"},
    CommandDefinition{"sketch.join", "Join", "join-curves", "Modify", true,
                      "Modify"},
    CommandDefinition{"sketch.remove-axis-alignment", "Remove Axis Alignment",
                      "remove-axis-alignment", "Modify", true, "Modify"},
};

inline constexpr std::array editSketchCommandDefinitions{
    CommandDefinition{"sketch.edit", "Edit Sketch", "edit", "Edit", true}};

inline constexpr std::array assemblyCommandDefinitions{
    CommandDefinition{"assembly.insert", "Insert", "add", "Components"},
    CommandDefinition{"assembly.fastener.insert", "Fastener", "fastener",
                      "Components"},
    CommandDefinition{"assembly.material.assign", "Material", "material",
                      "Components"},
    CommandDefinition{"assembly.joint", "Joint", "joint", "Relationships"},
    CommandDefinition{"assembly.fastened", "Fastened", "fastener",
                      "Relationships"},
    CommandDefinition{"assembly.revolute", "Revolute", "revolve",
                      "Relationships"},
    CommandDefinition{"assembly.slider", "Slider", "path", "Relationships"},
    CommandDefinition{"assembly.drive", "Drive", "play", "Motion"},
    CommandDefinition{"assembly.interference", "Interference", "interference",
                      "Inspect"}};
inline constexpr std::array sheetMetalCommandDefinitions{
    CommandDefinition{"sheet-metal.base", "Base flange", "sheet-metal",
                      "Create"},
    CommandDefinition{"sheet-metal.flange", "Flange", "sheet-metal", "Create"},
    CommandDefinition{"sheet-metal.bend", "Bend", "bend", "Create"},
    CommandDefinition{"sheet-metal.hem", "Hem", "bend", "Create"},
    CommandDefinition{"sheet-metal.relief", "Corner relief", "trim", "Modify"},
    CommandDefinition{"sheet-metal.rip", "Rip", "section", "Modify"},
    CommandDefinition{"sheet-metal.unfold", "Unfold", "unfold", "Review"},
    CommandDefinition{"sheet-metal.refold", "Refold", "mirror", "Review"}};
inline constexpr std::array simulationCommandDefinitions{
    CommandDefinition{"simulation.study", "Study", "simulate", "Setup"},
    CommandDefinition{"simulation.material", "Material", "material", "Setup"},
    CommandDefinition{"simulation.constraint", "Constraint", "lock", "Loads"},
    CommandDefinition{"simulation.load", "Load", "target", "Loads"},
    CommandDefinition{"simulation.mesh", "Mesh", "mesh", "Solve"},
    CommandDefinition{"simulation.solve", "Solve", "solve", "Solve"},
    CommandDefinition{"simulation.results", "Results", "chart", "Review"}};
inline constexpr std::array drawingCommandDefinitions{
    CommandDefinition{"drawing.sheet", "Sheet", "sheet", "Create"},
    CommandDefinition{"drawing.base_view", "Base view", "view", "Views"},
    CommandDefinition{"drawing.projected_view", "Projected", "layers", "Views"},
    CommandDefinition{"drawing.section_view", "Section", "section", "Views"},
    CommandDefinition{"drawing.dimension", "Dimension", "dimension",
                      "Annotate"},
    CommandDefinition{"drawing.gdt", "GD&T", "target", "Annotate"}};
inline constexpr std::array bomCommandDefinitions{
    CommandDefinition{"bom.refresh", "Refresh", "refresh", "Table"},
    CommandDefinition{"bom.columns", "Columns", "columns", "Table"},
    CommandDefinition{"bom.balloon", "Balloon", "circle", "Annotate"},
    CommandDefinition{"bom.export", "Export", "export", "Output"}};
inline constexpr std::array camCommandDefinitions{
    CommandDefinition{"cam.setup", "Setup", "cam", "Setup"},
    CommandDefinition{"cam.stock", "Stock", "stock", "Setup"},
    CommandDefinition{"cam.face", "Face", "face", "2D"},
    CommandDefinition{"cam.contour", "Contour", "contour", "2D"},
    CommandDefinition{"cam.pocket", "Pocket", "pocket", "2D"},
    CommandDefinition{"cam.drill", "Drill", "drill", "Hole"},
    CommandDefinition{"cam.adaptive", "Adaptive", "toolpath", "3D"},
    CommandDefinition{"cam.simulate", "Simulate", "play", "Verify"},
    CommandDefinition{"cam.post", "Post", "export", "Output"}};
inline constexpr std::array versionCommandDefinitions{
    CommandDefinition{"version.undo", "Undo", "undo", "History"},
    CommandDefinition{"version.redo", "Redo", "redo", "History"},
    CommandDefinition{"version.checkpoint", "Checkpoint", "checkpoint",
                      "History"},
    CommandDefinition{"version.branch", "Branch", "branch", "History"},
    CommandDefinition{"version.compare", "Compare", "compare", "Review"},
    CommandDefinition{"version.merge", "Merge", "merge", "Review"}};
inline constexpr std::array agentCommandDefinitions{
    CommandDefinition{"agent.ask", "Ask Kearne", "agent", "Agent"},
    CommandDefinition{"agent.inspect", "Inspect context", "inspect", "Agent"},
    CommandDefinition{"agent.plan", "Plan change", "plan", "Agent"},
    CommandDefinition{"agent.review", "Review change", "review", "Agent"}};

[[nodiscard]] inline QString commandUnavailableReason(const QString &id) {
  if (id.startsWith(QStringLiteral("assembly.")) ||
      id.startsWith(QStringLiteral("sheet-metal.")) ||
      id.startsWith(QStringLiteral("simulation.")) ||
      id.startsWith(QStringLiteral("cam.")) ||
      id.startsWith(QStringLiteral("drawing.")) ||
      id.startsWith(QStringLiteral("bom.")))
    return QStringLiteral("Planned after the parametric-part MVP");
  return QStringLiteral("Not implemented in this build");
}

[[nodiscard]] inline std::vector<CommandDescriptor>
commandRecords(std::span<const CommandDefinition> definitions,
               const QString &workspaceId) {
  std::vector<CommandDescriptor> result;
  result.reserve(definitions.size());
  for (const auto &definition : definitions) {
    const QString id =
        QString::fromLatin1(definition.id.data(), definition.id.size());
    result.push_back(
        {id,
         QString::fromLatin1(definition.label.data(), definition.label.size()),
         QString::fromLatin1(definition.icon.data(), definition.icon.size()),
         QString::fromLatin1(definition.group.data(), definition.group.size()),
         definition.menu.empty() ? QString::fromLatin1(definition.group.data(),
                                                       definition.group.size())
                                 : QString::fromLatin1(definition.menu.data(),
                                                       definition.menu.size()),
         workspaceId,
         {},
         definition.primary,
         definition.implemented,
         false,
         definition.implemented ? QString{} : commandUnavailableReason(id)});
  }
  return result;
}

[[nodiscard]] inline bool hasFrontendCommandContractId(const QString &id) {
  const auto implemented = [&id](const auto &definitions) {
    return std::ranges::any_of(definitions, [&id](const auto &definition) {
      return definition.implemented &&
             id == QLatin1StringView{
                       definition.id.data(),
                       static_cast<qsizetype>(definition.id.size())};
    });
  };
  return implemented(modelCommandDefinitions) ||
         implemented(editSketchCommandDefinitions) ||
         implemented(sketchCommandDefinitions);
}

[[nodiscard]] inline QString
commandLabel(const std::vector<CommandDescriptor> &commands,
             const QString &id) {
  const auto found = std::ranges::find(commands, id, &CommandDescriptor::id);
  return found == commands.end() ? QString{} : found->label;
}

[[nodiscard]] inline std::vector<CommandDescriptor> sketchCommandRecords() {
  return commandRecords(sketchCommandDefinitions, QStringLiteral("sketch"));
}

[[nodiscard]] inline std::vector<CommandDescriptor> historyCommandRecords() {
  return commandRecords(versionCommandDefinitions, {});
}

[[nodiscard]] inline std::vector<CommandDescriptor> allCommandRecords() {
  std::vector<CommandDescriptor> result;
  const auto append = [&result](auto definitions, const QString &workspace) {
    auto records = commandRecords(definitions, workspace);
    result.insert(result.end(), std::make_move_iterator(records.begin()),
                  std::make_move_iterator(records.end()));
  };
  append(std::span{modelCommandDefinitions}.first(1), QStringLiteral("sketch"));
  append(editSketchCommandDefinitions, QStringLiteral("sketch"));
  append(std::span{modelCommandDefinitions}.subspan(1),
         QStringLiteral("model"));
  append(sketchCommandDefinitions, QStringLiteral("sketch"));
  append(assemblyCommandDefinitions, QStringLiteral("assemble"));
  append(sheetMetalCommandDefinitions, QStringLiteral("sheet-metal"));
  append(simulationCommandDefinitions, QStringLiteral("simulate"));
  append(camCommandDefinitions, QStringLiteral("cam"));
  append(drawingCommandDefinitions, QStringLiteral("drawing"));
  append(bomCommandDefinitions, QStringLiteral("bom"));
  append(versionCommandDefinitions, {});
  append(agentCommandDefinitions, {});
  return result;
}

[[nodiscard]] inline std::vector<CommandDescriptor>
commandsFor(const QString &workspaceId, bool sketchEditing = false,
            bool sketchSelected = false) {
  if (workspaceId == QStringLiteral("sketch")) {
    if (sketchEditing)
      return sketchCommandRecords();
    auto result = commandRecords(std::span{modelCommandDefinitions}.first(1),
                                 workspaceId);
    auto edit = commandRecords(editSketchCommandDefinitions, workspaceId);
    edit.front().available = sketchSelected;
    edit.front().unavailableReason =
        sketchSelected ? QString{} : QStringLiteral("Select a Sketch to edit");
    result.insert(result.end(), std::make_move_iterator(edit.begin()),
                  std::make_move_iterator(edit.end()));
    return result;
  }
  if (workspaceId == QStringLiteral("assemble"))
    return commandRecords(assemblyCommandDefinitions, workspaceId);
  if (workspaceId == QStringLiteral("sheet-metal"))
    return commandRecords(sheetMetalCommandDefinitions, workspaceId);
  if (workspaceId == QStringLiteral("simulate"))
    return commandRecords(simulationCommandDefinitions, workspaceId);
  if (workspaceId == QStringLiteral("drawing"))
    return commandRecords(drawingCommandDefinitions, workspaceId);
  if (workspaceId == QStringLiteral("bom"))
    return commandRecords(bomCommandDefinitions, workspaceId);
  if (workspaceId == QStringLiteral("cam"))
    return commandRecords(camCommandDefinitions, workspaceId);
  return commandRecords(modelCommandDefinitions, QStringLiteral("model"));
}

} // namespace kearne::ui
