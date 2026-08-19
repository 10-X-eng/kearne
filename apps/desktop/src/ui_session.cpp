#include "ui_session.hpp"

#include <QVariantMap>

#include <array>
#include <algorithm>
#include <span>
#include <utility>

namespace kearne::ui {
namespace {

using Record = QVariantMap;

struct CommandDefinition {
  const char *id;
  const char *label;
  const char *icon;
  const char *group;
  const char *shortcut;
};

QVariantList records(std::initializer_list<Record> values) {
  QVariantList result;
  result.reserve(static_cast<qsizetype>(values.size()));
  for (const Record &value : values)
    result.push_back(value);
  return result;
}

QVariantList commandRecords(const std::span<const CommandDefinition> definitions) {
  QVariantList result;
  result.reserve(static_cast<qsizetype>(definitions.size()));
  for (const CommandDefinition &definition : definitions) {
    result.push_back(Record{{QStringLiteral("id"), QString::fromLatin1(definition.id)},
                            {QStringLiteral("label"), QString::fromLatin1(definition.label)},
                            {QStringLiteral("icon"),
                             QString::fromLatin1(definition.icon)},
                            {QStringLiteral("group"), QString::fromLatin1(definition.group)},
                            {QStringLiteral("shortcut"),
                             QString::fromLatin1(definition.shortcut)},
                            {QStringLiteral("available"), true}});
  }
  return result;
}

constexpr std::array modelCommands{
    CommandDefinition{"model.sketch.create", "New Sketch", "sketch", "Create", "S"},
    CommandDefinition{"model.extrude", "Extrude", "extrude", "Create", "E"},
    CommandDefinition{"model.revolve", "Revolve", "revolve", "Create", "R"},
    CommandDefinition{"model.sweep", "Sweep", "path", "Create", ""},
    CommandDefinition{"model.loft", "Loft", "layers", "Create", ""},
    CommandDefinition{"model.fillet", "Fillet", "round", "Modify", "F"},
    CommandDefinition{"model.chamfer", "Chamfer", "chamfer", "Modify", ""},
    CommandDefinition{"model.shell", "Shell", "shell", "Modify", ""},
    CommandDefinition{"model.hole", "Hole", "hole", "Modify", "H"},
    CommandDefinition{"model.pattern", "Pattern", "grid", "Transform", ""},
    CommandDefinition{"model.mirror", "Mirror", "mirror", "Transform", ""},
    CommandDefinition{"model.plane.create", "Plane", "plane", "Reference", ""},
    CommandDefinition{"model.material.assign", "Material", "material", "Inspect", ""},
    CommandDefinition{"inspect.measure", "Measure", "measure", "Inspect", "M"},
    CommandDefinition{"inspect.section", "Section", "section", "Inspect", ""},
};

constexpr std::array sketchCommands{
    CommandDefinition{"sketch.line", "Line", "line", "Geometry", "L"},
    CommandDefinition{"sketch.polyline", "Polyline", "polyline", "Geometry", ""},
    CommandDefinition{"sketch.rectangle", "Rectangle", "rectangle", "Geometry", "G, R"},
    CommandDefinition{"sketch.circle", "Circle", "circle", "Geometry", "C"},
    CommandDefinition{"sketch.arc", "Arc", "arc", "Geometry", "A"},
    CommandDefinition{"sketch.trim", "Trim", "trim", "Modify", "T"},
    CommandDefinition{"sketch.dimension", "Dimension", "dimension", "Constrain", "D"},
    CommandDefinition{"sketch.coincident", "Coincident", "coincident", "Constrain", ""},
    CommandDefinition{"sketch.horizontal", "Horizontal", "horizontal", "Constrain", ""},
    CommandDefinition{"sketch.vertical", "Vertical", "vertical", "Constrain", ""},
    CommandDefinition{"sketch.equal", "Equal", "equal", "Constrain", ""},
};

constexpr std::array assemblyCommands{
    CommandDefinition{"assembly.insert", "Insert", "add", "Components", "I"},
    CommandDefinition{"assembly.fastener.insert", "Fastener", "fastener", "Components", ""},
    CommandDefinition{"assembly.material.assign", "Material", "material", "Components", ""},
    CommandDefinition{"assembly.joint", "Joint", "joint", "Relationships", "J"},
    CommandDefinition{"assembly.fastened", "Fastened", "fastener", "Relationships", ""},
    CommandDefinition{"assembly.revolute", "Revolute", "revolve", "Relationships", ""},
    CommandDefinition{"assembly.slider", "Slider", "path", "Relationships", ""},
    CommandDefinition{"assembly.drive", "Drive", "play", "Motion", ""},
    CommandDefinition{"assembly.interference", "Interference", "interference", "Inspect", ""},
};

constexpr std::array sheetMetalCommands{
    CommandDefinition{"sheet-metal.base", "Base flange", "sheet-metal", "Create", ""},
    CommandDefinition{"sheet-metal.flange", "Flange", "sheet-metal", "Create", ""},
    CommandDefinition{"sheet-metal.bend", "Bend", "bend", "Create", ""},
    CommandDefinition{"sheet-metal.hem", "Hem", "bend", "Create", ""},
    CommandDefinition{"sheet-metal.relief", "Corner relief", "trim", "Modify", ""},
    CommandDefinition{"sheet-metal.rip", "Rip", "section", "Modify", ""},
    CommandDefinition{"sheet-metal.unfold", "Unfold", "unfold", "Review", ""},
    CommandDefinition{"sheet-metal.refold", "Refold", "mirror", "Review", ""},
};

constexpr std::array simulationCommands{
    CommandDefinition{"simulation.study", "Study", "simulate", "Setup", ""},
    CommandDefinition{"simulation.material", "Material", "material", "Setup", ""},
    CommandDefinition{"simulation.constraint", "Constraint", "lock", "Loads", ""},
    CommandDefinition{"simulation.load", "Load", "target", "Loads", ""},
    CommandDefinition{"simulation.mesh", "Mesh", "mesh", "Solve", ""},
    CommandDefinition{"simulation.solve", "Solve", "solve", "Solve", ""},
    CommandDefinition{"simulation.results", "Results", "chart", "Review", ""},
};

constexpr std::array drawingCommands{
    CommandDefinition{"drawing.sheet", "Sheet", "sheet", "Create", ""},
    CommandDefinition{"drawing.base_view", "Base view", "view", "Views", ""},
    CommandDefinition{"drawing.projected_view", "Projected", "layers", "Views", ""},
    CommandDefinition{"drawing.section_view", "Section", "section", "Views", ""},
    CommandDefinition{"drawing.dimension", "Dimension", "dimension", "Annotate", ""},
    CommandDefinition{"drawing.gdt", "GD&T", "target", "Annotate", ""},
};

constexpr std::array bomCommands{
    CommandDefinition{"bom.refresh", "Refresh", "refresh", "Table", ""},
    CommandDefinition{"bom.columns", "Columns", "columns", "Table", ""},
    CommandDefinition{"bom.balloon", "Balloon", "circle", "Annotate", ""},
    CommandDefinition{"bom.export", "Export", "export", "Output", ""},
};

constexpr std::array camCommands{
    CommandDefinition{"cam.setup", "Setup", "cam", "Setup", ""},
    CommandDefinition{"cam.stock", "Stock", "stock", "Setup", ""},
    CommandDefinition{"cam.face", "Face", "face", "2D", ""},
    CommandDefinition{"cam.contour", "Contour", "contour", "2D", ""},
    CommandDefinition{"cam.pocket", "Pocket", "pocket", "2D", ""},
    CommandDefinition{"cam.drill", "Drill", "drill", "Hole", ""},
    CommandDefinition{"cam.adaptive", "Adaptive", "toolpath", "3D", ""},
    CommandDefinition{"cam.simulate", "Simulate", "play", "Verify", ""},
    CommandDefinition{"cam.post", "Post", "export", "Output", ""},
};

constexpr std::array versionCommands{
    CommandDefinition{"version.checkpoint", "Checkpoint", "checkpoint", "History", ""},
    CommandDefinition{"version.branch", "Branch", "branch", "History", ""},
    CommandDefinition{"version.compare", "Compare", "compare", "Review", ""},
    CommandDefinition{"version.merge", "Merge", "merge", "Review", ""},
};

constexpr std::array agentCommands{
    CommandDefinition{"agent.ask", "Ask Kearne", "agent", "Agent", "Ctrl+J"},
    CommandDefinition{"agent.inspect", "Inspect context", "inspect", "Agent", ""},
    CommandDefinition{"agent.plan", "Plan change", "plan", "Agent", ""},
    CommandDefinition{"agent.review", "Review change", "review", "Agent", ""},
};

QVariantList allCommandRecords() {
  QVariantList result;
  const auto append = [&result](QVariantList commands) {
    result.append(commands);
  };
  append(commandRecords(modelCommands));
  append(commandRecords(sketchCommands));
  append(commandRecords(assemblyCommands));
  append(commandRecords(sheetMetalCommands));
  append(commandRecords(simulationCommands));
  append(commandRecords(camCommands));
  append(commandRecords(drawingCommands));
  append(commandRecords(bomCommands));
  append(commandRecords(versionCommands));
  append(commandRecords(agentCommands));
  return result;
}

QVariantList commandsFor(const QString &workspaceId) {
  if (workspaceId == QStringLiteral("sketch"))
    return commandRecords(sketchCommands);
  if (workspaceId == QStringLiteral("assemble"))
    return commandRecords(assemblyCommands);
  if (workspaceId == QStringLiteral("sheet-metal"))
    return commandRecords(sheetMetalCommands);
  if (workspaceId == QStringLiteral("simulate"))
    return commandRecords(simulationCommands);
  if (workspaceId == QStringLiteral("drawing"))
    return commandRecords(drawingCommands);
  if (workspaceId == QStringLiteral("bom"))
    return commandRecords(bomCommands);
  if (workspaceId == QStringLiteral("cam"))
    return commandRecords(camCommands);
  return commandRecords(modelCommands);
}

QString commandLabel(const QVariantList &commands, const QString &commandId) {
  for (const QVariant &value : commands) {
    const QVariantMap command = value.toMap();
    if (command.value(QStringLiteral("id")).toString() == commandId)
      return command.value(QStringLiteral("label")).toString();
  }
  return {};
}

bool containsRecordId(const QVariantList &records, const QString &id) {
  return std::any_of(records.cbegin(), records.cend(), [&id](const QVariant &value) {
    return value.toMap().value(QStringLiteral("id")).toString() == id;
  });
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

class DevelopmentFrontendPort final : public FrontendPort {
public:
  DevelopmentFrontendPort() {
    snapshot_.generation = 1;
    snapshot_.projectName = QStringLiteral("Motor Bracket");
    snapshot_.branchLabel = QStringLiteral("main");
    snapshot_.revisionLabel = QStringLiteral("UI contract mode");
    snapshot_.activeWorkspaceId = QStringLiteral("model");
    snapshot_.viewportState = QStringLiteral("unavailable");
    snapshot_.inspectorTitle = QStringLiteral("Selection");
    snapshot_.inspectorStatus = QStringLiteral("Engineering backend disconnected");
    snapshot_.viewportHeadline = QStringLiteral("Viewport ready");
    snapshot_.viewportDetail = QStringLiteral("Render projection is not connected");
    snapshot_.modelHealth = QStringLiteral("Backend disconnected");
    snapshot_.selectionSummary = QStringLiteral("Nothing selected");
    snapshot_.agentStatus = QStringLiteral("Codex harness disconnected");
    snapshot_.defaultLengthUnitId = QStringLiteral("mm");
    snapshot_.projectLengthUnitId = QStringLiteral("mm");
    snapshot_.gridPlaneLabel = QStringLiteral("XY");
    snapshot_.gridSpacingLabel = gridSpacingFor(snapshot_.projectLengthUnitId);
    snapshot_.lengthUnits = records({
        {{QStringLiteral("id"), QStringLiteral("mm")},
         {QStringLiteral("label"), QStringLiteral("Millimeters")},
         {QStringLiteral("symbol"), QStringLiteral("mm")}},
        {{QStringLiteral("id"), QStringLiteral("cm")},
         {QStringLiteral("label"), QStringLiteral("Centimeters")},
         {QStringLiteral("symbol"), QStringLiteral("cm")}},
        {{QStringLiteral("id"), QStringLiteral("m")},
         {QStringLiteral("label"), QStringLiteral("Meters")},
         {QStringLiteral("symbol"), QStringLiteral("m")}},
        {{QStringLiteral("id"), QStringLiteral("in")},
         {QStringLiteral("label"), QStringLiteral("Inches")},
         {QStringLiteral("symbol"), QStringLiteral("in")}},
    });
    snapshot_.modelSource = QStringLiteral(
        "from build123d import Box\n\n"
        "def base_plate(\n"
        "    width: float = 100.0,\n"
        "    depth: float = 60.0,\n"
        "    thickness: float = 8.0,\n"
        "):\n"
        "    return Box(width, depth, thickness)\n");
    snapshot_.commandCatalog = allCommandRecords();
    snapshot_.workspaces = records({
        {{QStringLiteral("id"), QStringLiteral("model")},
         {QStringLiteral("label"), QStringLiteral("Model")},
         {QStringLiteral("icon"), QStringLiteral("model")}},
        {{QStringLiteral("id"), QStringLiteral("sketch")},
         {QStringLiteral("label"), QStringLiteral("Sketch")},
         {QStringLiteral("icon"), QStringLiteral("sketch")}},
        {{QStringLiteral("id"), QStringLiteral("assemble")},
         {QStringLiteral("label"), QStringLiteral("Assemble")},
         {QStringLiteral("icon"), QStringLiteral("assemble")}},
        {{QStringLiteral("id"), QStringLiteral("sheet-metal")},
         {QStringLiteral("label"), QStringLiteral("Sheet Metal")},
         {QStringLiteral("icon"), QStringLiteral("sheet-metal")}},
        {{QStringLiteral("id"), QStringLiteral("simulate")},
         {QStringLiteral("label"), QStringLiteral("Simulate")},
         {QStringLiteral("icon"), QStringLiteral("simulate")}},
        {{QStringLiteral("id"), QStringLiteral("cam")},
         {QStringLiteral("label"), QStringLiteral("CAM")},
         {QStringLiteral("icon"), QStringLiteral("cam")}},
        {{QStringLiteral("id"), QStringLiteral("drawing")},
         {QStringLiteral("label"), QStringLiteral("Drawing")},
         {QStringLiteral("icon"), QStringLiteral("drawing")}},
        {{QStringLiteral("id"), QStringLiteral("bom")},
         {QStringLiteral("label"), QStringLiteral("BOM")},
         {QStringLiteral("icon"), QStringLiteral("bom")}},
    });
    snapshot_.structure = records({
        {{QStringLiteral("id"), QStringLiteral("project.root")},
         {QStringLiteral("label"), QStringLiteral("Motor Bracket")},
         {QStringLiteral("depth"), 0},
         {QStringLiteral("kind"), QStringLiteral("project")}},
        {{QStringLiteral("id"), QStringLiteral("component.base_plate")},
         {QStringLiteral("label"), QStringLiteral("Base Plate")},
         {QStringLiteral("depth"), 1},
         {QStringLiteral("kind"), QStringLiteral("component")}},
        {{QStringLiteral("id"), QStringLiteral("reference.origin")},
         {QStringLiteral("label"), QStringLiteral("Origin")},
         {QStringLiteral("depth"), 2},
         {QStringLiteral("kind"), QStringLiteral("group")}},
        {{QStringLiteral("id"), QStringLiteral("reference.plane.xy")},
         {QStringLiteral("label"), QStringLiteral("XY Plane")},
         {QStringLiteral("depth"), 3},
         {QStringLiteral("kind"), QStringLiteral("plane")}},
        {{QStringLiteral("id"), QStringLiteral("reference.plane.xz")},
         {QStringLiteral("label"), QStringLiteral("XZ Plane")},
         {QStringLiteral("depth"), 3},
         {QStringLiteral("kind"), QStringLiteral("plane")}},
        {{QStringLiteral("id"), QStringLiteral("reference.plane.yz")},
         {QStringLiteral("label"), QStringLiteral("YZ Plane")},
         {QStringLiteral("depth"), 3},
         {QStringLiteral("kind"), QStringLiteral("plane")}},
        {{QStringLiteral("id"), QStringLiteral("function.sketch_003")},
         {QStringLiteral("label"), QStringLiteral("sketch_003()")},
         {QStringLiteral("depth"), 2},
         {QStringLiteral("kind"), QStringLiteral("model-function")}},
        {{QStringLiteral("id"), QStringLiteral("function.extrude_004")},
         {QStringLiteral("label"), QStringLiteral("extrude_004()")},
         {QStringLiteral("depth"), 2},
         {QStringLiteral("kind"), QStringLiteral("model-function")}},
        {{QStringLiteral("id"), QStringLiteral("component.motor")},
         {QStringLiteral("label"), QStringLiteral("Motor NEMA 23")},
         {QStringLiteral("depth"), 1},
         {QStringLiteral("kind"), QStringLiteral("component")}},
        {{QStringLiteral("id"), QStringLiteral("group.fasteners")},
         {QStringLiteral("label"), QStringLiteral("Fasteners")},
         {QStringLiteral("depth"), 1},
         {QStringLiteral("kind"), QStringLiteral("group")}},
    });
    snapshot_.revisions = records({
        {{QStringLiteral("id"), QStringLiteral("revision.ui.3")},
         {QStringLiteral("label"), QStringLiteral("Inspector state")},
         {QStringLiteral("detail"), QStringLiteral("frontend contract")}},
        {{QStringLiteral("id"), QStringLiteral("revision.ui.2")},
         {QStringLiteral("label"), QStringLiteral("Workspace layout")},
         {QStringLiteral("detail"), QStringLiteral("frontend contract")}},
        {{QStringLiteral("id"), QStringLiteral("revision.ui.1")},
         {QStringLiteral("label"), QStringLiteral("Application shell")},
         {QStringLiteral("detail"), QStringLiteral("frontend contract")}},
    });
    snapshot_.historyCommands = commandRecords(versionCommands);
    snapshot_.parameters = records({
        {{QStringLiteral("name"), QStringLiteral("plateWidth")},
         {QStringLiteral("expression"), QStringLiteral("100 mm")},
         {QStringLiteral("value"), QStringLiteral("unresolved")}},
        {{QStringLiteral("name"), QStringLiteral("height")},
         {QStringLiteral("expression"), QStringLiteral("plateWidth / 2")},
         {QStringLiteral("value"), QStringLiteral("unresolved")}},
        {{QStringLiteral("name"), QStringLiteral("wall")},
         {QStringLiteral("expression"), QStringLiteral("2.5 mm")},
         {QStringLiteral("value"), QStringLiteral("unresolved")}},
    });
    snapshot_.jobs = records({
        {{QStringLiteral("id"), QStringLiteral("job.backend")},
         {QStringLiteral("label"), QStringLiteral("Engineering services")},
         {QStringLiteral("state"), QStringLiteral("Unavailable")},
         {QStringLiteral("progress"), -1}},
    });
    snapshot_.diagnostics = records({
        {{QStringLiteral("id"), QStringLiteral("diagnostic.backend")},
         {QStringLiteral("severity"), QStringLiteral("information")},
         {QStringLiteral("summary"),
          QStringLiteral("Frontend contract mode does not evaluate geometry.")}},
    });
    snapshot_.proposals = records({
        {{QStringLiteral("id"), QStringLiteral("proposal.connect")},
         {QStringLiteral("summary"),
          QStringLiteral("Connect the supervised Codex harness to enable proposals.")},
         {QStringLiteral("state"), QStringLiteral("Unavailable")}},
    });
    snapshot_.recentProjects = records({
        {{QStringLiteral("id"), QStringLiteral("project.motor_bracket")},
         {QStringLiteral("name"), QStringLiteral("Motor Bracket")},
         {QStringLiteral("detail"), QStringLiteral("UI contract workspace")},
         {QStringLiteral("modified"), QStringLiteral("Current session")},
         {QStringLiteral("icon"), QStringLiteral("model")}},
    });
    snapshot_.projectTemplates = records({
        {{QStringLiteral("id"), QStringLiteral("template.part")},
         {QStringLiteral("name"), QStringLiteral("Model")},
         {QStringLiteral("detail"), QStringLiteral("Parametric component")},
         {QStringLiteral("icon"), QStringLiteral("model")}},
        {{QStringLiteral("id"), QStringLiteral("template.assembly")},
         {QStringLiteral("name"), QStringLiteral("Assembly")},
         {QStringLiteral("detail"), QStringLiteral("Components and joints")},
         {QStringLiteral("icon"), QStringLiteral("assemble")}},
        {{QStringLiteral("id"), QStringLiteral("template.drawing")},
         {QStringLiteral("name"), QStringLiteral("Drawing")},
         {QStringLiteral("detail"), QStringLiteral("Associative documentation")},
         {QStringLiteral("icon"), QStringLiteral("drawing")}},
        {{QStringLiteral("id"), QStringLiteral("template.sheet-metal")},
         {QStringLiteral("name"), QStringLiteral("Sheet Metal")},
         {QStringLiteral("detail"), QStringLiteral("Fabricated sheet component")},
         {QStringLiteral("icon"), QStringLiteral("sheet-metal")}},
        {{QStringLiteral("id"), QStringLiteral("template.cam")},
         {QStringLiteral("name"), QStringLiteral("CAM")},
         {QStringLiteral("detail"), QStringLiteral("Manufacturing setup")},
         {QStringLiteral("icon"), QStringLiteral("cam")}},
    });
    snapshot_.recoveryItems = records({
        {{QStringLiteral("id"), QStringLiteral("recovery.none")},
         {QStringLiteral("name"), QStringLiteral("No recovery required")},
         {QStringLiteral("detail"),
          QStringLiteral("The development workspace has no interrupted writes.")},
         {QStringLiteral("state"), QStringLiteral("current")},
         {QStringLiteral("available"), false}},
    });
    snapshot_.operations = records({
        {{QStringLiteral("id"), QStringLiteral("operation.services")},
         {QStringLiteral("name"), QStringLiteral("Engineering services")},
         {QStringLiteral("kind"), QStringLiteral("service")},
         {QStringLiteral("state"), QStringLiteral("unavailable")},
         {QStringLiteral("detail"),
          QStringLiteral("No backend process has been started.")},
         {QStringLiteral("progress"), -1}},
        {{QStringLiteral("id"), QStringLiteral("operation.frontend")},
         {QStringLiteral("name"), QStringLiteral("Frontend projection")},
         {QStringLiteral("kind"), QStringLiteral("projection")},
         {QStringLiteral("state"), QStringLiteral("current")},
         {QStringLiteral("detail"),
          QStringLiteral("Deterministic development data is active.")},
         {QStringLiteral("progress"), 100}},
    });
    snapshot_.interfaceStates = records({
        {{QStringLiteral("id"), QStringLiteral("empty")},
         {QStringLiteral("label"), QStringLiteral("Empty")},
         {QStringLiteral("icon"), QStringLiteral("empty")}},
        {{QStringLiteral("id"), QStringLiteral("loading")},
         {QStringLiteral("label"), QStringLiteral("Loading")},
         {QStringLiteral("icon"), QStringLiteral("loading")}},
        {{QStringLiteral("id"), QStringLiteral("current")},
         {QStringLiteral("label"), QStringLiteral("Current")},
         {QStringLiteral("icon"), QStringLiteral("check")}},
        {{QStringLiteral("id"), QStringLiteral("preview")},
         {QStringLiteral("label"), QStringLiteral("Preview")},
         {QStringLiteral("icon"), QStringLiteral("preview")}},
        {{QStringLiteral("id"), QStringLiteral("pending")},
         {QStringLiteral("label"), QStringLiteral("Pending")},
         {QStringLiteral("icon"), QStringLiteral("clock")}},
        {{QStringLiteral("id"), QStringLiteral("stale")},
         {QStringLiteral("label"), QStringLiteral("Stale")},
         {QStringLiteral("icon"), QStringLiteral("stale")}},
        {{QStringLiteral("id"), QStringLiteral("failed")},
         {QStringLiteral("label"), QStringLiteral("Failed")},
         {QStringLiteral("icon"), QStringLiteral("error")}},
        {{QStringLiteral("id"), QStringLiteral("unavailable")},
         {QStringLiteral("label"), QStringLiteral("Unavailable")},
         {QStringLiteral("icon"), QStringLiteral("unavailable")}},
        {{QStringLiteral("id"), QStringLiteral("read-only")},
         {QStringLiteral("label"), QStringLiteral("Read-only")},
         {QStringLiteral("icon"), QStringLiteral("lock")}},
        {{QStringLiteral("id"), QStringLiteral("permission-denied")},
         {QStringLiteral("label"), QStringLiteral("Permission denied")},
         {QStringLiteral("icon"), QStringLiteral("shield")}},
    });
    refreshWorkspace();
  }

  [[nodiscard]] FrontendSnapshot snapshot() const override { return snapshot_; }

  void selectWorkspace(const QString &workspaceId) override {
    const QString label = commandLabel(snapshot_.workspaces, workspaceId);
    if (label.isEmpty())
      return;
    if (workspaceId == snapshot_.activeWorkspaceId)
      return;
    snapshot_.activeWorkspaceId = workspaceId;
    snapshot_.activeCommandId.clear();
    snapshot_.selectionSummary = QStringLiteral("Nothing selected");
    snapshot_.inspectorTitle = QStringLiteral("%1 workspace").arg(label);
    refreshWorkspace();
  }

  void selectEntity(const QString &entityId) override {
    snapshot_.selectionSummary = entityId;
    snapshot_.inspectorTitle = QStringLiteral("Selection");
    snapshot_.inspectorStatus = QStringLiteral("Read-only development projection");
    snapshot_.fields = records({
        {{QStringLiteral("id"), QStringLiteral("selection.identity")},
         {QStringLiteral("label"), QStringLiteral("Identity")},
         {QStringLiteral("kind"), QStringLiteral("text")},
         {QStringLiteral("value"), entityId},
         {QStringLiteral("readOnly"), true}},
        {{QStringLiteral("id"), QStringLiteral("selection.revision")},
         {QStringLiteral("label"), QStringLiteral("Revision")},
         {QStringLiteral("kind"), QStringLiteral("text")},
         {QStringLiteral("value"), QStringLiteral("Not connected")},
         {QStringLiteral("readOnly"), true}},
    });
    ++snapshot_.generation;
  }

  void requestCommand(const QString &commandId) override {
    const QString statePrefix = QStringLiteral("development.state.");
    if (commandId.startsWith(statePrefix)) {
      const QString state = commandId.sliced(statePrefix.size());
      snapshot_.viewportState = state;
      snapshot_.activeCommandId.clear();
      snapshot_.viewportHeadline = state.left(1).toUpper() + state.mid(1);
      snapshot_.viewportDetail = QStringLiteral("Deterministic %1 state projection")
                                     .arg(state);
      snapshot_.modelHealth = snapshot_.viewportHeadline;
      ++snapshot_.generation;
      return;
    }
    if (commandId == QStringLiteral("model.plane.create")) {
      snapshot_.activeCommandId = commandId;
      snapshot_.inspectorTitle = QStringLiteral("Plane");
      snapshot_.inspectorStatus = QStringLiteral("Define a construction plane");
      snapshot_.fields = records({
          {{QStringLiteral("id"), QStringLiteral("model.plane.create.method")},
           {QStringLiteral("label"), QStringLiteral("Method")},
           {QStringLiteral("kind"), QStringLiteral("choice")},
           {QStringLiteral("value"), QStringLiteral("Offset")},
           {QStringLiteral("options"),
            QStringList{QStringLiteral("Offset"), QStringLiteral("Midplane"),
                        QStringLiteral("At angle"),
                        QStringLiteral("Through three points"),
                        QStringLiteral("Tangent to surface")}},
           {QStringLiteral("readOnly"), false}},
          {{QStringLiteral("id"), QStringLiteral("model.plane.create.reference")},
           {QStringLiteral("label"), QStringLiteral("Reference")},
           {QStringLiteral("kind"), QStringLiteral("reference")},
           {QStringLiteral("value"), QStringLiteral("Choose a plane or planar face")},
           {QStringLiteral("readOnly"), false}},
          {{QStringLiteral("id"), QStringLiteral("model.plane.create.offset")},
           {QStringLiteral("label"), QStringLiteral("Offset")},
           {QStringLiteral("kind"), QStringLiteral("expression")},
           {QStringLiteral("value"), snapshot_.gridSpacingLabel},
           {QStringLiteral("effectiveValue"), QStringLiteral("Unresolved")},
           {QStringLiteral("readOnly"), false}},
      });
      ++snapshot_.generation;
      return;
    }
    if (commandId == QStringLiteral("model.sketch.create")) {
      snapshot_.activeCommandId = commandId;
      snapshot_.inspectorTitle = QStringLiteral("New Sketch");
      snapshot_.inspectorStatus = QStringLiteral("Choose where the sketch is attached");
      snapshot_.fields = records({
          {{QStringLiteral("id"), QStringLiteral("model.sketch.create.attachment")},
           {QStringLiteral("label"), QStringLiteral("Plane or planar face")},
           {QStringLiteral("kind"), QStringLiteral("reference")},
           {QStringLiteral("value"), QStringLiteral("Choose a reference")},
           {QStringLiteral("readOnly"), false}},
          {{QStringLiteral("id"), QStringLiteral("model.sketch.create.orientation")},
           {QStringLiteral("label"), QStringLiteral("View orientation")},
           {QStringLiteral("kind"), QStringLiteral("choice")},
           {QStringLiteral("value"), QStringLiteral("Normal to sketch")},
           {QStringLiteral("options"),
            QStringList{QStringLiteral("Normal to sketch"),
                        QStringLiteral("Keep current view")}},
           {QStringLiteral("readOnly"), false}},
      });
      ++snapshot_.generation;
      return;
    }
    snapshot_.activeCommandId = commandId;
    const QString label = commandLabel(snapshot_.commandCatalog, commandId);
    snapshot_.inspectorTitle = label.isEmpty() ? commandId : label;
    snapshot_.inspectorStatus = QStringLiteral("Ready for backend contract");
    snapshot_.fields = records({
        {{QStringLiteral("id"), commandId + QStringLiteral(".target")},
         {QStringLiteral("label"), QStringLiteral("Target")},
         {QStringLiteral("kind"), QStringLiteral("reference")},
         {QStringLiteral("value"), QStringLiteral("Choose a model reference")},
         {QStringLiteral("readOnly"), false}},
        {{QStringLiteral("id"), commandId + QStringLiteral(".value")},
         {QStringLiteral("label"), QStringLiteral("Value")},
         {QStringLiteral("kind"), QStringLiteral("expression")},
         {QStringLiteral("value"), snapshot_.gridSpacingLabel},
         {QStringLiteral("effectiveValue"), QStringLiteral("Unresolved")},
         {QStringLiteral("readOnly"), false}},
        {{QStringLiteral("id"), commandId + QStringLiteral(".mode")},
         {QStringLiteral("label"), QStringLiteral("Mode")},
         {QStringLiteral("kind"), QStringLiteral("choice")},
         {QStringLiteral("value"), QStringLiteral("New")},
         {QStringLiteral("options"), QStringList{QStringLiteral("New"),
                                                 QStringLiteral("Add"),
                                                 QStringLiteral("Remove")}},
         {QStringLiteral("readOnly"), false}},
    });
    ++snapshot_.generation;
  }

  void setDefaultLengthUnit(const QString &unitId) override {
    if (!containsRecordId(snapshot_.lengthUnits, unitId) ||
        snapshot_.defaultLengthUnitId == unitId)
      return;
    snapshot_.defaultLengthUnitId = unitId;
    ++snapshot_.generation;
  }

  void setProjectLengthUnit(const QString &unitId) override {
    if (!containsRecordId(snapshot_.lengthUnits, unitId) ||
        snapshot_.projectLengthUnitId == unitId)
      return;
    snapshot_.projectLengthUnitId = unitId;
    snapshot_.gridSpacingLabel = gridSpacingFor(unitId);
    ++snapshot_.generation;
  }

  void setGridVisible(bool visible) override {
    if (snapshot_.gridVisible == visible)
      return;
    snapshot_.gridVisible = visible;
    ++snapshot_.generation;
  }

  void setGridSnapEnabled(bool enabled) override {
    if (snapshot_.gridSnapEnabled == enabled)
      return;
    snapshot_.gridSnapEnabled = enabled;
    ++snapshot_.generation;
  }

private:
  void refreshWorkspace() {
    snapshot_.commands = commandsFor(snapshot_.activeWorkspaceId);
    snapshot_.fields.clear();
    snapshot_.inspectorStatus = QStringLiteral("Engineering backend disconnected");
    ++snapshot_.generation;
  }

  FrontendSnapshot snapshot_;
};

} // namespace

std::unique_ptr<FrontendPort> makeDevelopmentFrontendPort() {
  return std::make_unique<DevelopmentFrontendPort>();
}

UiSession::UiSession(std::unique_ptr<FrontendPort> port, QObject *parent)
    : QObject(parent), port_(std::move(port)) {
  refresh();
}

qulonglong UiSession::generation() const { return generation_; }
QString UiSession::projectName() const { return snapshot_.projectName; }
QString UiSession::branchLabel() const { return snapshot_.branchLabel; }
QString UiSession::revisionLabel() const { return snapshot_.revisionLabel; }
QString UiSession::activeWorkspaceId() const { return snapshot_.activeWorkspaceId; }
QString UiSession::activeCommandId() const { return snapshot_.activeCommandId; }
QString UiSession::activeSurfaceId() const { return activeSurfaceId_; }
QString UiSession::settingsCategoryId() const { return settingsCategoryId_; }
int UiSession::inspectorPage() const { return inspectorPage_; }
QString UiSession::viewportState() const { return snapshot_.viewportState; }
QString UiSession::inspectorTitle() const { return snapshot_.inspectorTitle; }
QString UiSession::inspectorStatus() const { return snapshot_.inspectorStatus; }
QString UiSession::viewportHeadline() const { return snapshot_.viewportHeadline; }
QString UiSession::viewportDetail() const { return snapshot_.viewportDetail; }
QString UiSession::modelHealth() const { return snapshot_.modelHealth; }
QString UiSession::selectionSummary() const { return snapshot_.selectionSummary; }
QString UiSession::agentStatus() const { return snapshot_.agentStatus; }
QString UiSession::modelSource() const { return snapshot_.modelSource; }
QString UiSession::defaultLengthUnitId() const { return snapshot_.defaultLengthUnitId; }
QString UiSession::projectLengthUnitId() const { return snapshot_.projectLengthUnitId; }
QString UiSession::gridPlaneLabel() const { return snapshot_.gridPlaneLabel; }
QString UiSession::gridSpacingLabel() const { return snapshot_.gridSpacingLabel; }
bool UiSession::gridVisible() const { return snapshot_.gridVisible; }
bool UiSession::gridSnapEnabled() const { return snapshot_.gridSnapEnabled; }
bool UiSession::backendConnected() const { return snapshot_.backendConnected; }
QVariantList UiSession::lengthUnits() const { return snapshot_.lengthUnits; }
QVariantList UiSession::workspaces() const { return snapshot_.workspaces; }
QVariantList UiSession::commands() const { return snapshot_.commands; }
QVariantList UiSession::commandCatalog() const { return snapshot_.commandCatalog; }
QVariantList UiSession::structure() const { return snapshot_.structure; }
QVariantList UiSession::revisions() const { return snapshot_.revisions; }
QVariantList UiSession::historyCommands() const { return snapshot_.historyCommands; }
QVariantList UiSession::fields() const { return snapshot_.fields; }
QVariantList UiSession::parameters() const { return snapshot_.parameters; }
QVariantList UiSession::jobs() const { return snapshot_.jobs; }
QVariantList UiSession::diagnostics() const { return snapshot_.diagnostics; }
QVariantList UiSession::proposals() const { return snapshot_.proposals; }
QVariantList UiSession::recentProjects() const { return snapshot_.recentProjects; }
QVariantList UiSession::projectTemplates() const { return snapshot_.projectTemplates; }
QVariantList UiSession::recoveryItems() const { return snapshot_.recoveryItems; }
QVariantList UiSession::operations() const { return snapshot_.operations; }
QVariantList UiSession::interfaceStates() const { return snapshot_.interfaceStates; }

void UiSession::navigateTo(const QString &surfaceId) {
  static const QStringList surfaces{QStringLiteral("projects"),
                                    QStringLiteral("editor"),
                                    QStringLiteral("settings"),
                                    QStringLiteral("recovery"),
                                    QStringLiteral("operations")};
  if (!surfaces.contains(surfaceId) || activeSurfaceId_ == surfaceId)
    return;
  activeSurfaceId_ = surfaceId;
  ++generation_;
  emit projectionChanged();
}

void UiSession::selectSettingsCategory(const QString &categoryId) {
  static const QStringList categories{QStringLiteral("appearance"),
                                      QStringLiteral("units"),
                                      QStringLiteral("input"),
                                      QStringLiteral("files"),
                                      QStringLiteral("compute"),
                                      QStringLiteral("agent")};
  if (!categories.contains(categoryId) || settingsCategoryId_ == categoryId)
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

void UiSession::selectEntity(const QString &entityId) {
  port_->selectEntity(entityId);
  refresh();
}

void UiSession::requestCommand(const QString &commandId) {
  port_->requestCommand(commandId);
  refresh();
  emit commandRequested(commandId, generation());
}

void UiSession::setDefaultLengthUnit(const QString &unitId) {
  port_->setDefaultLengthUnit(unitId);
  refresh();
}

void UiSession::setProjectLengthUnit(const QString &unitId) {
  port_->setProjectLengthUnit(unitId);
  refresh();
}

void UiSession::setGridVisible(bool visible) {
  port_->setGridVisible(visible);
  refresh();
}

void UiSession::setGridSnapEnabled(bool enabled) {
  port_->setGridSnapEnabled(enabled);
  refresh();
}

void UiSession::refresh() {
  snapshot_ = port_->snapshot();
  generation_ = std::max(generation_ + 1,
                         static_cast<qulonglong>(snapshot_.generation));
  emit projectionChanged();
}

} // namespace kearne::ui
