#include "development_frontend_port.hpp"
#include "local_sketch_session.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <iterator>
#include <limits>
#include <optional>
#include <span>
#include <string_view>
#include <utility>

namespace kearne::ui {
namespace {

struct CommandDefinition {
  const char *id;
  const char *label;
  const char *icon;
  const char *group;
};

constexpr std::array<std::string_view, 22> frontendCommandContracts{
    "model.sketch.create", "model.extrude",        "model.plane.create",
    "sketch.point",        "sketch.line",          "sketch.polyline",
    "sketch.rectangle",    "sketch.circle",        "sketch.arc",
    "sketch.trim",         "sketch.dimension",     "sketch.coincident",
    "sketch.horizontal",   "sketch.vertical",      "sketch.equal",
    "sketch.parallel",     "sketch.perpendicular", "sketch.tangent",
    "sketch.concentric",   "sketch.midpoint",      "sketch.fixed",
    "sketch.collinear",
};

bool hasFrontendCommandContract(std::string_view commandId) {
  return std::ranges::find(frontendCommandContracts, commandId) !=
         frontendCommandContracts.end();
}

QString unavailableReason(const QString &commandId) {
  if (commandId.startsWith(QStringLiteral("assembly.")) ||
      commandId.startsWith(QStringLiteral("sheet-metal.")) ||
      commandId.startsWith(QStringLiteral("simulation.")) ||
      commandId.startsWith(QStringLiteral("cam.")) ||
      commandId.startsWith(QStringLiteral("drawing.")) ||
      commandId.startsWith(QStringLiteral("bom.")))
    return QStringLiteral("Planned after the parametric-part MVP");
  return QStringLiteral("Interaction contract not defined yet");
}

std::vector<CommandDescriptor>
commandRecords(const std::span<const CommandDefinition> definitions,
               const QString &workspaceId) {
  std::vector<CommandDescriptor> result;
  result.reserve(definitions.size());
  for (const CommandDefinition &definition : definitions) {
    const bool available = hasFrontendCommandContract(definition.id);
    const QString id = QString::fromLatin1(definition.id);
    result.push_back({id, QString::fromLatin1(definition.label),
                      QString::fromLatin1(definition.icon),
                      QString::fromLatin1(definition.group), workspaceId,
                      QString{}, available,
                      available ? QString{} : unavailableReason(id)});
  }
  return result;
}

bool containsOption(const std::vector<UiOption> &options, const QString &id) {
  return std::any_of(options.cbegin(), options.cend(),
                     [&id](const UiOption &option) { return option.id == id; });
}

constexpr std::array modelCommands{
    CommandDefinition{"model.sketch.create", "New Sketch", "sketch", "Create"},
    CommandDefinition{"model.extrude", "Extrude", "extrude", "Create"},
    CommandDefinition{"model.revolve", "Revolve", "revolve", "Create"},
    CommandDefinition{"model.sweep", "Sweep", "path", "Create"},
    CommandDefinition{"model.loft", "Loft", "layers", "Create"},
    CommandDefinition{"model.fillet", "Fillet", "round", "Modify"},
    CommandDefinition{"model.chamfer", "Chamfer", "chamfer", "Modify"},
    CommandDefinition{"model.shell", "Shell", "shell", "Modify"},
    CommandDefinition{"model.hole", "Hole", "hole", "Modify"},
    CommandDefinition{"model.pattern", "Pattern", "grid", "Transform"},
    CommandDefinition{"model.mirror", "Mirror", "mirror", "Transform"},
    CommandDefinition{"model.plane.create", "Plane", "plane", "Reference"},
    CommandDefinition{"model.material.assign", "Material", "material",
                      "Inspect"},
    CommandDefinition{"inspect.measure", "Measure", "measure", "Inspect"},
    CommandDefinition{"inspect.section", "Section", "section", "Inspect"},
};

constexpr std::array sketchCommands{
    CommandDefinition{"sketch.point", "Point", "point", "Geometry"},
    CommandDefinition{"sketch.line", "Line", "line", "Geometry"},
    CommandDefinition{"sketch.polyline", "Polyline", "polyline", "Geometry"},
    CommandDefinition{"sketch.rectangle", "Rectangle", "rectangle", "Geometry"},
    CommandDefinition{"sketch.circle", "Circle", "circle", "Geometry"},
    CommandDefinition{"sketch.arc", "Arc", "arc", "Geometry"},
    CommandDefinition{"sketch.trim", "Trim", "trim", "Modify"},
    CommandDefinition{"sketch.dimension", "Dimension", "dimension",
                      "Constrain"},
    CommandDefinition{"sketch.coincident", "Coincident", "coincident",
                      "Constrain"},
    CommandDefinition{"sketch.horizontal", "Horizontal", "horizontal",
                      "Constrain"},
    CommandDefinition{"sketch.vertical", "Vertical", "vertical", "Constrain"},
    CommandDefinition{"sketch.equal", "Equal", "equal", "Constrain"},
    CommandDefinition{"sketch.parallel", "Parallel", "parallel", "Constrain"},
    CommandDefinition{"sketch.perpendicular", "Perpendicular", "perpendicular",
                      "Constrain"},
    CommandDefinition{"sketch.tangent", "Tangent", "tangent", "Constrain"},
    CommandDefinition{"sketch.concentric", "Concentric", "concentric",
                      "Constrain"},
    CommandDefinition{"sketch.midpoint", "Midpoint", "midpoint", "Constrain"},
    CommandDefinition{"sketch.fixed", "Fixed", "fixed", "Constrain"},
    CommandDefinition{"sketch.collinear", "Collinear", "collinear",
                      "Constrain"},
};

constexpr std::array assemblyCommands{
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
                      "Inspect"},
};

constexpr std::array sheetMetalCommands{
    CommandDefinition{"sheet-metal.base", "Base flange", "sheet-metal",
                      "Create"},
    CommandDefinition{"sheet-metal.flange", "Flange", "sheet-metal", "Create"},
    CommandDefinition{"sheet-metal.bend", "Bend", "bend", "Create"},
    CommandDefinition{"sheet-metal.hem", "Hem", "bend", "Create"},
    CommandDefinition{"sheet-metal.relief", "Corner relief", "trim", "Modify"},
    CommandDefinition{"sheet-metal.rip", "Rip", "section", "Modify"},
    CommandDefinition{"sheet-metal.unfold", "Unfold", "unfold", "Review"},
    CommandDefinition{"sheet-metal.refold", "Refold", "mirror", "Review"},
};

constexpr std::array simulationCommands{
    CommandDefinition{"simulation.study", "Study", "simulate", "Setup"},
    CommandDefinition{"simulation.material", "Material", "material", "Setup"},
    CommandDefinition{"simulation.constraint", "Constraint", "lock", "Loads"},
    CommandDefinition{"simulation.load", "Load", "target", "Loads"},
    CommandDefinition{"simulation.mesh", "Mesh", "mesh", "Solve"},
    CommandDefinition{"simulation.solve", "Solve", "solve", "Solve"},
    CommandDefinition{"simulation.results", "Results", "chart", "Review"},
};

constexpr std::array drawingCommands{
    CommandDefinition{"drawing.sheet", "Sheet", "sheet", "Create"},
    CommandDefinition{"drawing.base_view", "Base view", "view", "Views"},
    CommandDefinition{"drawing.projected_view", "Projected", "layers", "Views"},
    CommandDefinition{"drawing.section_view", "Section", "section", "Views"},
    CommandDefinition{"drawing.dimension", "Dimension", "dimension",
                      "Annotate"},
    CommandDefinition{"drawing.gdt", "GD&T", "target", "Annotate"},
};

constexpr std::array bomCommands{
    CommandDefinition{"bom.refresh", "Refresh", "refresh", "Table"},
    CommandDefinition{"bom.columns", "Columns", "columns", "Table"},
    CommandDefinition{"bom.balloon", "Balloon", "circle", "Annotate"},
    CommandDefinition{"bom.export", "Export", "export", "Output"},
};

constexpr std::array camCommands{
    CommandDefinition{"cam.setup", "Setup", "cam", "Setup"},
    CommandDefinition{"cam.stock", "Stock", "stock", "Setup"},
    CommandDefinition{"cam.face", "Face", "face", "2D"},
    CommandDefinition{"cam.contour", "Contour", "contour", "2D"},
    CommandDefinition{"cam.pocket", "Pocket", "pocket", "2D"},
    CommandDefinition{"cam.drill", "Drill", "drill", "Hole"},
    CommandDefinition{"cam.adaptive", "Adaptive", "toolpath", "3D"},
    CommandDefinition{"cam.simulate", "Simulate", "play", "Verify"},
    CommandDefinition{"cam.post", "Post", "export", "Output"},
};

constexpr std::array versionCommands{
    CommandDefinition{"version.undo", "Undo", "undo", "History"},
    CommandDefinition{"version.redo", "Redo", "redo", "History"},
    CommandDefinition{"version.checkpoint", "Checkpoint", "checkpoint",
                      "History"},
    CommandDefinition{"version.branch", "Branch", "branch", "History"},
    CommandDefinition{"version.compare", "Compare", "compare", "Review"},
    CommandDefinition{"version.merge", "Merge", "merge", "Review"},
};

constexpr std::array agentCommands{
    CommandDefinition{"agent.ask", "Ask Kearne", "agent", "Agent"},
    CommandDefinition{"agent.inspect", "Inspect context", "inspect", "Agent"},
    CommandDefinition{"agent.plan", "Plan change", "plan", "Agent"},
    CommandDefinition{"agent.review", "Review change", "review", "Agent"},
};

std::vector<CommandDescriptor> allCommandRecords() {
  std::vector<CommandDescriptor> result;
  const auto append = [&result](std::vector<CommandDescriptor> commands) {
    result.insert(result.end(), std::make_move_iterator(commands.begin()),
                  std::make_move_iterator(commands.end()));
  };
  append(commandRecords(modelCommands, QStringLiteral("model")));
  append(commandRecords(sketchCommands, QStringLiteral("sketch")));
  append(commandRecords(assemblyCommands, QStringLiteral("assemble")));
  append(commandRecords(sheetMetalCommands, QStringLiteral("sheet-metal")));
  append(commandRecords(simulationCommands, QStringLiteral("simulate")));
  append(commandRecords(camCommands, QStringLiteral("cam")));
  append(commandRecords(drawingCommands, QStringLiteral("drawing")));
  append(commandRecords(bomCommands, QStringLiteral("bom")));
  append(commandRecords(versionCommands, {}));
  append(commandRecords(agentCommands, {}));
  return result;
}

std::vector<CommandDescriptor> commandsFor(const QString &workspaceId) {
  if (workspaceId == QStringLiteral("sketch"))
    return commandRecords(sketchCommands, workspaceId);
  if (workspaceId == QStringLiteral("assemble"))
    return commandRecords(assemblyCommands, workspaceId);
  if (workspaceId == QStringLiteral("sheet-metal"))
    return commandRecords(sheetMetalCommands, workspaceId);
  if (workspaceId == QStringLiteral("simulate"))
    return commandRecords(simulationCommands, workspaceId);
  if (workspaceId == QStringLiteral("drawing"))
    return commandRecords(drawingCommands, workspaceId);
  if (workspaceId == QStringLiteral("bom"))
    return commandRecords(bomCommands, workspaceId);
  if (workspaceId == QStringLiteral("cam"))
    return commandRecords(camCommands, workspaceId);
  return commandRecords(modelCommands, QStringLiteral("model"));
}

QString commandLabel(const std::vector<CommandDescriptor> &commands,
                     const QString &commandId) {
  for (const CommandDescriptor &command : commands) {
    if (command.id == commandId)
      return command.label;
  }
  return {};
}

QString workspaceLabel(const std::vector<WorkspaceDescriptor> &workspaces,
                       const QString &workspaceId) {
  for (const WorkspaceDescriptor &workspace : workspaces) {
    if (workspace.id == workspaceId)
      return workspace.label;
  }
  return {};
}

QString gridSpacingFor(const QString &unitId) {
  if (unitId == QStringLiteral("cm"))
    return QStringLiteral("1 cm");
  if (unitId == QStringLiteral("m"))
    return QStringLiteral("0.01 m");
  if (unitId == QStringLiteral("in"))
    return QStringLiteral("0.5 in");
  return QStringLiteral("10 mm");
}

double gridSpacingMillimetersFor(const QString &unitId) {
  return unitId == QStringLiteral("in") ? 12.7 : 10.0;
}

struct CommandForm {
  QString title;
  QString guidance;
  std::vector<FieldDescriptor> fields;
  bool previewSupported = false;
  bool applySupported = false;
  SketchInputKind sketchInputKind = SketchInputKind::None;
  int minimumSketchInputs = 0;
  int maximumSketchInputs = 0;
  std::vector<SketchSelectionKind> sketchSelectionSequence;
};

std::optional<CommandForm> commandFormFor(const QString &commandId,
                                          const QString &gridSpacing) {
  if (commandId == QStringLiteral("model.plane.create")) {
    return CommandForm{
        QStringLiteral("Plane"),
        QStringLiteral("Define a construction plane"),
        {
            {QStringLiteral("model.plane.create.method"),
             QStringLiteral("Method"),
             FieldKind::Choice,
             QStringLiteral("offset"),
             {},
             {{QStringLiteral("offset"), QStringLiteral("Offset")},
              {QStringLiteral("midplane"), QStringLiteral("Midplane")},
              {QStringLiteral("angle"), QStringLiteral("At angle")},
              {QStringLiteral("three-points"),
               QStringLiteral("Through three points")},
              {QStringLiteral("tangent"),
               QStringLiteral("Tangent to surface")}}},
            {QStringLiteral("model.plane.create.reference"),
             QStringLiteral("Reference"), FieldKind::Reference,
             QStringLiteral("reference.plane.xy")},
            {QStringLiteral("model.plane.create.offset"),
             QStringLiteral("Offset"), FieldKind::Expression, gridSpacing,
             QStringLiteral("Not evaluated")},
        },
        true,
        true,
        SketchInputKind::None,
        0,
        0,
        {},
    };
  }
  if (commandId == QStringLiteral("model.sketch.create")) {
    return CommandForm{
        QStringLiteral("New Sketch"),
        QStringLiteral("Attach the Sketch to a datum plane or planar face"),
        {
            {QStringLiteral("model.sketch.create.attachment"),
             QStringLiteral("Plane or planar face"), FieldKind::Reference,
             QStringLiteral("reference.plane.xy")},
            {QStringLiteral("model.sketch.create.orientation"),
             QStringLiteral("View orientation"),
             FieldKind::Choice,
             QStringLiteral("normal"),
             {},
             {{QStringLiteral("normal"), QStringLiteral("Normal to Sketch")},
              {QStringLiteral("current"),
               QStringLiteral("Keep current view")}}},
        },
        true,
        true,
        SketchInputKind::None,
        0,
        0,
        {},
    };
  }
  if (commandId == QStringLiteral("model.extrude")) {
    return CommandForm{
        QStringLiteral("Extrude"),
        QStringLiteral("Create or modify a solid from a closed profile"),
        {
            {QStringLiteral("model.extrude.profile"), QStringLiteral("Profile"),
             FieldKind::Reference, QStringLiteral("profile.sketch001")},
            {QStringLiteral("model.extrude.distance"),
             QStringLiteral("Distance"), FieldKind::Expression, gridSpacing,
             QStringLiteral("Not evaluated")},
            {QStringLiteral("model.extrude.extent"),
             QStringLiteral("Extent"),
             FieldKind::Choice,
             QStringLiteral("one-sided"),
             {},
             {{QStringLiteral("one-sided"), QStringLiteral("One side")},
              {QStringLiteral("symmetric"), QStringLiteral("Symmetric")},
              {QStringLiteral("two-sided"), QStringLiteral("Two sides")}}},
            {QStringLiteral("model.extrude.operation"),
             QStringLiteral("Operation"),
             FieldKind::Choice,
             QStringLiteral("new"),
             {},
             {{QStringLiteral("new"), QStringLiteral("New body")},
              {QStringLiteral("add"), QStringLiteral("Add")},
              {QStringLiteral("remove"), QStringLiteral("Remove")}}},
        },
        true,
        true,
        SketchInputKind::None,
        0,
        0,
        {},
    };
  }

  const auto sketchToggle = [&commandId](const QString &suffix,
                                         const QString &label,
                                         bool value = false) {
    return FieldDescriptor{commandId + QStringLiteral(".") + suffix, label,
                           FieldKind::Toggle, value};
  };
  if (commandId == QStringLiteral("sketch.point"))
    return CommandForm{QStringLiteral("Point"),
                       QStringLiteral("Choose a point"),
                       {sketchToggle(QStringLiteral("construction"),
                                     QStringLiteral("Construction"))},
                       false,
                       false,
                       SketchInputKind::PlanePoint,
                       1,
                       1,
                       {}};
  if (commandId == QStringLiteral("sketch.line"))
    return CommandForm{QStringLiteral("Line"),
                       QStringLiteral("Choose two points"),
                       {sketchToggle(QStringLiteral("construction"),
                                     QStringLiteral("Construction"))},
                       false,
                       false,
                       SketchInputKind::PlanePoint,
                       2,
                       2,
                       {}};
  if (commandId == QStringLiteral("sketch.polyline"))
    return CommandForm{QStringLiteral("Polyline"),
                       QStringLiteral("Choose connected points"),
                       {sketchToggle(QStringLiteral("construction"),
                                     QStringLiteral("Construction")),
                        sketchToggle(QStringLiteral("close-profile"),
                                     QStringLiteral("Close profile"))},
                       false,
                       false,
                       SketchInputKind::PlanePoint,
                       2,
                       0,
                       {}};
  if (commandId == QStringLiteral("sketch.rectangle"))
    return CommandForm{
        QStringLiteral("Rectangle"),
        QStringLiteral("Choose defining points"),
        {{commandId + QStringLiteral(".method"),
          QStringLiteral("Method"),
          FieldKind::Choice,
          QStringLiteral("corner"),
          {},
          {{QStringLiteral("corner"), QStringLiteral("Corner")},
           {QStringLiteral("center"), QStringLiteral("Center")},
           {QStringLiteral("three-point"), QStringLiteral("Three point")}}},
         sketchToggle(QStringLiteral("construction"),
                      QStringLiteral("Construction"))},
        false,
        false,
        SketchInputKind::PlanePoint,
        2,
        2,
        {}};
  if (commandId == QStringLiteral("sketch.circle"))
    return CommandForm{
        QStringLiteral("Circle"),
        QStringLiteral("Choose defining points"),
        {{commandId + QStringLiteral(".method"),
          QStringLiteral("Method"),
          FieldKind::Choice,
          QStringLiteral("center-radius"),
          {},
          {{QStringLiteral("center-radius"),
            QStringLiteral("Center and radius")},
           {QStringLiteral("three-point"), QStringLiteral("Three point")}}},
         sketchToggle(QStringLiteral("construction"),
                      QStringLiteral("Construction"))},
        false,
        false,
        SketchInputKind::PlanePoint,
        2,
        2,
        {}};
  if (commandId == QStringLiteral("sketch.arc"))
    return CommandForm{
        QStringLiteral("Arc"),
        QStringLiteral("Choose defining points"),
        {{commandId + QStringLiteral(".method"),
          QStringLiteral("Method"),
          FieldKind::Choice,
          QStringLiteral("three-point"),
          {},
          {{QStringLiteral("three-point"), QStringLiteral("Three point")},
           {QStringLiteral("center"), QStringLiteral("Center")},
           {QStringLiteral("tangent"), QStringLiteral("Tangent")}}},
         sketchToggle(QStringLiteral("construction"),
                      QStringLiteral("Construction"))},
        false,
        false,
        SketchInputKind::PlanePoint,
        3,
        3,
        {}};
  if (commandId == QStringLiteral("sketch.trim"))
    return CommandForm{
        QStringLiteral("Trim"),
        QStringLiteral("Choose geometry to modify"),
        {{commandId + QStringLiteral(".mode"),
          QStringLiteral("Mode"),
          FieldKind::Choice,
          QStringLiteral("trim"),
          {},
          {{QStringLiteral("trim"), QStringLiteral("Trim")},
           {QStringLiteral("extend"), QStringLiteral("Extend")}}}},
        false,
        false,
        SketchInputKind::Entity,
        1,
        1,
        {SketchSelectionKind::Curve}};
  if (commandId == QStringLiteral("sketch.dimension"))
    return CommandForm{
        QStringLiteral("Dimension"),
        QStringLiteral("Choose geometry, then enter an expression"),
        {{commandId + QStringLiteral(".kind"),
          QStringLiteral("Dimension"),
          FieldKind::Choice,
          QStringLiteral("automatic"),
          {},
          {{QStringLiteral("automatic"), QStringLiteral("Automatic")},
           {QStringLiteral("distance"), QStringLiteral("Distance")},
           {QStringLiteral("horizontal-distance"),
            QStringLiteral("Horizontal distance")},
           {QStringLiteral("vertical-distance"),
            QStringLiteral("Vertical distance")},
           {QStringLiteral("radius"), QStringLiteral("Radius")},
           {QStringLiteral("diameter"), QStringLiteral("Diameter")},
           {QStringLiteral("angle"), QStringLiteral("Angle")}}},
         {commandId + QStringLiteral(".expression"),
          QStringLiteral("Expression"), FieldKind::Expression,
          QStringLiteral("")},
         sketchToggle(QStringLiteral("driving"), QStringLiteral("Driving"),
                      true)},
        false,
        false,
        SketchInputKind::Entity,
        1,
        2,
        {SketchSelectionKind::Any, SketchSelectionKind::Any}};
  if (commandId.startsWith(QStringLiteral("sketch."))) {
    const QString title = commandLabel(
        commandRecords(sketchCommands, QStringLiteral("sketch")), commandId);
    const bool singleEntity =
        commandId == QStringLiteral("sketch.horizontal") ||
        commandId == QStringLiteral("sketch.vertical") ||
        commandId == QStringLiteral("sketch.fixed");
    std::vector<SketchSelectionKind> selections;
    if (commandId == QStringLiteral("sketch.coincident")) {
      selections = {SketchSelectionKind::Point, SketchSelectionKind::Point};
    } else if (commandId == QStringLiteral("sketch.midpoint")) {
      selections = {SketchSelectionKind::Point, SketchSelectionKind::Curve};
    } else if (commandId == QStringLiteral("sketch.fixed")) {
      selections = {SketchSelectionKind::Any};
    } else {
      selections.assign(singleEntity ? 1 : 2, SketchSelectionKind::Curve);
    }
    return CommandForm{
        title,
        singleEntity ? QStringLiteral("Choose compatible Sketch geometry")
                     : QStringLiteral("Choose two compatible Sketch entities"),
        {},
        false,
        false,
        SketchInputKind::Entity,
        singleEntity ? 1 : 2,
        singleEntity ? 1 : 2,
        std::move(selections)};
  }
  return std::nullopt;
}

std::vector<SketchPrimitiveProjection> mountingProfileProjection() {
  using Kind = SketchPrimitiveKind;
  return {
      {QStringLiteral("sketch.mounting.edge.bottom"),
       Kind::Line,
       {{-0.05, -0.03}, {0.05, -0.03}},
       {QStringLiteral("start"), QStringLiteral("end")},
       {}},
      {QStringLiteral("sketch.mounting.edge.right"),
       Kind::Line,
       {{0.05, -0.03}, {0.05, 0.03}},
       {QStringLiteral("start"), QStringLiteral("end")},
       {}},
      {QStringLiteral("sketch.mounting.edge.top"),
       Kind::Line,
       {{0.05, 0.03}, {-0.05, 0.03}},
       {QStringLiteral("start"), QStringLiteral("end")},
       {}},
      {QStringLiteral("sketch.mounting.edge.left"),
       Kind::Line,
       {{-0.05, 0.03}, {-0.05, -0.03}},
       {QStringLiteral("start"), QStringLiteral("end")},
       {}},
      {QStringLiteral("sketch.mounting.hole.bottom-left"),
       Kind::Circle,
       {{-0.024, -0.018}},
       {QStringLiteral("center")},
       {},
       0.00325},
      {QStringLiteral("sketch.mounting.hole.bottom-right"),
       Kind::Circle,
       {{0.024, -0.018}},
       {QStringLiteral("center")},
       {},
       0.00325},
      {QStringLiteral("sketch.mounting.hole.top-left"),
       Kind::Circle,
       {{-0.024, 0.018}},
       {QStringLiteral("center")},
       {},
       0.00325},
      {QStringLiteral("sketch.mounting.hole.top-right"),
       Kind::Circle,
       {{0.024, 0.018}},
       {QStringLiteral("center")},
       {},
       0.00325},
  };
}

struct Circumcircle {
  PlanePoint center;
  double radiusMetres;
};

std::optional<Circumcircle> circumcircle(const PlanePoint &first,
                                         const PlanePoint &second,
                                         const PlanePoint &third) {
  const double bx = second.xMetres - first.xMetres;
  const double by = second.yMetres - first.yMetres;
  const double cx = third.xMetres - first.xMetres;
  const double cy = third.yMetres - first.yMetres;
  const double cross = bx * cy - by * cx;
  const double scale = std::max(
      {std::hypot(bx, by), std::hypot(cx, cy), std::hypot(cx - bx, cy - by)});
  if (!std::isfinite(cross) || !std::isfinite(scale) || scale == 0.0 ||
      std::abs(cross) <=
          64.0 * std::numeric_limits<double>::epsilon() * scale * scale)
    return std::nullopt;

  const double bSquared = bx * bx + by * by;
  const double cSquared = cx * cx + cy * cy;
  const double denominator = 2.0 * cross;
  const PlanePoint center{
      first.xMetres + (cy * bSquared - by * cSquared) / denominator,
      first.yMetres + (bx * cSquared - cx * bSquared) / denominator};
  const double radius = std::hypot(center.xMetres - first.xMetres,
                                   center.yMetres - first.yMetres);
  if (!std::isfinite(center.xMetres) || !std::isfinite(center.yMetres) ||
      !std::isfinite(radius) || radius == 0.0)
    return std::nullopt;
  return Circumcircle{center, radius};
}

class DevelopmentFrontendPort final : public FrontendPort {
public:
  DevelopmentFrontendPort(
      std::vector<UiOption> themeOptions, const QString &themeId,
      const QString &defaultLengthUnitId, const QString &interfaceDensityId,
      const QString &navigationProfileId, const QString &zoomDirectionId,
      std::unique_ptr<LocalSketchSession> sketchSession = {})
      : localMode_(sketchSession != nullptr),
        localSketchSession_(std::move(sketchSession)) {
    snapshot_.generation = 1;
    snapshot_.projectName = QStringLiteral("Motor Bracket");
    snapshot_.branchLabel = QStringLiteral("main");
    snapshot_.revisionLabel = QStringLiteral("UI contract mode");
    snapshot_.projectRevision = QStringLiteral("development.project.1");
    snapshot_.activeWorkspaceId = QStringLiteral("model");
    snapshot_.viewportState = QStringLiteral("unavailable");
    snapshot_.inspectorTitle = QStringLiteral("Selection");
    snapshot_.inspectorStatus =
        QStringLiteral("Engineering backend disconnected");
    snapshot_.viewportHeadline = QStringLiteral("Viewport ready");
    snapshot_.viewportDetail =
        QStringLiteral("Render projection is not connected");
    snapshot_.modelHealth = QStringLiteral("Backend disconnected");
    snapshot_.selectionSummary = QStringLiteral("Nothing selected");
    snapshot_.agentStatus = QStringLiteral("Codex harness disconnected");
    snapshot_.sourceEditingAvailable = true;
    snapshot_.defaultLengthUnitId = defaultLengthUnitId;
    snapshot_.interfaceDensityId = interfaceDensityId;
    snapshot_.projectLengthUnitId = QStringLiteral("mm");
    snapshot_.gridPlaneLabel = QStringLiteral("XY");
    snapshot_.gridSpacingLabel = gridSpacingFor(snapshot_.projectLengthUnitId);
    snapshot_.gridSpacingMillimeters =
        gridSpacingMillimetersFor(snapshot_.projectLengthUnitId);
    snapshot_.lengthUnits = {
        {QStringLiteral("mm"), QStringLiteral("Millimeters"),
         QStringLiteral("mm")},
        {QStringLiteral("cm"), QStringLiteral("Centimeters"),
         QStringLiteral("cm")},
        {QStringLiteral("m"), QStringLiteral("Meters"), QStringLiteral("m")},
        {QStringLiteral("in"), QStringLiteral("Inches"), QStringLiteral("in")},
    };
    snapshot_.preferenceCategories = {
        {QStringLiteral("appearance"), QStringLiteral("Appearance"),
         QStringLiteral("view")},
        {QStringLiteral("units"), QStringLiteral("Units"),
         QStringLiteral("measure")},
        {QStringLiteral("input"), QStringLiteral("Input"),
         QStringLiteral("pan")},
        {QStringLiteral("files"), QStringLiteral("Files"),
         QStringLiteral("folder")},
        {QStringLiteral("compute"), QStringLiteral("Compute"),
         QStringLiteral("operations")},
        {QStringLiteral("agent"), QStringLiteral("AI and privacy"),
         QStringLiteral("agent")},
    };
    snapshot_.preferences = {
        {QStringLiteral("theme"), QStringLiteral("appearance"),
         QStringLiteral("Theme"), QStringLiteral("Application color scheme"),
         PreferenceKind::Choice, themeId, std::move(themeOptions)},
        {QStringLiteral("interface-density"),
         QStringLiteral("appearance"),
         QStringLiteral("Interface density"),
         QStringLiteral("Control and workspace spacing"),
         PreferenceKind::Choice,
         snapshot_.interfaceDensityId,
         {{QStringLiteral("compact"), QStringLiteral("Compact")},
          {QStringLiteral("comfortable"), QStringLiteral("Comfortable")}}},
        {QStringLiteral("default-length-unit"), QStringLiteral("units"),
         QStringLiteral("New project length unit"),
         QStringLiteral(
             "Seeds new projects; existing projects keep their own unit"),
         PreferenceKind::Choice, snapshot_.defaultLengthUnitId,
         snapshot_.lengthUnits},
        {QStringLiteral("project-length-unit"), QStringLiteral("units"),
         QStringLiteral("Current project length unit"),
         QStringLiteral("Controls display, input, and grid labels without "
                        "rescaling geometry"),
         PreferenceKind::Choice, snapshot_.projectLengthUnitId,
         snapshot_.lengthUnits},
        {QStringLiteral("navigation-profile"),
         QStringLiteral("input"),
         QStringLiteral("Mouse navigation"),
         QStringLiteral("Pan, orbit, and zoom button mapping"),
         PreferenceKind::Choice,
         navigationProfileId,
         {{QStringLiteral("fusion"), QStringLiteral("Fusion")},
          {QStringLiteral("solidworks"), QStringLiteral("SolidWorks")},
          {QStringLiteral("onshape"), QStringLiteral("Onshape")}}},
        {QStringLiteral("zoom-direction"),
         QStringLiteral("input"),
         QStringLiteral("Wheel direction"),
         QStringLiteral("Scroll direction for viewport zoom"),
         PreferenceKind::Choice,
         zoomDirectionId,
         {{QStringLiteral("standard"), QStringLiteral("Scroll up to zoom in")},
          {QStringLiteral("reversed"),
           QStringLiteral("Scroll down to zoom in")}}},
        {QStringLiteral("selection"),
         QStringLiteral("input"),
         QStringLiteral("Selection cycling"),
         QStringLiteral("Cycle overlapping selectable entities"),
         PreferenceKind::Toggle,
         true,
         {},
         false},
        {QStringLiteral("autosave"),
         QStringLiteral("files"),
         QStringLiteral("Recovery interval"),
         QStringLiteral("Minutes between recoverable journal checkpoints"),
         PreferenceKind::Text,
         QStringLiteral("5"),
         {},
         false},
        {QStringLiteral("backup"),
         QStringLiteral("files"),
         QStringLiteral("Backup before migration"),
         QStringLiteral("Keep the original project before format migration"),
         PreferenceKind::Toggle,
         true,
         {},
         false},
        {QStringLiteral("cache"),
         QStringLiteral("files"),
         QStringLiteral("Cache limit"),
         QStringLiteral("Disposable geometry and mesh artifacts"),
         PreferenceKind::Text,
         QStringLiteral("20 GB"),
         {},
         false},
        {QStringLiteral("workers"),
         QStringLiteral("compute"),
         QStringLiteral("Worker limit"),
         QStringLiteral("Maximum concurrent local workers"),
         PreferenceKind::Text,
         QStringLiteral("Automatic"),
         {},
         false},
        {QStringLiteral("gpu"),
         QStringLiteral("compute"),
         QStringLiteral("Graphics backend"),
         QStringLiteral("Restart required after a persistent change"),
         PreferenceKind::Choice,
         QStringLiteral("automatic"),
         {{QStringLiteral("automatic"), QStringLiteral("Automatic")},
          {QStringLiteral("vulkan"), QStringLiteral("Vulkan")},
          {QStringLiteral("d3d11"), QStringLiteral("Direct3D 11")},
          {QStringLiteral("opengl"), QStringLiteral("OpenGL")}},
         false},
        {QStringLiteral("background"),
         QStringLiteral("compute"),
         QStringLiteral("Background evaluation"),
         QStringLiteral("Continue noninteractive work while editing"),
         PreferenceKind::Toggle,
         true,
         {},
         false},
        {QStringLiteral("codex"),
         QStringLiteral("agent"),
         QStringLiteral("Codex harness"),
         QStringLiteral("Supervised app-server access; currently disconnected"),
         PreferenceKind::Toggle,
         false,
         {},
         false},
        {QStringLiteral("capture"),
         QStringLiteral("agent"),
         QStringLiteral("Application capture"),
         QStringLiteral("Allow lossless capture of Kearne-owned surfaces"),
         PreferenceKind::Choice,
         QStringLiteral("ask"),
         {{QStringLiteral("disabled"), QStringLiteral("Disabled")},
          {QStringLiteral("ask"), QStringLiteral("Ask each session")},
          {QStringLiteral("developer"), QStringLiteral("Developer profile")}},
         false},
        {QStringLiteral("network"),
         QStringLiteral("agent"),
         QStringLiteral("Provider network access"),
         QStringLiteral("Separate from local engineering commands"),
         PreferenceKind::Toggle,
         false,
         {},
         false},
    };
    snapshot_.modelSource = QStringLiteral(
        "from build123d import BuildSketch, Circle, Locations, Mode, "
        "Rectangle, extrude\n\n"
        "def mounting_profile(\n"
        "    width: float = 100.0,\n"
        "    depth: float = 60.0,\n"
        "    hole_spacing: float = 48.0,\n"
        "    hole_diameter: float = 6.5,\n"
        "):\n"
        "    with BuildSketch() as profile:\n"
        "        Rectangle(width, depth)\n"
        "        with Locations(\n"
        "            (-hole_spacing / 2, -18),\n"
        "            (hole_spacing / 2, -18),\n"
        "            (-hole_spacing / 2, 18),\n"
        "            (hole_spacing / 2, 18),\n"
        "        ):\n"
        "            Circle(hole_diameter / 2, mode=Mode.SUBTRACT)\n"
        "    return profile.sketch\n\n"
        "def base_plate(\n"
        "    width: float = 100.0,\n"
        "    depth: float = 60.0,\n"
        "    thickness: float = 8.0,\n"
        "    hole_spacing: float = 48.0,\n"
        "    hole_diameter: float = 6.5,\n"
        "):\n"
        "    profile = mounting_profile(\n"
        "        width, depth, hole_spacing, hole_diameter\n"
        "    )\n"
        "    return extrude(profile, amount=thickness)\n");
    snapshot_.selectedFunction = {
        QStringLiteral("function.base_plate"),
        QStringLiteral("base_plate"),
        QStringLiteral(
            "base_plate(width: Length, depth: Length, thickness: Length, "
            "hole_spacing: Length, hole_diameter: Length) → body: Solid"),
        QStringLiteral("base_plate.py"),
        QStringLiteral("Python / build123d"),
        QStringLiteral("unverified"),
        QStringLiteral("development projection"),
        {
            {QStringLiteral("input.width"), QStringLiteral("width"),
             QStringLiteral("Length"), QStringLiteral("100 mm"),
             QStringLiteral("unresolved")},
            {QStringLiteral("input.depth"), QStringLiteral("depth"),
             QStringLiteral("Length"), QStringLiteral("60 mm"),
             QStringLiteral("unresolved")},
            {QStringLiteral("input.thickness"), QStringLiteral("thickness"),
             QStringLiteral("Length"), QStringLiteral("8 mm"),
             QStringLiteral("unresolved")},
            {QStringLiteral("input.hole-spacing"),
             QStringLiteral("hole_spacing"), QStringLiteral("Length"),
             QStringLiteral("48 mm"), QStringLiteral("unresolved")},
            {QStringLiteral("input.hole-diameter"),
             QStringLiteral("hole_diameter"), QStringLiteral("Length"),
             QStringLiteral("6.5 mm"), QStringLiteral("unresolved")},
        },
        {
            {QStringLiteral("output.body"), QStringLiteral("body"),
             QStringLiteral("Solid"), QStringLiteral("Not evaluated"),
             QStringLiteral("unavailable")},
        },
    };
    basePlateFunction_ = snapshot_.selectedFunction;
    mountingProfileFunction_ = {
        QStringLiteral("function.mounting_profile"),
        QStringLiteral("mounting_profile"),
        QStringLiteral(
            "mounting_profile(width: Length, depth: Length, hole_spacing: "
            "Length, hole_diameter: Length) → profile: Sketch"),
        QStringLiteral("base_plate.py"),
        QStringLiteral("Python / build123d"),
        QStringLiteral("unverified"),
        QStringLiteral("development projection"),
        {
            {QStringLiteral("input.width"), QStringLiteral("width"),
             QStringLiteral("Length"), QStringLiteral("100 mm"),
             QStringLiteral("unresolved")},
            {QStringLiteral("input.depth"), QStringLiteral("depth"),
             QStringLiteral("Length"), QStringLiteral("60 mm"),
             QStringLiteral("unresolved")},
            {QStringLiteral("input.hole-spacing"),
             QStringLiteral("hole_spacing"), QStringLiteral("Length"),
             QStringLiteral("48 mm"), QStringLiteral("unresolved")},
            {QStringLiteral("input.hole-diameter"),
             QStringLiteral("hole_diameter"), QStringLiteral("Length"),
             QStringLiteral("6.5 mm"), QStringLiteral("unresolved")},
        },
        {
            {QStringLiteral("output.profile"), QStringLiteral("profile"),
             QStringLiteral("Sketch"), QStringLiteral("Not evaluated"),
             QStringLiteral("unavailable")},
        },
    };
    snapshot_.sketchProjection = {
        QStringLiteral("development projection"),
        QStringLiteral("function.mounting_profile"),
        QStringLiteral("reference.plane.xy"),
        QStringLiteral("mm"),
        QStringLiteral("not-evaluated"),
        -1,
        mountingProfileProjection(),
    };
    snapshot_.commandCatalog = allCommandRecords();
    snapshot_.workspaces = {
        {QStringLiteral("model"), QStringLiteral("Model"),
         QStringLiteral("model")},
        {QStringLiteral("sketch"), QStringLiteral("Sketch"),
         QStringLiteral("sketch")},
        {QStringLiteral("assemble"), QStringLiteral("Assemble"),
         QStringLiteral("assemble")},
        {QStringLiteral("sheet-metal"), QStringLiteral("Sheet Metal"),
         QStringLiteral("sheet-metal")},
        {QStringLiteral("simulate"), QStringLiteral("Simulate"),
         QStringLiteral("simulate")},
        {QStringLiteral("cam"), QStringLiteral("CAM"), QStringLiteral("cam")},
        {QStringLiteral("drawing"), QStringLiteral("Drawing"),
         QStringLiteral("drawing")},
        {QStringLiteral("bom"), QStringLiteral("BOM"), QStringLiteral("bom")},
    };
    snapshot_.structure = {
        {QStringLiteral("project.root"), QStringLiteral("Motor Bracket"), 0,
         QStringLiteral("project")},
        {QStringLiteral("component.base_plate"), QStringLiteral("Base Plate"),
         1, QStringLiteral("component")},
        {QStringLiteral("reference.origin"), QStringLiteral("Origin"), 2,
         QStringLiteral("group")},
        {QStringLiteral("reference.plane.xy"), QStringLiteral("XY Plane"), 3,
         QStringLiteral("plane")},
        {QStringLiteral("reference.plane.xz"), QStringLiteral("XZ Plane"), 3,
         QStringLiteral("plane")},
        {QStringLiteral("reference.plane.yz"), QStringLiteral("YZ Plane"), 3,
         QStringLiteral("plane")},
        {QStringLiteral("function.mounting_profile"),
         QStringLiteral("mounting_profile()"), 2,
         QStringLiteral("model-function")},
        {QStringLiteral("output.mounting_profile.profile"),
         QStringLiteral("profile"), 3, QStringLiteral("sketch")},
        {QStringLiteral("function.base_plate"), QStringLiteral("base_plate()"),
         2, QStringLiteral("model-function")},
        {QStringLiteral("output.base_plate.body"), QStringLiteral("body"), 3,
         QStringLiteral("output")},
        {QStringLiteral("component.motor"), QStringLiteral("Motor NEMA 23"), 1,
         QStringLiteral("component")},
        {QStringLiteral("group.fasteners"), QStringLiteral("Fasteners"), 1,
         QStringLiteral("group")},
    };
    snapshot_.revisions = {
        {QStringLiteral("revision.ui.3"), QStringLiteral("Inspector state"),
         QStringLiteral("frontend contract")},
        {QStringLiteral("revision.ui.2"), QStringLiteral("Workspace layout"),
         QStringLiteral("frontend contract")},
        {QStringLiteral("revision.ui.1"), QStringLiteral("Application shell"),
         QStringLiteral("frontend contract")},
    };
    snapshot_.historyCommands = commandRecords(versionCommands, {});
    snapshot_.parameters = {
        {QStringLiteral("width"), QStringLiteral("width"),
         QStringLiteral("100 mm"), QStringLiteral("unresolved")},
        {QStringLiteral("depth"), QStringLiteral("depth"),
         QStringLiteral("60 mm"), QStringLiteral("unresolved")},
        {QStringLiteral("thickness"), QStringLiteral("thickness"),
         QStringLiteral("8 mm"), QStringLiteral("unresolved")},
        {QStringLiteral("hole-spacing"), QStringLiteral("hole_spacing"),
         QStringLiteral("48 mm"), QStringLiteral("unresolved")},
        {QStringLiteral("hole-diameter"), QStringLiteral("hole_diameter"),
         QStringLiteral("6.5 mm"), QStringLiteral("unresolved")},
    };
    snapshot_.jobs = {
        {QStringLiteral("job.backend"), QStringLiteral("Engineering services"),
         QStringLiteral("Unavailable"), -1},
    };
    snapshot_.diagnostics = {
        {QStringLiteral("diagnostic.backend"), QStringLiteral("information"),
         QStringLiteral("Frontend contract mode does not evaluate geometry.")},
    };
    snapshot_.proposals = {
        {QStringLiteral("proposal.connect"),
         QStringLiteral(
             "Connect the supervised Codex harness to enable proposals."),
         QStringLiteral("Unavailable")},
    };
    snapshot_.recentProjects = {
        {QStringLiteral("motor-bracket"), QStringLiteral("Motor Bracket"),
         QStringLiteral("UI contract workspace"),
         QStringLiteral("Current session"), QStringLiteral("model"),
         QStringLiteral("model")},
    };
    snapshot_.projectTemplates = {
        {QStringLiteral("part"), QStringLiteral("Model"),
         QStringLiteral("Parametric component"), QStringLiteral("model"),
         QStringLiteral("model")},
        {QStringLiteral("assembly"), QStringLiteral("Assembly"),
         QStringLiteral("Components and joints"), QStringLiteral("assemble"),
         QStringLiteral("assemble")},
        {QStringLiteral("drawing"), QStringLiteral("Drawing"),
         QStringLiteral("Associative documentation"), QStringLiteral("drawing"),
         QStringLiteral("drawing")},
        {QStringLiteral("sheet-metal"), QStringLiteral("Sheet Metal"),
         QStringLiteral("Fabricated sheet component"),
         QStringLiteral("sheet-metal"), QStringLiteral("sheet-metal")},
        {QStringLiteral("cam"), QStringLiteral("CAM"),
         QStringLiteral("Manufacturing setup"), QStringLiteral("cam"),
         QStringLiteral("cam")},
    };
    snapshot_.recoveryItems = {
        {QStringLiteral("recovery.none"),
         QStringLiteral("No recovery required"),
         QStringLiteral("The development workspace has no interrupted writes."),
         QStringLiteral("current"), false},
    };
    snapshot_.operations = {
        {QStringLiteral("operation.services"),
         QStringLiteral("Engineering services"), QStringLiteral("service"),
         QStringLiteral("unavailable"),
         QStringLiteral("No backend process has been started."), -1},
        {QStringLiteral("operation.frontend"),
         QStringLiteral("Frontend projection"), QStringLiteral("projection"),
         QStringLiteral("current"),
         QStringLiteral("Deterministic development data is active."), 100},
    };
    snapshot_.interfaceStates = {
        {QStringLiteral("empty"), QStringLiteral("Empty"),
         QStringLiteral("empty")},
        {QStringLiteral("loading"), QStringLiteral("Loading"),
         QStringLiteral("loading")},
        {QStringLiteral("current"), QStringLiteral("Current"),
         QStringLiteral("check")},
        {QStringLiteral("preview"), QStringLiteral("Preview"),
         QStringLiteral("preview")},
        {QStringLiteral("pending"), QStringLiteral("Pending"),
         QStringLiteral("clock")},
        {QStringLiteral("stale"), QStringLiteral("Stale"),
         QStringLiteral("stale")},
        {QStringLiteral("failed"), QStringLiteral("Failed"),
         QStringLiteral("error")},
        {QStringLiteral("unavailable"), QStringLiteral("Unavailable"),
         QStringLiteral("unavailable")},
        {QStringLiteral("read-only"), QStringLiteral("Read-only"),
         QStringLiteral("lock")},
        {QStringLiteral("permission-denied"),
         QStringLiteral("Permission denied"), QStringLiteral("shield")},
    };
    if (localMode_)
      initializeLocalProjection();
    refreshWorkspace();
    if (localMode_)
      beginLocalPreparation();
  }

  [[nodiscard]] FrontendSnapshotPtr snapshot() const override {
    if (!published_ || published_->generation != snapshot_.generation)
      published_ = std::make_shared<const FrontendSnapshot>(snapshot_);
    return published_;
  }

  void setChangeHandler(ChangeHandler handler) override {
    changeHandler_ = std::move(handler);
  }

  void selectWorkspace(const QString &workspaceId) override {
    const QString label = workspaceLabel(snapshot_.workspaces, workspaceId);
    if (label.isEmpty())
      return;
    if (workspaceId == snapshot_.activeWorkspaceId)
      return;
    snapshot_.activeWorkspaceId = workspaceId;
    selectFunctionForWorkspace(workspaceId);
    snapshot_.activeCommandId.clear();
    snapshot_.selectionSummary = QStringLiteral("Nothing selected");
    snapshot_.inspectorTitle = QStringLiteral("%1 workspace").arg(label);
    refreshWorkspace();
  }

  void selectEntity(const QString &entityId) override {
    if (entityId == mountingProfileFunction_.id)
      snapshot_.selectedFunction = mountingProfileFunction_;
    else if (entityId == basePlateFunction_.id)
      snapshot_.selectedFunction = basePlateFunction_;
    snapshot_.selectionSummary = entityId;
    snapshot_.selectedSketchEntityId.clear();
    if (!snapshot_.activeCommandId.isEmpty()) {
      const auto reference = std::ranges::find_if(
          snapshot_.fields, [](const FieldDescriptor &field) {
            return field.kind == FieldKind::Reference && !field.readOnly;
          });
      if (reference != snapshot_.fields.end()) {
        reference->value = entityId;
        if (localMode_ && snapshot_.activeCommandId ==
                              QStringLiteral("model.sketch.create")) {
          snapshot_.commandDraft.state = CommandDraftState::Editing;
          snapshot_.inspectorStatus =
              QStringLiteral("Creating a Sketch on the selected reference");
          ++snapshot_.generation;
          static_cast<void>(submitLocalSketchCreation());
          return;
        }
      }
      snapshot_.commandDraft.state = CommandDraftState::Editing;
      snapshot_.inspectorStatus =
          QStringLiteral("Selection added to the active command draft");
      ++snapshot_.generation;
      return;
    }
    if (localMode_ && snapshot_.activeWorkspaceId == QStringLiteral("sketch") &&
        SketchEntityId::parse(entityId.toStdString())) {
      snapshot_.selectedSketchEntityId = entityId;
      snapshot_.selectionSummary = QStringLiteral("Sketch edge selected");
      snapshot_.inspectorTitle = QStringLiteral("Sketch geometry");
      snapshot_.inspectorStatus =
          QStringLiteral("Drag to resize · X toggles construction");
      snapshot_.fields = {
          {QStringLiteral("selection.identity"),
           QStringLiteral("Identity"),
           FieldKind::Text,
           entityId,
           {},
           {},
           true},
          {QStringLiteral("selection.revision"),
           QStringLiteral("Revision"),
           FieldKind::Text,
           snapshot_.projectRevision,
           {},
           {},
           true},
      };
      ++snapshot_.generation;
      return;
    }
    snapshot_.inspectorTitle = QStringLiteral("Selection");
    snapshot_.inspectorStatus =
        QStringLiteral("Read-only development projection");
    snapshot_.fields = {
        {QStringLiteral("selection.identity"),
         QStringLiteral("Identity"),
         FieldKind::Text,
         entityId,
         {},
         {},
         true},
        {QStringLiteral("selection.revision"),
         QStringLiteral("Revision"),
         FieldKind::Text,
         QStringLiteral("Not connected"),
         {},
         {},
         true},
    };
    ++snapshot_.generation;
  }

  void requestCommand(const QString &commandId) override {
    const QString statePrefix = QStringLiteral("development.state.");
    if (commandId.startsWith(statePrefix)) {
      const QString state = commandId.sliced(statePrefix.size());
      snapshot_.viewportState = state;
      snapshot_.activeCommandId.clear();
      snapshot_.viewportHeadline = state.left(1).toUpper() + state.mid(1);
      snapshot_.viewportDetail =
          QStringLiteral("Deterministic %1 state projection").arg(state);
      snapshot_.modelHealth = snapshot_.viewportHeadline;
      ++snapshot_.generation;
      return;
    }
    if (commandId.startsWith(QStringLiteral("project."))) {
      if (commandId.startsWith(QStringLiteral("project.create."))) {
        snapshot_.projectLengthUnitId = snapshot_.defaultLengthUnitId;
        snapshot_.gridSpacingLabel =
            gridSpacingFor(snapshot_.projectLengthUnitId);
        snapshot_.gridSpacingMillimeters =
            gridSpacingMillimetersFor(snapshot_.projectLengthUnitId);
        const auto projectUnits = std::find_if(
            snapshot_.preferences.begin(), snapshot_.preferences.end(),
            [](const PreferenceDescriptor &preference) {
              return preference.id == QStringLiteral("project-length-unit");
            });
        if (projectUnits != snapshot_.preferences.end())
          projectUnits->value = snapshot_.projectLengthUnitId;
      }
      snapshot_.activeCommandId.clear();
      snapshot_.fields.clear();
      snapshot_.commandDraft = {};
      clearSketchInteraction();
      snapshot_.inspectorStatus =
          QStringLiteral("Engineering backend disconnected");
      ++snapshot_.generation;
      return;
    }
    std::optional<CommandForm> form =
        commandFormFor(commandId, snapshot_.gridSpacingLabel);
    const auto descriptor = std::ranges::find_if(
        snapshot_.commandCatalog, [&commandId](const CommandDescriptor &item) {
          return item.id == commandId;
        });
    if (!form) {
      snapshot_.activeCommandId.clear();
      snapshot_.fields.clear();
      snapshot_.commandDraft = {};
      snapshot_.inspectorTitle = descriptor == snapshot_.commandCatalog.end()
                                     ? commandId
                                     : descriptor->label;
      snapshot_.inspectorStatus =
          descriptor == snapshot_.commandCatalog.end()
              ? QStringLiteral("Action is not registered")
              : descriptor->unavailableReason;
      ++snapshot_.generation;
      return;
    }
    if (descriptor == snapshot_.commandCatalog.end() ||
        !descriptor->available) {
      snapshot_.activeCommandId.clear();
      snapshot_.fields.clear();
      snapshot_.commandDraft = {};
      snapshot_.inspectorTitle = commandId;
      snapshot_.inspectorStatus = QStringLiteral("Action is not registered");
      ++snapshot_.generation;
      return;
    }
    if (localMode_ && commandId == QStringLiteral("sketch.rectangle")) {
      form->fields.front().value = QStringLiteral("corner");
      form->fields.front().options = {
          {QStringLiteral("corner"), QStringLiteral("Corner")}};
    }
    if (!descriptor->workspaceId.isEmpty() &&
        descriptor->workspaceId != snapshot_.activeWorkspaceId) {
      snapshot_.activeWorkspaceId = descriptor->workspaceId;
      selectFunctionForWorkspace(snapshot_.activeWorkspaceId);
      snapshot_.commands = commandsFor(snapshot_.activeWorkspaceId);
      snapshot_.selectionSummary = QStringLiteral("Nothing selected");
      restoreWorkspaceViewport();
    }
    snapshot_.activeCommandId = commandId;
    snapshot_.inspectorTitle = form->title;
    snapshot_.inspectorStatus = form->guidance;
    snapshot_.fields = form->fields;
    snapshot_.commandDraft = {
        commandId,
        snapshot_.projectRevision,
        CommandDraftState::Editing,
        form->previewSupported,
        form->applySupported,
    };
    beginSketchInteraction(*form);
    ++snapshot_.generation;
  }

  void setPreference(const QString &preferenceId,
                     const PreferenceValue &value) override {
    const auto found =
        std::find_if(snapshot_.preferences.begin(), snapshot_.preferences.end(),
                     [&preferenceId](const PreferenceDescriptor &preference) {
                       return preference.id == preferenceId;
                     });
    if (found == snapshot_.preferences.end() || !found->enabled ||
        (found->kind == PreferenceKind::Toggle) !=
            std::holds_alternative<bool>(value))
      return;
    if (found->kind == PreferenceKind::Choice &&
        !containsOption(found->options, std::get<QString>(value)))
      return;
    if (found->value == value)
      return;
    found->value = value;
    if (preferenceId == QStringLiteral("default-length-unit"))
      snapshot_.defaultLengthUnitId = std::get<QString>(value);
    if (preferenceId == QStringLiteral("interface-density"))
      snapshot_.interfaceDensityId = std::get<QString>(value);
    if (preferenceId == QStringLiteral("project-length-unit")) {
      snapshot_.projectLengthUnitId = std::get<QString>(value);
      snapshot_.gridSpacingLabel =
          gridSpacingFor(snapshot_.projectLengthUnitId);
      snapshot_.gridSpacingMillimeters =
          gridSpacingMillimetersFor(snapshot_.projectLengthUnitId);
    }
    ++snapshot_.generation;
  }

  void replacePreferenceOptions(const QString &preferenceId,
                                std::vector<UiOption> options,
                                const QString &value) override {
    const auto found =
        std::find_if(snapshot_.preferences.begin(), snapshot_.preferences.end(),
                     [&preferenceId](const PreferenceDescriptor &preference) {
                       return preference.id == preferenceId;
                     });
    if (found == snapshot_.preferences.end() ||
        found->kind != PreferenceKind::Choice ||
        !containsOption(options, value) ||
        (found->options == options && std::get<QString>(found->value) == value))
      return;
    found->options = std::move(options);
    found->value = value;
    ++snapshot_.generation;
  }

  void editField(const QString &fieldId, const FieldValue &value) override {
    const auto found =
        std::find_if(snapshot_.fields.begin(), snapshot_.fields.end(),
                     [&fieldId](const FieldDescriptor &field) {
                       return field.id == fieldId;
                     });
    if (found == snapshot_.fields.end() || found->readOnly ||
        found->value == value)
      return;
    if ((found->kind == FieldKind::Toggle) !=
        std::holds_alternative<bool>(value))
      return;
    if (found->kind == FieldKind::Choice &&
        !containsOption(found->options, std::get<QString>(value)))
      return;
    found->value = value;
    if (found->kind == FieldKind::Expression)
      found->effectiveValue = QStringLiteral("Pending preview");
    snapshot_.commandDraft.state = CommandDraftState::Editing;
    snapshot_.inspectorStatus = QStringLiteral("Draft changed");
    if (snapshot_.sketchInteraction.inputKind != SketchInputKind::None) {
      updateSketchInteractionRule();
      rebuildSketchProjection();
      updateSketchReadiness();
    }
    ++snapshot_.generation;
  }

  bool submitSketchInput(const SketchInputRequest &request) override {
    if (snapshot_.activeWorkspaceId != QStringLiteral("sketch") ||
        request.commandId != snapshot_.activeCommandId ||
        request.commandId != snapshot_.sketchInteraction.commandId ||
        request.expectedRevision !=
            snapshot_.sketchInteraction.expectedRevision ||
        request.expectedRevision != snapshot_.projectRevision ||
        request.kind != snapshot_.sketchInteraction.inputKind ||
        request.kind == SketchInputKind::None)
      return false;

    const int maximum = snapshot_.sketchInteraction.maximumInputCount;
    if (maximum > 0 && static_cast<int>(sketchInputs_.size()) >= maximum)
      return false;

    if (request.kind == SketchInputKind::PlanePoint) {
      if (!std::isfinite(request.planePoint.xMetres) ||
          !std::isfinite(request.planePoint.yMetres) ||
          std::abs(request.planePoint.xMetres) > 1'000'000.0 ||
          std::abs(request.planePoint.yMetres) > 1'000'000.0)
        return false;
    } else {
      const auto primitive = std::ranges::find_if(
          snapshot_.sketchProjection.primitives,
          [&request](const SketchPrimitiveProjection &candidate) {
            return candidate.id == request.entityId && !candidate.draft;
          });
      if (primitive == snapshot_.sketchProjection.primitives.end())
        return false;
      const std::size_t selectionIndex = sketchInputs_.size();
      const auto &sequence = snapshot_.sketchInteraction.selectionSequence;
      const SketchSelectionKind required =
          sequence.empty()
              ? SketchSelectionKind::Any
              : sequence[std::min(selectionIndex, sequence.size() - 1)];
      const bool pointExists =
          !request.subElementKey.isEmpty() &&
          std::ranges::find(primitive->pointKeys, request.subElementKey) !=
              primitive->pointKeys.end();
      if ((required == SketchSelectionKind::Point && !pointExists) ||
          (required == SketchSelectionKind::Curve &&
           !request.subElementKey.isEmpty()) ||
          (!request.subElementKey.isEmpty() && !pointExists) ||
          std::ranges::any_of(sketchInputs_, [&request](const auto &input) {
            return input.entityId == request.entityId &&
                   input.subElementKey == request.subElementKey;
          }))
        return false;
    }

    sketchInputs_.push_back(request);
    rebuildSketchProjection();
    updateSketchReadiness();
    ++snapshot_.generation;
    if (localMode_ && request.commandId == QStringLiteral("sketch.rectangle") &&
        snapshot_.sketchInteraction.maximumInputCount > 0 &&
        snapshot_.sketchInteraction.inputCount ==
            snapshot_.sketchInteraction.maximumInputCount)
      return submitLocalRectangle();
    return true;
  }

  bool toggleSketchConstruction() override {
    if (!localMode_ || !localSketchSession_ ||
        snapshot_.activeWorkspaceId != QStringLiteral("sketch") ||
        snapshot_.selectedSketchEntityId.isEmpty() ||
        localSketchSession_->pendingOperationCount() != 0U)
      return false;
    localEditEntity_ = snapshot_.selectedSketchEntityId;
    const bool queued = localSketchSession_->toggleConstruction(
        {localEditEntity_}, [this](Result<LocalSketchProjection> result) {
          completeLocalOperation(QStringLiteral("sketch.construction.toggle"),
                                 std::move(result));
        });
    if (queued)
      setLocalOperationPending();
    snapshot_.inspectorStatus =
        queued ? QStringLiteral("Updating construction geometry")
               : QStringLiteral("Sketch engineering queue is full");
    ++snapshot_.generation;
    return queued;
  }

  bool dragSketchCurve(const QString &entityId, PlanePoint first,
                       PlanePoint current) override {
    if (!localMode_ || !localSketchSession_ ||
        snapshot_.activeWorkspaceId != QStringLiteral("sketch") ||
        !snapshot_.activeCommandId.isEmpty() || entityId.isEmpty() ||
        localSketchSession_->pendingOperationCount() != 0U)
      return false;
    localEditEntity_ = entityId;
    const bool queued = localSketchSession_->dragCurve(
        {entityId, first.xMetres, first.yMetres, current.xMetres,
         current.yMetres},
        [this](Result<LocalSketchProjection> result) {
          completeLocalOperation(QStringLiteral("sketch.curve.drag"),
                                 std::move(result));
        });
    if (queued)
      setLocalOperationPending();
    snapshot_.inspectorStatus =
        queued ? QStringLiteral("Resizing Sketch geometry")
               : QStringLiteral("Sketch engineering queue is full");
    ++snapshot_.generation;
    return queued;
  }

  bool submitCommandDraft(const CommandDraftRequest &request,
                          CommandDraftMode mode) override {
    if (request.commandId != snapshot_.activeCommandId ||
        request.commandId != snapshot_.commandDraft.commandId ||
        request.expectedRevision != snapshot_.commandDraft.baseRevision ||
        request.fields.size() != snapshot_.fields.size()) {
      snapshot_.commandDraft.state = CommandDraftState::Rejected;
      snapshot_.inspectorStatus = QStringLiteral("Command draft was rejected");
      ++snapshot_.generation;
      return false;
    }
    if (request.expectedRevision != snapshot_.projectRevision) {
      snapshot_.commandDraft.state = CommandDraftState::Stale;
      snapshot_.inspectorStatus =
          QStringLiteral("Project revision changed; refresh the command draft");
      ++snapshot_.generation;
      return false;
    }
    for (std::size_t index = 0; index < request.fields.size(); ++index) {
      if (request.fields[index].id != snapshot_.fields[index].id ||
          request.fields[index].value != snapshot_.fields[index].value) {
        snapshot_.commandDraft.state = CommandDraftState::Rejected;
        snapshot_.inspectorStatus =
            QStringLiteral("Command fields differ from the active draft");
        ++snapshot_.generation;
        return false;
      }
    }
    if (localMode_ && mode == CommandDraftMode::Apply &&
        request.commandId == QStringLiteral("model.sketch.create"))
      return submitLocalSketchCreation();
    if (localMode_ && mode == CommandDraftMode::Apply &&
        request.commandId == QStringLiteral("sketch.rectangle"))
      return submitLocalRectangle();
    const bool supported = mode == CommandDraftMode::Preview
                               ? snapshot_.commandDraft.previewSupported
                               : snapshot_.commandDraft.applySupported;
    if (!supported) {
      snapshot_.commandDraft.state = CommandDraftState::Rejected;
      snapshot_.inspectorStatus =
          QStringLiteral("Complete the required viewport interaction first");
      ++snapshot_.generation;
      return false;
    }
    if (mode == CommandDraftMode::Preview) {
      snapshot_.commandDraft.state = CommandDraftState::Preview;
      snapshot_.viewportState = QStringLiteral("preview");
      snapshot_.viewportHeadline = QStringLiteral("Command preview");
      snapshot_.viewportDetail =
          QStringLiteral("Draft is valid for UI review; evaluator unavailable");
      snapshot_.inspectorStatus =
          QStringLiteral("Preview request ready; evaluator unavailable");
    } else {
      snapshot_.commandDraft.state = CommandDraftState::Unavailable;
      restoreWorkspaceViewport();
      snapshot_.inspectorStatus = QStringLiteral(
          "Apply request ready; engineering backend unavailable");
    }
    ++snapshot_.generation;
    return true;
  }

  void cancelCommandDraft(const QString &commandId) override {
    if (commandId != snapshot_.activeCommandId)
      return;
    snapshot_.activeCommandId.clear();
    snapshot_.fields.clear();
    snapshot_.commandDraft = {};
    clearSketchInteraction();
    snapshot_.inspectorTitle = QStringLiteral("Selection");
    snapshot_.inspectorStatus =
        QStringLiteral("Engineering backend disconnected");
    restoreWorkspaceViewport();
    ++snapshot_.generation;
  }

  bool submitParameterEdit(const ParameterEditRequest &request) override {
    const auto parameter = std::ranges::find_if(
        snapshot_.parameters, [&request](const ParameterSummary &candidate) {
          return candidate.id == request.parameterId;
        });
    if (parameter == snapshot_.parameters.end() ||
        request.expectedRevision != snapshot_.projectRevision ||
        request.expression.trimmed().isEmpty() ||
        request.expression.size() > 4096) {
      snapshot_.inspectorStatus =
          QStringLiteral("Parameter revision rejected; refresh the editor");
      ++snapshot_.generation;
      return false;
    }
    parameter->expression = request.expression.trimmed();
    parameter->value = QStringLiteral("pending evaluation");
    const QString inputId = QStringLiteral("input.") + parameter->id;
    const auto input =
        std::ranges::find_if(snapshot_.selectedFunction.inputs,
                             [&inputId](const FunctionPortSummary &candidate) {
                               return candidate.id == inputId;
                             });
    if (input != snapshot_.selectedFunction.inputs.end()) {
      input->value = parameter->expression;
      input->state = QStringLiteral("unresolved");
    }
    if (snapshot_.selectedFunction.id == basePlateFunction_.id)
      basePlateFunction_ = snapshot_.selectedFunction;
    else if (snapshot_.selectedFunction.id == mountingProfileFunction_.id)
      mountingProfileFunction_ = snapshot_.selectedFunction;
    snapshot_.projectRevision =
        QStringLiteral("development.project.%1").arg(snapshot_.generation + 1);
    snapshot_.revisionLabel = snapshot_.projectRevision;
    if (!snapshot_.activeCommandId.isEmpty())
      snapshot_.commandDraft.state = CommandDraftState::Stale;
    snapshot_.inspectorStatus =
        QStringLiteral("Parameter revision accepted; evaluator unavailable");
    ++snapshot_.generation;
    return true;
  }

  bool submitSourceEdit(const SourceEditRequest &request,
                        SourceEditMode mode) override {
    if (request.functionId != snapshot_.selectedFunction.id ||
        request.sourcePath != snapshot_.selectedFunction.sourcePath ||
        request.expectedRevision != snapshot_.selectedFunction.revision ||
        request.source.trimmed().isEmpty() ||
        request.source.size() > 1024 * 1024) {
      snapshot_.inspectorStatus =
          QStringLiteral("Source revision rejected; refresh the draft");
      ++snapshot_.generation;
      return false;
    }
    if (localMode_) {
      if (mode == SourceEditMode::Preview) {
        snapshot_.inspectorStatus = QStringLiteral(
            "Source diff ready; Apply validates and evaluates it");
        ++snapshot_.generation;
        return true;
      }
      return submitLocalSourceReplacement(request);
    }
    if (mode == SourceEditMode::Preview) {
      snapshot_.inspectorStatus = QStringLiteral(
          "Source revision ready for review; evaluator unavailable");
    } else {
      snapshot_.modelSource = request.source;
      snapshot_.selectedFunction.revision =
          QStringLiteral("development revision %1")
              .arg(snapshot_.generation + 1);
      basePlateFunction_.revision = snapshot_.selectedFunction.revision;
      mountingProfileFunction_.revision = snapshot_.selectedFunction.revision;
      snapshot_.sketchProjection.sourceRevision =
          snapshot_.selectedFunction.revision;
      snapshot_.projectRevision = QStringLiteral("development.project.%1")
                                      .arg(snapshot_.generation + 1);
      snapshot_.revisionLabel = snapshot_.projectRevision;
      if (!snapshot_.activeCommandId.isEmpty())
        snapshot_.commandDraft.state = CommandDraftState::Stale;
      snapshot_.inspectorStatus =
          QStringLiteral("Source revision accepted; evaluator unavailable");
    }
    ++snapshot_.generation;
    return true;
  }

  bool undo() override {
    if (localMode_)
      return submitLocalHistory(false);
    snapshot_.inspectorStatus =
        QStringLiteral("Undo unavailable; engineering backend disconnected");
    ++snapshot_.generation;
    return false;
  }

  bool redo() override {
    if (localMode_)
      return submitLocalHistory(true);
    snapshot_.inspectorStatus =
        QStringLiteral("Redo unavailable; engineering backend disconnected");
    ++snapshot_.generation;
    return false;
  }

private:
  std::vector<SketchPrimitiveProjection> baseSketchPrimitives() const {
    return localMode_ ? std::vector<SketchPrimitiveProjection>{}
                      : mountingProfileProjection();
  }

  void initializeLocalProjection() {
    snapshot_.projectName = QStringLiteral("Untitled");
    snapshot_.revisionLabel = QStringLiteral("Not created");
    snapshot_.projectRevision = QStringLiteral("not-created");
    snapshot_.backendConnected = false;
    snapshot_.sourceEditingAvailable = false;
    setLocalHistoryAvailability(false, false,
                                QStringLiteral("Create a Sketch first"));
    snapshot_.viewportState = QStringLiteral("loading");
    snapshot_.viewportHeadline = QStringLiteral("Model workspace");
    snapshot_.viewportDetail =
        QStringLiteral("Starting local engineering services");
    snapshot_.modelHealth = QStringLiteral("Starting local engineering");
    snapshot_.inspectorStatus = QStringLiteral("Starting local engineering");
    snapshot_.modelSource.clear();
    snapshot_.selectedFunction = {};
    snapshot_.sketchProjection = {
        {},
        {},
        QStringLiteral("reference.plane.xy"),
        snapshot_.projectLengthUnitId,
        QStringLiteral("not-created"),
        -1,
        {},
    };
    snapshot_.sketchScene.reset();
    snapshot_.structure = {
        {QStringLiteral("project.root"), QStringLiteral("Untitled"), 0,
         QStringLiteral("project")},
        {QStringLiteral("component.part"), QStringLiteral("Part"), 1,
         QStringLiteral("component")},
        {QStringLiteral("reference.origin"), QStringLiteral("Origin"), 2,
         QStringLiteral("group")},
        {QStringLiteral("reference.plane.xy"), QStringLiteral("XY Plane"), 3,
         QStringLiteral("plane")},
        {QStringLiteral("reference.plane.xz"), QStringLiteral("XZ Plane"), 3,
         QStringLiteral("plane")},
        {QStringLiteral("reference.plane.yz"), QStringLiteral("YZ Plane"), 3,
         QStringLiteral("plane")},
    };
    snapshot_.revisions.clear();
    snapshot_.parameters.clear();
    snapshot_.jobs = {
        {QStringLiteral("job.engineering"),
         QStringLiteral("Local engineering services"),
         QStringLiteral("Starting"), -1},
    };
    snapshot_.diagnostics.clear();
    snapshot_.proposals = {
        {QStringLiteral("proposal.codex"),
         QStringLiteral("Connect Codex to inspect or propose model changes."),
         QStringLiteral("Unavailable")},
    };
    snapshot_.recentProjects.clear();
    snapshot_.recoveryItems = {
        {QStringLiteral("recovery.none"), QStringLiteral("No recovery needed"),
         QStringLiteral("No local project has been created."),
         QStringLiteral("current"), false},
    };
    snapshot_.operations = {
        {QStringLiteral("operation.engineering"),
         QStringLiteral("Local engineering"), QStringLiteral("service"),
         QStringLiteral("pending"),
         QStringLiteral("Preparing the canonical source editor."), -1},
    };
    basePlateFunction_ = {};
    mountingProfileFunction_ = {};
  }

  void beginLocalPreparation() {
    localSketchSession_->whenReady([this](Result<void> result) {
      localBackendState_ =
          result ? LocalBackendState::Ready : LocalBackendState::Failed;
      snapshot_.backendConnected = result.has_value();
      if (result) {
        snapshot_.inspectorStatus = QStringLiteral("Local engineering ready");
        snapshot_.jobs = {
            {QStringLiteral("job.engineering"),
             QStringLiteral("Local engineering services"),
             QStringLiteral("Ready"), 100},
        };
        snapshot_.operations = {
            {QStringLiteral("operation.engineering"),
             QStringLiteral("Local engineering"), QStringLiteral("service"),
             QStringLiteral("current"),
             QStringLiteral("Ready to accept canonical commands."), 100},
        };
      } else {
        const QString summary = QString::fromStdString(result.error().summary);
        snapshot_.inspectorStatus = summary;
        snapshot_.jobs = {
            {QStringLiteral("job.engineering"),
             QStringLiteral("Local engineering services"),
             QStringLiteral("Failed"), -1},
        };
        snapshot_.operations = {
            {QStringLiteral("operation.engineering"),
             QStringLiteral("Local engineering"), QStringLiteral("service"),
             QStringLiteral("failed"), summary, -1},
        };
        snapshot_.diagnostics.insert(
            snapshot_.diagnostics.begin(),
            {QString::fromStdString(result.error().code),
             result.error().severity == Severity::Fatal
                 ? QStringLiteral("fatal")
                 : QStringLiteral("error"),
             summary});
      }
      restoreWorkspaceViewport();
      ++snapshot_.generation;
      notifyChanged();
    });
  }

  bool submitLocalSketchCreation() {
    if (!localSketchSession_ ||
        snapshot_.commandDraft.state == CommandDraftState::Pending)
      return false;
    const auto attachment = std::ranges::find_if(
        snapshot_.fields, [](const FieldDescriptor &field) {
          return field.id == QStringLiteral("model.sketch.create.attachment");
        });
    const auto plane =
        attachment == snapshot_.fields.end() ||
                !std::holds_alternative<QString>(attachment->value)
            ? std::optional<LocalSketchPlane>{}
            : localSketchPlaneFromId(std::get<QString>(attachment->value));
    if (!plane) {
      snapshot_.commandDraft.state = CommandDraftState::Rejected;
      snapshot_.inspectorStatus =
          QStringLiteral("Select an XY, XZ, or YZ datum plane");
      ++snapshot_.generation;
      return false;
    }
    const bool queued = localSketchSession_->create(
        {*plane}, [this](Result<LocalSketchProjection> result) {
          completeLocalOperation(QStringLiteral("model.sketch.create"),
                                 std::move(result));
        });
    snapshot_.commandDraft.state =
        queued ? CommandDraftState::Pending : CommandDraftState::Rejected;
    if (queued)
      setLocalOperationPending();
    snapshot_.inspectorStatus =
        queued ? QStringLiteral("Creating canonical Sketch source")
               : QStringLiteral("Sketch engineering queue is full");
    ++snapshot_.generation;
    return queued;
  }

  bool submitLocalRectangle() {
    if (!localSketchSession_ || sketchInputs_.size() != 2U ||
        snapshot_.commandDraft.state == CommandDraftState::Pending)
      return false;
    const LocalRectangleGesture gesture{
        sketchInputs_[0].planePoint.xMetres,
        sketchInputs_[0].planePoint.yMetres,
        sketchInputs_[1].planePoint.xMetres,
        sketchInputs_[1].planePoint.yMetres,
        activeSketchConstruction(),
    };
    const bool queued = localSketchSession_->applyRectangle(
        gesture, [this](Result<LocalSketchProjection> result) {
          completeLocalOperation(QStringLiteral("sketch.rectangle"),
                                 std::move(result));
        });
    snapshot_.commandDraft.state =
        queued ? CommandDraftState::Pending : CommandDraftState::Rejected;
    if (queued)
      setLocalOperationPending();
    snapshot_.inspectorStatus =
        queued ? QStringLiteral("Applying rectangle to canonical source")
               : QStringLiteral("Sketch engineering queue is full");
    ++snapshot_.generation;
    return queued;
  }

  bool submitLocalSourceReplacement(const SourceEditRequest &request) {
    if (!localSketchSession_ || !snapshot_.sourceEditingAvailable)
      return false;
    const bool queued = localSketchSession_->replaceSource(
        {request.expectedRevision, request.source},
        [this](Result<LocalSketchProjection> result) {
          completeLocalOperation(QStringLiteral("source.replace"),
                                 std::move(result));
        });
    if (queued)
      setLocalOperationPending();
    snapshot_.inspectorStatus =
        queued ? QStringLiteral("Validating and evaluating canonical source")
               : QStringLiteral("Sketch engineering queue is full");
    ++snapshot_.generation;
    return queued;
  }

  bool submitLocalHistory(bool redo) {
    if (!localSketchSession_ ||
        localSketchSession_->pendingOperationCount() != 0U ||
        (redo ? !snapshot_.canRedo : !snapshot_.canUndo))
      return false;
    const QString commandId =
        redo ? QStringLiteral("version.redo") : QStringLiteral("version.undo");
    auto completion = [this, commandId](Result<LocalSketchProjection> result) {
      completeLocalOperation(commandId, std::move(result));
    };
    const bool queued = redo ? localSketchSession_->redo(std::move(completion))
                             : localSketchSession_->undo(std::move(completion));
    if (!queued) {
      snapshot_.inspectorStatus =
          QStringLiteral("Sketch engineering queue is full");
      ++snapshot_.generation;
      return false;
    }
    setLocalOperationPending();
    snapshot_.inspectorStatus =
        redo ? QStringLiteral("Restoring next canonical Sketch revision")
             : QStringLiteral("Restoring previous canonical Sketch revision");
    ++snapshot_.generation;
    return true;
  }

  void setLocalOperationPending() {
    snapshot_.sourceEditingAvailable = false;
    setLocalHistoryAvailability(
        false, false, QStringLiteral("Engineering operation pending"));
  }

  void setLocalHistoryAvailability(bool canUndo, bool canRedo,
                                   const QString &pendingReason = {}) {
    snapshot_.canUndo = canUndo;
    snapshot_.canRedo = canRedo;
    const auto update = [&](std::vector<CommandDescriptor> &commands) {
      for (CommandDescriptor &command : commands) {
        if (command.id == QStringLiteral("version.undo")) {
          command.available = canUndo;
          command.shortcut = QStringLiteral("Ctrl+Z");
          command.unavailableReason =
              canUndo ? QString{}
                      : (pendingReason.isEmpty()
                             ? QStringLiteral("No earlier Sketch revision")
                             : pendingReason);
        } else if (command.id == QStringLiteral("version.redo")) {
          command.available = canRedo;
          command.shortcut = QStringLiteral("Ctrl+Shift+Z");
          command.unavailableReason =
              canRedo ? QString{}
                      : (pendingReason.isEmpty()
                             ? QStringLiteral("No later Sketch revision")
                             : pendingReason);
        }
      }
    };
    update(snapshot_.historyCommands);
    update(snapshot_.commandCatalog);
  }

  void applyLocalProjection(const LocalSketchProjection &projection,
                            const QString &commandId) {
    snapshot_.projectRevision = projection.projectRevision;
    snapshot_.revisionLabel = projection.projectRevision.left(18);
    snapshot_.modelSource = projection.source;
    snapshot_.sourceEditingAvailable = true;
    historyCanUndo_ = projection.canUndo;
    historyCanRedo_ = projection.canRedo;
    setLocalHistoryAvailability(historyCanUndo_, historyCanRedo_);
    snapshot_.sketchScene = projection.scene;
    localSketchFunction_ = {
        QStringLiteral("function.sketch"),
        projection.functionName,
        QStringLiteral("sketch() → profile: Sketch"),
        projection.sourcePath,
        QStringLiteral("Python / build123d"),
        QStringLiteral("canonical"),
        projection.sourceRevision,
        {},
        {{QStringLiteral("output.profile"), QStringLiteral("profile"),
          QStringLiteral("Sketch"), QStringLiteral("Evaluated"),
          QStringLiteral("current")}},
    };
    snapshot_.selectedFunction = localSketchFunction_;
    snapshot_.sketchProjection = {
        projection.sourceRevision,
        localSketchFunction_.id,
        localSketchPlaneId(projection.plane),
        snapshot_.projectLengthUnitId,
        projection.solveStatus,
        projection.degreesOfFreedom,
        {},
    };
    snapshot_.structure = {
        {QStringLiteral("project.root"), QStringLiteral("Untitled"), 0,
         QStringLiteral("project")},
        {QStringLiteral("component.part"), QStringLiteral("Part"), 1,
         QStringLiteral("component")},
        {QStringLiteral("reference.origin"), QStringLiteral("Origin"), 2,
         QStringLiteral("group")},
        {QStringLiteral("reference.plane.xy"), QStringLiteral("XY Plane"), 3,
         QStringLiteral("plane")},
        {QStringLiteral("reference.plane.xz"), QStringLiteral("XZ Plane"), 3,
         QStringLiteral("plane")},
        {QStringLiteral("reference.plane.yz"), QStringLiteral("YZ Plane"), 3,
         QStringLiteral("plane")},
        {localSketchFunction_.id, QStringLiteral("Sketch 1"), 2,
         QStringLiteral("model-function")},
    };
    for (std::size_t index = 0; index < projection.profileCount; ++index)
      snapshot_.structure.push_back(
          {QStringLiteral("output.sketch.profile.%1").arg(index + 1U),
           QStringLiteral("Profile %1").arg(index + 1U), 3,
           QStringLiteral("sketch-profile")});
    if (!commandId.startsWith(QStringLiteral("version."))) {
      snapshot_.revisions.insert(
          snapshot_.revisions.begin(),
          {projection.projectRevision,
           commandId == QStringLiteral("model.sketch.create")
               ? QStringLiteral("Create Sketch")
           : commandId == QStringLiteral("sketch.rectangle")
               ? QStringLiteral("Rectangle")
           : commandId == QStringLiteral("sketch.curve.drag")
               ? QStringLiteral("Resize Sketch geometry")
           : commandId == QStringLiteral("sketch.construction.toggle")
               ? QStringLiteral("Toggle construction geometry")
               : QStringLiteral("Source edit"),
           QStringLiteral("%1 · %2 DOF")
               .arg(projection.solveStatus)
               .arg(projection.degreesOfFreedom)});
    }
    snapshot_.modelHealth = QStringLiteral("%1 · %2 DOF")
                                .arg(projection.solveStatus)
                                .arg(projection.degreesOfFreedom);
    snapshot_.viewportState = QStringLiteral("current");
    snapshot_.viewportHeadline = QStringLiteral("Sketch plane");
    snapshot_.viewportDetail =
        QStringLiteral("Canonical source and evaluated geometry");
    snapshot_.selectionSummary = QStringLiteral("Nothing selected");
    switch (projection.plane) {
    case LocalSketchPlane::XY:
      snapshot_.gridPlaneLabel = QStringLiteral("XY");
      break;
    case LocalSketchPlane::XZ:
      snapshot_.gridPlaneLabel = QStringLiteral("XZ");
      break;
    case LocalSketchPlane::YZ:
      snapshot_.gridPlaneLabel = QStringLiteral("YZ");
      break;
    }
    snapshot_.diagnostics.clear();
  }

  void completeLocalOperation(const QString &commandId,
                              Result<LocalSketchProjection> result) {
    if (!result) {
      if (commandId == QStringLiteral("sketch.rectangle")) {
        sketchInputs_.clear();
        snapshot_.sketchProjection.primitives.clear();
        snapshot_.sketchInteraction.expectedRevision =
            snapshot_.projectRevision;
        snapshot_.sketchInteraction.inputCount = 0;
        snapshot_.commandDraft.baseRevision = snapshot_.projectRevision;
        snapshot_.commandDraft.state = CommandDraftState::Editing;
        snapshot_.commandDraft.previewSupported = false;
        snapshot_.commandDraft.applySupported = false;
      } else {
        snapshot_.commandDraft.state = CommandDraftState::Rejected;
      }
      snapshot_.inspectorStatus =
          QString::fromStdString(result.error().summary);
      snapshot_.diagnostics.insert(
          snapshot_.diagnostics.begin(),
          {QString::fromStdString(result.error().code),
           result.error().severity == Severity::Fatal ? QStringLiteral("fatal")
                                                      : QStringLiteral("error"),
           QString::fromStdString(result.error().summary)});
      snapshot_.sourceEditingAvailable = !localSketchFunction_.id.isEmpty();
      setLocalHistoryAvailability(historyCanUndo_, historyCanRedo_);
      ++snapshot_.generation;
      notifyChanged();
      return;
    }

    applyLocalProjection(*result, commandId);
    if (commandId == QStringLiteral("model.sketch.create")) {
      snapshot_.activeWorkspaceId = QStringLiteral("sketch");
      snapshot_.commands = commandsFor(snapshot_.activeWorkspaceId);
      snapshot_.activeCommandId.clear();
      snapshot_.fields.clear();
      snapshot_.commandDraft = {};
      clearSketchInteraction();
      snapshot_.inspectorTitle = QStringLiteral("Sketch");
      snapshot_.inspectorStatus =
          QStringLiteral("Sketch created · choose a geometry tool");
    } else if (commandId == QStringLiteral("sketch.rectangle")) {
      sketchInputs_.clear();
      snapshot_.sketchProjection.primitives.clear();
      snapshot_.activeCommandId.clear();
      snapshot_.fields.clear();
      snapshot_.commandDraft = {};
      clearSketchInteraction();
      snapshot_.inspectorTitle = QStringLiteral("Sketch");
      snapshot_.inspectorStatus =
          QStringLiteral("Rectangle created · Select active");
    } else if (commandId == QStringLiteral("sketch.curve.drag") ||
               commandId == QStringLiteral("sketch.construction.toggle")) {
      snapshot_.activeCommandId.clear();
      snapshot_.fields.clear();
      snapshot_.commandDraft = {};
      clearSketchInteraction();
      snapshot_.selectedSketchEntityId = localEditEntity_;
      snapshot_.selectionSummary = QStringLiteral("Sketch edge selected");
      snapshot_.inspectorTitle = QStringLiteral("Sketch geometry");
      snapshot_.inspectorStatus =
          commandId == QStringLiteral("sketch.curve.drag")
              ? QStringLiteral("Geometry resized · Select active")
              : QStringLiteral("Construction state changed · Select active");
    } else if (commandId == QStringLiteral("source.replace")) {
      snapshot_.activeCommandId.clear();
      snapshot_.fields.clear();
      snapshot_.commandDraft = {};
      clearSketchInteraction();
      snapshot_.inspectorTitle = QStringLiteral("Sketch");
      snapshot_.inspectorStatus =
          QStringLiteral("Canonical source committed and evaluated");
    } else {
      snapshot_.activeCommandId.clear();
      snapshot_.fields.clear();
      snapshot_.commandDraft = {};
      clearSketchInteraction();
      snapshot_.inspectorTitle = QStringLiteral("Sketch");
      snapshot_.inspectorStatus = commandId == QStringLiteral("version.redo")
                                      ? QStringLiteral("Redo complete")
                                      : QStringLiteral("Undo complete");
    }
    ++snapshot_.generation;
    notifyChanged();
  }

  void notifyChanged() {
    if (changeHandler_)
      changeHandler_();
  }

  bool activeSketchConstruction() const {
    const auto field = std::ranges::find_if(
        snapshot_.fields, [](const FieldDescriptor &candidate) {
          return candidate.id.endsWith(QStringLiteral(".construction"));
        });
    return field != snapshot_.fields.end() &&
           std::holds_alternative<bool>(field->value) &&
           std::get<bool>(field->value);
  }

  QString activeChoice(const QString &suffix) const {
    const auto field = std::ranges::find_if(
        snapshot_.fields, [&suffix](const FieldDescriptor &candidate) {
          return candidate.id.endsWith(suffix);
        });
    return field != snapshot_.fields.end() &&
                   std::holds_alternative<QString>(field->value)
               ? std::get<QString>(field->value)
               : QString{};
  }

  void beginSketchInteraction(const CommandForm &form) {
    sketchInputs_.clear();
    snapshot_.sketchInteraction = {
        snapshot_.activeCommandId,
        snapshot_.projectRevision,
        form.sketchInputKind,
        form.minimumSketchInputs,
        form.maximumSketchInputs,
        0,
        form.guidance,
        form.sketchSelectionSequence,
    };
    rebuildSketchProjection();
    updateSketchInteractionRule();
    updateSketchReadiness();
  }

  void clearSketchInteraction() {
    sketchInputs_.clear();
    snapshot_.sketchInteraction = {};
    snapshot_.sketchProjection.primitives = baseSketchPrimitives();
    if (snapshot_.activeWorkspaceId == QStringLiteral("sketch"))
      snapshot_.selectionSummary = QStringLiteral("Nothing selected");
  }

  void updateSketchInteractionRule() {
    if (snapshot_.sketchInteraction.inputKind != SketchInputKind::PlanePoint)
      return;
    if (snapshot_.activeCommandId == QStringLiteral("sketch.rectangle") ||
        snapshot_.activeCommandId == QStringLiteral("sketch.circle")) {
      const bool threePoint = activeChoice(QStringLiteral(".method")) ==
                              QStringLiteral("three-point");
      snapshot_.sketchInteraction.minimumInputCount = threePoint ? 3 : 2;
      snapshot_.sketchInteraction.maximumInputCount = threePoint ? 3 : 2;
    }
    while (snapshot_.sketchInteraction.maximumInputCount > 0 &&
           static_cast<int>(sketchInputs_.size()) >
               snapshot_.sketchInteraction.maximumInputCount)
      sketchInputs_.pop_back();
  }

  void updateSketchReadiness() {
    auto &interaction = snapshot_.sketchInteraction;
    interaction.inputCount = static_cast<int>(sketchInputs_.size());
    if (interaction.inputKind == SketchInputKind::None)
      return;
    const bool ready = interaction.inputCount >= interaction.minimumInputCount;
    if (interaction.inputKind == SketchInputKind::Entity) {
      snapshot_.selectionSummary =
          interaction.inputCount == 0   ? QStringLiteral("Nothing selected")
          : interaction.inputCount == 1 ? QStringLiteral("1 Sketch selection")
                                        : QStringLiteral("%1 Sketch selections")
                                              .arg(interaction.inputCount);
    }
    snapshot_.commandDraft.previewSupported = ready;
    snapshot_.commandDraft.applySupported = ready;
    snapshot_.inspectorStatus =
        ready ? (interaction.maximumInputCount == 0
                     ? QStringLiteral("Continue choosing points or preview the "
                                      "source change")
                     : QStringLiteral("Viewport input complete; preview or "
                                      "apply the source change"))
              : QStringLiteral("%1 · %2 of %3")
                    .arg(interaction.prompt)
                    .arg(interaction.inputCount)
                    .arg(interaction.minimumInputCount);
  }

  void appendDraftLine(const PlanePoint &start, const PlanePoint &end,
                       int index, bool construction) {
    snapshot_.sketchProjection.primitives.push_back(
        {QStringLiteral("draft.%1").arg(index),
         SketchPrimitiveKind::Line,
         {start, end},
         {QStringLiteral("start"), QStringLiteral("end")},
         {},
         0.0,
         construction,
         false,
         true});
  }

  void rebuildSketchProjection() {
    snapshot_.sketchProjection.primitives = baseSketchPrimitives();
    if (snapshot_.sketchInteraction.inputKind == SketchInputKind::Entity) {
      for (SketchPrimitiveProjection &primitive :
           snapshot_.sketchProjection.primitives) {
        primitive.selected = std::ranges::any_of(
            sketchInputs_, [&primitive](const SketchInputRequest &input) {
              return input.entityId == primitive.id &&
                     input.subElementKey.isEmpty();
            });
        for (const SketchInputRequest &input : sketchInputs_) {
          if (input.entityId == primitive.id && !input.subElementKey.isEmpty())
            primitive.selectedPointKeys.push_back(input.subElementKey);
        }
      }
      return;
    }

    std::vector<PlanePoint> points;
    points.reserve(sketchInputs_.size());
    for (const SketchInputRequest &input : sketchInputs_)
      points.push_back(input.planePoint);
    if (points.empty())
      return;

    const QString command = snapshot_.activeCommandId;
    const bool construction = activeSketchConstruction();
    if (command == QStringLiteral("sketch.point")) {
      snapshot_.sketchProjection.primitives.push_back(
          {QStringLiteral("draft.0"),
           SketchPrimitiveKind::Point,
           {points.front()},
           {QStringLiteral("point")},
           {},
           0.0,
           construction,
           false,
           true});
      return;
    }
    if (command == QStringLiteral("sketch.line") ||
        command == QStringLiteral("sketch.polyline")) {
      if (points.size() == 1) {
        snapshot_.sketchProjection.primitives.push_back(
            {QStringLiteral("draft.anchor"),
             SketchPrimitiveKind::Point,
             {points.front()},
             {QStringLiteral("point")},
             {},
             0.0,
             construction,
             false,
             true});
      }
      for (std::size_t index = 1; index < points.size(); ++index)
        appendDraftLine(points[index - 1], points[index],
                        static_cast<int>(index - 1), construction);
      return;
    }
    if (command == QStringLiteral("sketch.rectangle") && points.size() >= 2) {
      std::array<PlanePoint, 4> corners{};
      const QString method = activeChoice(QStringLiteral(".method"));
      if (method == QStringLiteral("center")) {
        corners = {
            {{2.0 * points[0].xMetres - points[1].xMetres,
              2.0 * points[0].yMetres - points[1].yMetres},
             {points[1].xMetres, 2.0 * points[0].yMetres - points[1].yMetres},
             points[1],
             {2.0 * points[0].xMetres - points[1].xMetres, points[1].yMetres}}};
      } else if (method == QStringLiteral("three-point") &&
                 points.size() >= 3) {
        const double dx = points[1].xMetres - points[0].xMetres;
        const double dy = points[1].yMetres - points[0].yMetres;
        const double length = std::hypot(dx, dy);
        const double safeLength = std::max(length, 1e-9);
        const double height = ((points[2].xMetres - points[0].xMetres) * -dy +
                               (points[2].yMetres - points[0].yMetres) * dx) /
                              safeLength;
        const PlanePoint offset{-dy / safeLength * height,
                                dx / safeLength * height};
        corners = {{points[0],
                    points[1],
                    {points[1].xMetres + offset.xMetres,
                     points[1].yMetres + offset.yMetres},
                    {points[0].xMetres + offset.xMetres,
                     points[0].yMetres + offset.yMetres}}};
      } else {
        corners = {{{points[0].xMetres, points[0].yMetres},
                    {points[1].xMetres, points[0].yMetres},
                    {points[1].xMetres, points[1].yMetres},
                    {points[0].xMetres, points[1].yMetres}}};
      }
      for (int index = 0; index < 4; ++index)
        appendDraftLine(corners[index], corners[(index + 1) % 4], index,
                        construction);
      return;
    }
    if (command == QStringLiteral("sketch.circle") && points.size() >= 2) {
      PlanePoint center = points[0];
      double radius = std::hypot(points[1].xMetres - center.xMetres,
                                 points[1].yMetres - center.yMetres);
      if (activeChoice(QStringLiteral(".method")) ==
              QStringLiteral("three-point") &&
          points.size() >= 3) {
        const auto circle = circumcircle(points[0], points[1], points[2]);
        if (!circle)
          return;
        center = circle->center;
        radius = circle->radiusMetres;
      }
      snapshot_.sketchProjection.primitives.push_back(
          {QStringLiteral("draft.0"),
           SketchPrimitiveKind::Circle,
           {center},
           {QStringLiteral("center")},
           {},
           radius,
           construction,
           false,
           true});
      return;
    }
    if (command == QStringLiteral("sketch.arc")) {
      if (points.size() < 3) {
        for (std::size_t index = 1; index < points.size(); ++index)
          appendDraftLine(points[index - 1], points[index],
                          static_cast<int>(index - 1), construction);
      } else {
        snapshot_.sketchProjection.primitives.push_back(
            {QStringLiteral("draft.0"),
             SketchPrimitiveKind::Arc,
             {points[0], points[1], points[2]},
             {QStringLiteral("start"), QStringLiteral("through"),
              QStringLiteral("end")},
             {},
             0.0,
             construction,
             false,
             true});
      }
    }
  }

  void restoreWorkspaceViewport() {
    if (snapshot_.activeWorkspaceId == QStringLiteral("model")) {
      snapshot_.viewportState = QStringLiteral("current");
      snapshot_.viewportHeadline = QStringLiteral("Navigation calibration");
      snapshot_.viewportDetail = QStringLiteral(
          "Deterministic fixture; not evaluated project geometry");
      snapshot_.modelHealth = QStringLiteral("Frontend contract current");
      if (localMode_) {
        snapshot_.viewportState =
            localBackendState_ == LocalBackendState::Ready
                ? QStringLiteral("current")
            : localBackendState_ == LocalBackendState::Failed
                ? QStringLiteral("unavailable")
                : QStringLiteral("loading");
        snapshot_.viewportHeadline = QStringLiteral("Model workspace");
        snapshot_.viewportDetail =
            localBackendState_ == LocalBackendState::Ready
                ? QStringLiteral("Local engineering services are ready")
            : localBackendState_ == LocalBackendState::Failed
                ? QStringLiteral("Local engineering services failed")
                : QStringLiteral("Starting local engineering services");
        snapshot_.modelHealth =
            localBackendState_ == LocalBackendState::Ready
                ? QStringLiteral("Local engineering ready")
            : localBackendState_ == LocalBackendState::Failed
                ? QStringLiteral("Local engineering failed")
                : QStringLiteral("Starting local engineering");
      }
      return;
    }
    snapshot_.viewportState = QStringLiteral("unavailable");
    if (snapshot_.activeWorkspaceId == QStringLiteral("sketch")) {
      snapshot_.viewportState = QStringLiteral("current");
      snapshot_.viewportHeadline = QStringLiteral("Sketch input calibration");
      snapshot_.viewportDetail = QStringLiteral(
          "Deterministic fixture; solver and evaluator unavailable");
      snapshot_.modelHealth =
          QStringLiteral("Sketch frontend contract current");
      if (localMode_) {
        snapshot_.viewportHeadline = QStringLiteral("Sketch plane");
        snapshot_.viewportDetail = snapshot_.sketchScene
                                       ? QStringLiteral("Canonical source and "
                                                        "evaluated geometry")
                                       : QStringLiteral("Create or edit Sketch "
                                                        "geometry");
        snapshot_.modelHealth = snapshot_.sketchProjection.solveStatus;
      }
      return;
    } else {
      const QString label =
          workspaceLabel(snapshot_.workspaces, snapshot_.activeWorkspaceId);
      snapshot_.viewportHeadline = label + QStringLiteral(" unavailable");
      snapshot_.viewportDetail = QStringLiteral(
          "UI shell only; engineering capability is not connected");
    }
    snapshot_.modelHealth = QStringLiteral("Engineering backend unavailable");
  }

  void selectFunctionForWorkspace(const QString &workspaceId) {
    if (localMode_) {
      snapshot_.selectedFunction = workspaceId == QStringLiteral("sketch")
                                       ? localSketchFunction_
                                       : FunctionSummary{};
      return;
    }
    snapshot_.selectedFunction = workspaceId == QStringLiteral("sketch")
                                     ? mountingProfileFunction_
                                     : basePlateFunction_;
  }

  void refreshWorkspace() {
    snapshot_.commands = commandsFor(snapshot_.activeWorkspaceId);
    snapshot_.activeCommandId.clear();
    snapshot_.fields.clear();
    snapshot_.commandDraft = {};
    clearSketchInteraction();
    snapshot_.inspectorStatus =
        localMode_ ? (localBackendState_ == LocalBackendState::Ready
                          ? QStringLiteral("Local engineering ready")
                      : localBackendState_ == LocalBackendState::Failed
                          ? QStringLiteral("Local engineering failed")
                          : QStringLiteral("Starting local engineering"))
                   : QStringLiteral("Engineering backend disconnected");
    restoreWorkspaceViewport();
    ++snapshot_.generation;
  }

  FrontendSnapshot snapshot_;
  mutable FrontendSnapshotPtr published_;
  std::vector<SketchInputRequest> sketchInputs_;
  FunctionSummary basePlateFunction_;
  FunctionSummary mountingProfileFunction_;
  FunctionSummary localSketchFunction_;
  enum class LocalBackendState { Starting, Ready, Failed };
  bool localMode_ = false;
  LocalBackendState localBackendState_ = LocalBackendState::Starting;
  bool historyCanUndo_ = false;
  bool historyCanRedo_ = false;
  QString localEditEntity_;
  std::unique_ptr<LocalSketchSession> localSketchSession_;
  ChangeHandler changeHandler_;
};

} // namespace

std::unique_ptr<FrontendPort> makeDevelopmentFrontendPort(
    std::vector<UiOption> themeOptions, const QString &themeId,
    const QString &defaultLengthUnitId, const QString &interfaceDensityId,
    const QString &navigationProfileId, const QString &zoomDirectionId) {
  return std::make_unique<DevelopmentFrontendPort>(
      std::move(themeOptions), themeId, defaultLengthUnitId, interfaceDensityId,
      navigationProfileId, zoomDirectionId);
}

std::unique_ptr<FrontendPort> makeLocalFrontendPort(
    std::unique_ptr<LocalSketchSession> sketchSession,
    std::vector<UiOption> themeOptions, const QString &themeId,
    const QString &defaultLengthUnitId, const QString &interfaceDensityId,
    const QString &navigationProfileId, const QString &zoomDirectionId) {
  return std::make_unique<DevelopmentFrontendPort>(
      std::move(themeOptions), themeId, defaultLengthUnitId, interfaceDensityId,
      navigationProfileId, zoomDirectionId, std::move(sketchSession));
}

} // namespace kearne::ui
