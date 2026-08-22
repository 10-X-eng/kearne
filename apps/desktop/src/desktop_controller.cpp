#include "desktop_controller.hpp"
#include "command_catalog.hpp"
#include "display_units.hpp"
#include "local_sketch_session.hpp"
#include "sketch_tool_gesture.hpp"

#include <kearne/sketch/nurbs.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <iterator>
#include <limits>
#include <map>
#include <numbers>
#include <optional>
#include <span>
#include <string_view>
#include <unordered_set>
#include <utility>

namespace kearne::ui {
namespace {

enum class LocalSketchTransformCommand : std::uint8_t {
  Translate,
  Rotate,
  Scale,
  Symmetry,
};

std::optional<LocalSketchTransformCommand>
localSketchTransformCommand(QStringView commandId) {
  if (commandId == QStringLiteral("sketch.translate"))
    return LocalSketchTransformCommand::Translate;
  if (commandId == QStringLiteral("sketch.rotate"))
    return LocalSketchTransformCommand::Rotate;
  if (commandId == QStringLiteral("sketch.scale"))
    return LocalSketchTransformCommand::Scale;
  if (commandId == QStringLiteral("sketch.symmetry"))
    return LocalSketchTransformCommand::Symmetry;
  return std::nullopt;
}

enum class LocalSketchCurveModifyCommand : std::uint8_t {
  Fillet,
  Chamfer,
  Offset,
  Extend,
  Trim,
  Split,
  Join,
  ConvertToNurbs,
};

std::optional<LocalSketchCurveModifyCommand>
localSketchCurveModifyCommand(QStringView commandId) {
  if (commandId == QStringLiteral("sketch.fillet"))
    return LocalSketchCurveModifyCommand::Fillet;
  if (commandId == QStringLiteral("sketch.chamfer"))
    return LocalSketchCurveModifyCommand::Chamfer;
  if (commandId == QStringLiteral("sketch.offset"))
    return LocalSketchCurveModifyCommand::Offset;
  if (commandId == QStringLiteral("sketch.extend"))
    return LocalSketchCurveModifyCommand::Extend;
  if (commandId == QStringLiteral("sketch.trim"))
    return LocalSketchCurveModifyCommand::Trim;
  if (commandId == QStringLiteral("sketch.split"))
    return LocalSketchCurveModifyCommand::Split;
  if (commandId == QStringLiteral("sketch.join"))
    return LocalSketchCurveModifyCommand::Join;
  if (commandId == QStringLiteral("sketch.bspline.convert-to-nurbs"))
    return LocalSketchCurveModifyCommand::ConvertToNurbs;
  return std::nullopt;
}

std::optional<LocalBSplineEditKind>
localBSplineEditKind(QStringView commandId) {
  if (commandId == QStringLiteral("sketch.bspline.increase-degree"))
    return LocalBSplineEditKind::IncreaseDegree;
  if (commandId == QStringLiteral("sketch.bspline.decrease-degree"))
    return LocalBSplineEditKind::DecreaseDegree;
  if (commandId == QStringLiteral("sketch.bspline.increase-knot-multiplicity"))
    return LocalBSplineEditKind::IncreaseKnotMultiplicity;
  if (commandId == QStringLiteral("sketch.bspline.decrease-knot-multiplicity"))
    return LocalBSplineEditKind::DecreaseKnotMultiplicity;
  if (commandId == QStringLiteral("sketch.bspline.insert-knot"))
    return LocalBSplineEditKind::InsertKnot;
  if (commandId == QStringLiteral("sketch.bspline.pole-weight"))
    return LocalBSplineEditKind::SetPoleWeight;
  return std::nullopt;
}

std::optional<LocalSketchToolKind>
localSketchToolKind(QStringView commandId, QStringView methodId = {}) {
  const LocalSketchToolDefinition *definition =
      localSketchToolDefinition(commandId, methodId);
  return definition ? std::optional{definition->kind} : std::nullopt;
}

QString localSketchToolLabel(LocalSketchToolKind kind) {
  const LocalSketchToolDefinition *definition = localSketchToolDefinition(kind);
  return definition ? QString::fromLatin1(
                          definition->label.data(),
                          static_cast<qsizetype>(definition->label.size()))
                    : QStringLiteral("Sketch geometry");
}

QString sketchMemberLabel(std::string_view role) {
  const std::size_t part = role.rfind("_part_");
  if (part != std::string_view::npos && part != 0U && part + 6U < role.size())
    return QStringLiteral("%1, part %2")
        .arg(sketchMemberLabel(role.substr(0U, part)),
             QString::fromLatin1(
                 role.substr(part + 6U).data(),
                 static_cast<qsizetype>(role.size() - part - 6U)));
  if (role == "bottom")
    return QStringLiteral("Bottom edge");
  if (role == "right")
    return QStringLiteral("Right edge");
  if (role == "top")
    return QStringLiteral("Top edge");
  if (role == "left")
    return QStringLiteral("Left edge");
  if (role == "point")
    return QStringLiteral("Point");
  if (role == "curve")
    return QStringLiteral("Curve");
  if (role == "start_cap")
    return QStringLiteral("Start cap");
  if (role == "end_cap")
    return QStringLiteral("End cap");
  if (role == "top_side")
    return QStringLiteral("Top side");
  if (role == "bottom_side")
    return QStringLiteral("Bottom side");
  if (role == "outer")
    return QStringLiteral("Outer arc");
  if (role == "inner")
    return QStringLiteral("Inner arc");
  if (role.starts_with("segment_"))
    return QStringLiteral("Segment %1")
        .arg(QString::fromLatin1(role.substr(8U).data(),
                                 static_cast<qsizetype>(role.size() - 8U)));
  if (role.starts_with("side_"))
    return QStringLiteral("Side %1").arg(QString::fromLatin1(
        role.substr(5U).data(), static_cast<qsizetype>(role.size() - 5U)));
  return QStringLiteral("Edge");
}

struct SketchObjectPresentation {
  const char *type;
  const char *structureKind;
};

SketchObjectPresentation
sketchObjectPresentation(sketch::SketchObjectKind kind) {
  switch (kind) {
  case sketch::SketchObjectKind::Rectangle:
    return {"Rectangle", "sketch-rectangle"};
  case sketch::SketchObjectKind::Point:
    return {"Point", "sketch-point"};
  case sketch::SketchObjectKind::Line:
    return {"Line", "sketch-line"};
  case sketch::SketchObjectKind::Circle:
    return {"Circle", "sketch-circle"};
  case sketch::SketchObjectKind::Arc:
    return {"Arc", "sketch-arc"};
  case sketch::SketchObjectKind::Slot:
    return {"Slot", "sketch-slot"};
  case sketch::SketchObjectKind::ArcSlot:
    return {"Arc Slot", "sketch-arc-slot"};
  case sketch::SketchObjectKind::Polyline:
    return {"Polyline", "sketch-polyline"};
  case sketch::SketchObjectKind::RegularPolygon:
    return {"Regular polygon", "sketch-polygon"};
  case sketch::SketchObjectKind::Oblong:
    return {"Oblong", "sketch-oblong"};
  case sketch::SketchObjectKind::Ellipse:
    return {"Ellipse", "sketch-ellipse"};
  case sketch::SketchObjectKind::EllipticalArc:
    return {"Elliptical Arc", "sketch-elliptical-arc"};
  case sketch::SketchObjectKind::HyperbolicArc:
    return {"Hyperbolic Arc", "sketch-hyperbolic-arc"};
  case sketch::SketchObjectKind::ParabolicArc:
    return {"Parabolic Arc", "sketch-parabolic-arc"};
  case sketch::SketchObjectKind::BSpline:
    return {"B-spline", "sketch-bspline"};
  case sketch::SketchObjectKind::Fillet:
    return {"Fillet", "sketch-fillet"};
  case sketch::SketchObjectKind::Chamfer:
    return {"Chamfer", "sketch-chamfer"};
  case sketch::SketchObjectKind::Offset:
    return {"Offset", "sketch-offset"};
  case sketch::SketchObjectKind::JoinedCurve:
    return {"Joined curve", "sketch-joined-curve"};
  case sketch::SketchObjectKind::CurveGroup:
    return {"Modified geometry", "sketch-curve-group"};
  }
  return {"Sketch geometry", "sketch-geometry"};
}

QString sketchMemberStructureKind(sketch::SketchObjectKind kind,
                                  std::string_view role) {
  if (kind == sketch::SketchObjectKind::Rectangle)
    return QStringLiteral("sketch-edge");
  if (kind == sketch::SketchObjectKind::Slot ||
      kind == sketch::SketchObjectKind::Oblong)
    return role == "start_cap" || role == "end_cap"
               ? QStringLiteral("sketch-arc")
               : QStringLiteral("sketch-line");
  if (kind == sketch::SketchObjectKind::ArcSlot)
    return QStringLiteral("sketch-arc");
  if (kind == sketch::SketchObjectKind::Polyline ||
      kind == sketch::SketchObjectKind::RegularPolygon)
    return QStringLiteral("sketch-line");
  if (kind == sketch::SketchObjectKind::Point)
    return QStringLiteral("sketch-point");
  if (kind == sketch::SketchObjectKind::Line)
    return QStringLiteral("sketch-line");
  if (kind == sketch::SketchObjectKind::Circle)
    return QStringLiteral("sketch-circle");
  if (kind == sketch::SketchObjectKind::Arc)
    return QStringLiteral("sketch-arc");
  if (kind == sketch::SketchObjectKind::Fillet)
    return QStringLiteral("sketch-arc");
  if (kind == sketch::SketchObjectKind::Chamfer)
    return QStringLiteral("sketch-line");
  if (kind == sketch::SketchObjectKind::Offset)
    return QStringLiteral("sketch-offset-curve");
  if (kind == sketch::SketchObjectKind::JoinedCurve)
    return QStringLiteral("sketch-bspline");
  if (kind == sketch::SketchObjectKind::CurveGroup)
    return QStringLiteral("sketch-geometry");
  if (kind == sketch::SketchObjectKind::Ellipse)
    return QStringLiteral("sketch-ellipse");
  if (kind == sketch::SketchObjectKind::EllipticalArc)
    return QStringLiteral("sketch-elliptical-arc");
  if (kind == sketch::SketchObjectKind::HyperbolicArc)
    return QStringLiteral("sketch-hyperbolic-arc");
  if (kind == sketch::SketchObjectKind::ParabolicArc)
    return QStringLiteral("sketch-parabolic-arc");
  return QStringLiteral("sketch-geometry");
}

QString sketchConstraintType(const sketch::Constraint &constraint) {
  return std::visit(
      []<typename Value>(const Value &) {
        using Type = std::decay_t<Value>;
        if constexpr (std::is_same_v<Type, sketch::Coincident>)
          return QStringLiteral("Coincident");
        if constexpr (std::is_same_v<Type, sketch::Horizontal>)
          return QStringLiteral("Horizontal");
        if constexpr (std::is_same_v<Type, sketch::Vertical>)
          return QStringLiteral("Vertical");
        if constexpr (std::is_same_v<Type, sketch::Parallel>)
          return QStringLiteral("Parallel");
        if constexpr (std::is_same_v<Type, sketch::Perpendicular>)
          return QStringLiteral("Perpendicular");
        if constexpr (std::is_same_v<Type, sketch::Tangent>)
          return QStringLiteral("Tangent");
        if constexpr (std::is_same_v<Type, sketch::Concentric>)
          return QStringLiteral("Concentric");
        if constexpr (std::is_same_v<Type, sketch::Equal>)
          return QStringLiteral("Equal");
        if constexpr (std::is_same_v<Type, sketch::Midpoint>)
          return QStringLiteral("Midpoint");
        if constexpr (std::is_same_v<Type, sketch::PointOnObject>)
          return QStringLiteral("Point on object");
        if constexpr (std::is_same_v<Type, sketch::Symmetric>)
          return QStringLiteral("Symmetric");
        if constexpr (std::is_same_v<Type, sketch::SymmetricAboutPoint>)
          return QStringLiteral("Symmetric");
        if constexpr (std::is_same_v<Type, sketch::Lock>)
          return QStringLiteral("Lock");
        if constexpr (std::is_same_v<Type, sketch::Block>)
          return QStringLiteral("Block");
        if constexpr (std::is_same_v<Type, sketch::Group>)
          return QStringLiteral("Group");
        if constexpr (std::is_same_v<Type, sketch::Collinear>)
          return QStringLiteral("Collinear");
        if constexpr (std::is_same_v<Type, sketch::Distance>)
          return QStringLiteral("Distance");
        if constexpr (std::is_same_v<Type, sketch::HorizontalDistance>)
          return QStringLiteral("Horizontal distance");
        if constexpr (std::is_same_v<Type, sketch::VerticalDistance>)
          return QStringLiteral("Vertical distance");
        if constexpr (std::is_same_v<Type, sketch::Radius>)
          return QStringLiteral("Radius");
        if constexpr (std::is_same_v<Type, sketch::Diameter>)
          return QStringLiteral("Diameter");
        if constexpr (std::is_same_v<Type, sketch::AngleBetween>)
          return QStringLiteral("Angle");
      },
      constraint);
}

std::optional<QString>
sketchConstraintValue(const sketch::Constraint &constraint,
                      const QString &lengthUnitId) {
  return std::visit(
      [&lengthUnitId]<typename Value>(
          const Value &value) -> std::optional<QString> {
        using Type = std::decay_t<Value>;
        if constexpr (std::is_same_v<Type, sketch::Distance> ||
                      std::is_same_v<Type, sketch::HorizontalDistance> ||
                      std::is_same_v<Type, sketch::VerticalDistance> ||
                      std::is_same_v<Type, sketch::Radius> ||
                      std::is_same_v<Type, sketch::Diameter>)
          return formatDisplayedLength(value.value.si() * millimetersPerMeter,
                                       lengthUnitId);
        if constexpr (std::is_same_v<Type, sketch::AngleBetween>)
          return QStringLiteral("%1°").arg(
              value.value.si() * 180.0 / std::numbers::pi, 0, 'f', 3);
        return std::nullopt;
      },
      constraint);
}

bool containsOption(const std::vector<UiOption> &options, const QString &id) {
  return std::any_of(options.cbegin(), options.cend(),
                     [&id](const UiOption &option) { return option.id == id; });
}

QString workspaceLabel(const std::vector<WorkspaceDescriptor> &workspaces,
                       const QString &workspaceId) {
  for (const WorkspaceDescriptor &workspace : workspaces) {
    if (workspace.id == workspaceId)
      return workspace.label;
  }
  return {};
}

QString sketchPointLabel(QStringView key) {
  if (key == QStringLiteral("start"))
    return QStringLiteral("Start point");
  if (key == QStringLiteral("end"))
    return QStringLiteral("End point");
  if (key == QStringLiteral("center"))
    return QStringLiteral("Center point");
  if (key == QStringLiteral("major"))
    return QStringLiteral("Major-axis point");
  if (key == QStringLiteral("minor"))
    return QStringLiteral("Minor-axis point");
  if (key == QStringLiteral("focus"))
    return QStringLiteral("Focus point");
  if (key.startsWith(QStringLiteral("control.")))
    return QStringLiteral("Control point %1")
        .arg(key.sliced(QStringLiteral("control.").size()));
  return QStringLiteral("Point");
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
  if (commandId == QStringLiteral("sketch.bspline.control-points") ||
      commandId == QStringLiteral("sketch.bspline.periodic-control-points")) {
    const bool periodic =
        commandId.endsWith(QStringLiteral("periodic-control-points"));
    return CommandForm{
        periodic ? QStringLiteral("Periodic B-spline")
                 : QStringLiteral("B-spline"),
        QStringLiteral("Choose control points, then apply"),
        {{commandId + QStringLiteral(".degree"), QStringLiteral("Degree"),
          FieldKind::Expression, QStringLiteral("3"), QStringLiteral("3")},
         sketchToggle(QStringLiteral("construction"),
                      QStringLiteral("Construction"))},
        false,
        false,
        SketchInputKind::PlanePoint,
        2,
        0,
        {}};
  }
  if (commandId == QStringLiteral("sketch.bspline.interpolation") ||
      commandId == QStringLiteral("sketch.bspline.periodic-interpolation")) {
    const bool periodic =
        commandId.endsWith(QStringLiteral("periodic-interpolation"));
    return CommandForm{
        periodic ? QStringLiteral("Periodic interpolated B-spline")
                 : QStringLiteral("Interpolated B-spline"),
        QStringLiteral("Choose interpolation points, then apply"),
        {sketchToggle(QStringLiteral("construction"),
                      QStringLiteral("Construction"))},
        false,
        false,
        SketchInputKind::PlanePoint,
        2,
        0,
        {}};
  }
  if (const auto edit = localBSplineEditKind(commandId)) {
    std::vector<FieldDescriptor> fields;
    QString guidance = QStringLiteral("Choose one B-spline");
    if (*edit == LocalBSplineEditKind::DecreaseDegree) {
      fields.emplace_back(commandId + QStringLiteral(".maximum-deviation"),
                          QStringLiteral("Maximum deviation"),
                          FieldKind::Expression, QStringLiteral("0.01 mm"),
                          QStringLiteral("0.01 mm"));
    } else if (*edit == LocalBSplineEditKind::IncreaseKnotMultiplicity ||
               *edit == LocalBSplineEditKind::DecreaseKnotMultiplicity) {
      fields.emplace_back(commandId + QStringLiteral(".knot"),
                          QStringLiteral("Knot number"), FieldKind::Expression,
                          QStringLiteral("2"), QStringLiteral("2"));
      if (*edit == LocalBSplineEditKind::DecreaseKnotMultiplicity)
        fields.emplace_back(commandId + QStringLiteral(".maximum-deviation"),
                            QStringLiteral("Maximum deviation"),
                            FieldKind::Expression, QStringLiteral("0.01 mm"),
                            QStringLiteral("0.01 mm"));
    } else if (*edit == LocalBSplineEditKind::InsertKnot) {
      fields.emplace_back(commandId + QStringLiteral(".parameter"),
                          QStringLiteral("Parameter"), FieldKind::Expression,
                          QStringLiteral("0.5"), QStringLiteral("0.5"));
    } else if (*edit == LocalBSplineEditKind::SetPoleWeight) {
      fields.emplace_back(commandId + QStringLiteral(".pole"),
                          QStringLiteral("Pole number"), FieldKind::Expression,
                          QStringLiteral("1"), QStringLiteral("1"));
      fields.emplace_back(commandId + QStringLiteral(".weight"),
                          QStringLiteral("Weight"), FieldKind::Expression,
                          QStringLiteral("1"), QStringLiteral("1"));
    }
    return CommandForm{commandLabel(sketchCommandRecords(), commandId),
                       std::move(guidance),
                       std::move(fields),
                       false,
                       false,
                       SketchInputKind::Entity,
                       1,
                       1,
                       {SketchSelectionKind::Curve}};
  }
  const auto transformPolicyFields = [&commandId]() {
    return std::vector<FieldDescriptor>{
        {commandId + QStringLiteral(".dimensions"),
         QStringLiteral("Copied dimensions"),
         FieldKind::Choice,
         QStringLiteral("preserve"),
         {},
         {{QStringLiteral("preserve"), QStringLiteral("Preserve values")},
          {QStringLiteral("equalize"), QStringLiteral("Equal to source")}}},
        {commandId + QStringLiteral(".external-constraints"),
         QStringLiteral("Outside constraints"),
         FieldKind::Choice,
         QStringLiteral("refuse"),
         {},
         {{QStringLiteral("refuse"), QStringLiteral("Keep and stop")},
          {QStringLiteral("detach"), QStringLiteral("Remove and continue")}}},
    };
  };
  if (commandId == QStringLiteral("sketch.translate")) {
    std::vector<FieldDescriptor> fields{
        {commandId + QStringLiteral(".mode"),
         QStringLiteral("Operation"),
         FieldKind::Choice,
         QStringLiteral("move"),
         {},
         {{QStringLiteral("move"), QStringLiteral("Move")},
          {QStringLiteral("array"), QStringLiteral("Rectangular array")}}},
        {commandId + QStringLiteral(".first-x"), QStringLiteral("Horizontal"),
         FieldKind::Expression, gridSpacing, gridSpacing},
        {commandId + QStringLiteral(".first-y"), QStringLiteral("Vertical"),
         FieldKind::Expression, QStringLiteral("0 mm"), QStringLiteral("0 mm")},
        {commandId + QStringLiteral(".copies"),
         QStringLiteral("Copies in first direction"), FieldKind::Expression,
         QStringLiteral("1"), QStringLiteral("1")},
        {commandId + QStringLiteral(".second-x"),
         QStringLiteral("Second direction horizontal"), FieldKind::Expression,
         QStringLiteral("0 mm"), QStringLiteral("0 mm")},
        {commandId + QStringLiteral(".second-y"),
         QStringLiteral("Second direction vertical"), FieldKind::Expression,
         QStringLiteral("0 mm"), QStringLiteral("0 mm")},
        {commandId + QStringLiteral(".rows"), QStringLiteral("Rows"),
         FieldKind::Expression, QStringLiteral("1"), QStringLiteral("1")},
    };
    auto policy = transformPolicyFields();
    fields.insert(fields.end(), std::make_move_iterator(policy.begin()),
                  std::make_move_iterator(policy.end()));
    return CommandForm{QStringLiteral("Translate"),
                       QStringLiteral("Choose geometry, then move or array it"),
                       std::move(fields),
                       false,
                       false,
                       SketchInputKind::Entity,
                       1,
                       1024,
                       {SketchSelectionKind::Curve}};
  }
  if (commandId == QStringLiteral("sketch.rotate")) {
    std::vector<FieldDescriptor> fields{
        {commandId + QStringLiteral(".mode"),
         QStringLiteral("Operation"),
         FieldKind::Choice,
         QStringLiteral("move"),
         {},
         {{QStringLiteral("move"), QStringLiteral("Rotate")},
          {QStringLiteral("array"), QStringLiteral("Polar array")}}},
        {commandId + QStringLiteral(".center-x"),
         QStringLiteral("Center horizontal"), FieldKind::Expression,
         QStringLiteral("0 mm"), QStringLiteral("0 mm")},
        {commandId + QStringLiteral(".center-y"),
         QStringLiteral("Center vertical"), FieldKind::Expression,
         QStringLiteral("0 mm"), QStringLiteral("0 mm")},
        {commandId + QStringLiteral(".angle"), QStringLiteral("Total angle"),
         FieldKind::Expression, QStringLiteral("90 deg"),
         QStringLiteral("90 deg")},
        {commandId + QStringLiteral(".copies"), QStringLiteral("Copies"),
         FieldKind::Expression, QStringLiteral("1"), QStringLiteral("1")},
    };
    auto policy = transformPolicyFields();
    fields.insert(fields.end(), std::make_move_iterator(policy.begin()),
                  std::make_move_iterator(policy.end()));
    return CommandForm{
        QStringLiteral("Rotate"),
        QStringLiteral("Choose geometry, then rotate or array it"),
        std::move(fields),
        false,
        false,
        SketchInputKind::Entity,
        1,
        1024,
        {SketchSelectionKind::Curve}};
  }
  if (commandId == QStringLiteral("sketch.scale")) {
    std::vector<FieldDescriptor> fields{
        {commandId + QStringLiteral(".mode"),
         QStringLiteral("Source geometry"),
         FieldKind::Choice,
         QStringLiteral("replace"),
         {},
         {{QStringLiteral("replace"), QStringLiteral("Replace originals")},
          {QStringLiteral("copy"), QStringLiteral("Keep originals")}}},
        {commandId + QStringLiteral(".center-x"),
         QStringLiteral("Center horizontal"), FieldKind::Expression,
         QStringLiteral("0 mm"), QStringLiteral("0 mm")},
        {commandId + QStringLiteral(".center-y"),
         QStringLiteral("Center vertical"), FieldKind::Expression,
         QStringLiteral("0 mm"), QStringLiteral("0 mm")},
        {commandId + QStringLiteral(".factor"), QStringLiteral("Scale factor"),
         FieldKind::Expression, QStringLiteral("2"), QStringLiteral("2")},
    };
    auto policy = transformPolicyFields();
    fields.insert(fields.end(), std::make_move_iterator(policy.begin()),
                  std::make_move_iterator(policy.end()));
    return CommandForm{QStringLiteral("Scale"),
                       QStringLiteral("Choose geometry, a center, and a scale"),
                       std::move(fields),
                       false,
                       false,
                       SketchInputKind::Entity,
                       1,
                       1024,
                       {SketchSelectionKind::Curve}};
  }
  if (commandId == QStringLiteral("sketch.symmetry")) {
    std::vector<FieldDescriptor> fields{
        {commandId + QStringLiteral(".mode"),
         QStringLiteral("Source geometry"),
         FieldKind::Choice,
         QStringLiteral("copy"),
         {},
         {{QStringLiteral("copy"), QStringLiteral("Keep originals")},
          {QStringLiteral("replace"), QStringLiteral("Replace originals")}}},
        {commandId + QStringLiteral(".axis-x"),
         QStringLiteral("Axis point horizontal"), FieldKind::Expression,
         QStringLiteral("0 mm"), QStringLiteral("0 mm")},
        {commandId + QStringLiteral(".axis-y"),
         QStringLiteral("Axis point vertical"), FieldKind::Expression,
         QStringLiteral("0 mm"), QStringLiteral("0 mm")},
        {commandId + QStringLiteral(".axis-angle"),
         QStringLiteral("Axis angle"), FieldKind::Expression,
         QStringLiteral("90 deg"), QStringLiteral("90 deg")},
    };
    auto policy = transformPolicyFields();
    fields.insert(fields.end(), std::make_move_iterator(policy.begin()),
                  std::make_move_iterator(policy.end()));
    return CommandForm{QStringLiteral("Symmetry"),
                       QStringLiteral("Choose geometry and the mirror axis"),
                       std::move(fields),
                       false,
                       false,
                       SketchInputKind::Entity,
                       1,
                       1024,
                       {SketchSelectionKind::Curve}};
  }
  if (commandId == QStringLiteral("sketch.fillet") ||
      commandId == QStringLiteral("sketch.chamfer")) {
    const bool fillet = commandId.endsWith(QStringLiteral("fillet"));
    return CommandForm{
        fillet ? QStringLiteral("Fillet") : QStringLiteral("Chamfer"),
        QStringLiteral("Choose two lines near the corner"),
        {{commandId + QStringLiteral(".size"),
          fillet ? QStringLiteral("Radius") : QStringLiteral("Distance"),
          FieldKind::Expression, gridSpacing, gridSpacing},
         {commandId + QStringLiteral(".external-constraints"),
          QStringLiteral("Affected constraints"),
          FieldKind::Choice,
          QStringLiteral("refuse"),
          {},
          {{QStringLiteral("refuse"), QStringLiteral("Keep and stop")},
           {QStringLiteral("detach"), QStringLiteral("Remove and continue")}}}},
        false,
        false,
        SketchInputKind::Entity,
        2,
        2,
        {SketchSelectionKind::Curve, SketchSelectionKind::Curve}};
  }
  if (commandId == QStringLiteral("sketch.offset"))
    return CommandForm{
        QStringLiteral("Offset"),
        QStringLiteral("Choose curves and set a signed distance"),
        {{commandId + QStringLiteral(".distance"), QStringLiteral("Distance"),
          FieldKind::Expression, gridSpacing, gridSpacing},
         {commandId + QStringLiteral(".source"),
          QStringLiteral("Source geometry"),
          FieldKind::Choice,
          QStringLiteral("keep"),
          {},
          {{QStringLiteral("keep"), QStringLiteral("Keep originals")},
           {QStringLiteral("delete"), QStringLiteral("Replace originals")}}},
         {commandId + QStringLiteral(".external-constraints"),
          QStringLiteral("Affected constraints"),
          FieldKind::Choice,
          QStringLiteral("refuse"),
          {},
          {{QStringLiteral("refuse"), QStringLiteral("Keep and stop")},
           {QStringLiteral("detach"), QStringLiteral("Remove and continue")}}}},
        false,
        false,
        SketchInputKind::Entity,
        1,
        1024,
        {SketchSelectionKind::Curve}};
  if (commandId == QStringLiteral("sketch.extend"))
    return CommandForm{
        QStringLiteral("Extend"),
        QStringLiteral(
            "Choose a line or arc near its endpoint, then choose the target"),
        {{commandId + QStringLiteral(".external-constraints"),
          QStringLiteral("Affected constraints"),
          FieldKind::Choice,
          QStringLiteral("refuse"),
          {},
          {{QStringLiteral("refuse"), QStringLiteral("Keep and stop")},
           {QStringLiteral("detach"), QStringLiteral("Remove and continue")}}}},
        false,
        false,
        SketchInputKind::Entity,
        2,
        2,
        {SketchSelectionKind::Curve}};
  if (commandId == QStringLiteral("sketch.trim"))
    return CommandForm{
        QStringLiteral("Trim"),
        QStringLiteral("Choose the curve segment to remove"),
        {{commandId + QStringLiteral(".external-constraints"),
          QStringLiteral("Affected constraints"),
          FieldKind::Choice,
          QStringLiteral("detach"),
          {},
          {{QStringLiteral("detach"), QStringLiteral("Remove affected")},
           {QStringLiteral("refuse"), QStringLiteral("Keep and stop")}}}},
        false,
        false,
        SketchInputKind::Entity,
        1,
        1,
        {SketchSelectionKind::Curve}};
  if (commandId == QStringLiteral("sketch.split"))
    return CommandForm{
        QStringLiteral("Split"),
        QStringLiteral("Choose a location on the curve to split"),
        {{commandId + QStringLiteral(".external-constraints"),
          QStringLiteral("Affected constraints"),
          FieldKind::Choice,
          QStringLiteral("refuse"),
          {},
          {{QStringLiteral("refuse"), QStringLiteral("Keep and stop")},
           {QStringLiteral("detach"), QStringLiteral("Remove and continue")}}}},
        false,
        false,
        SketchInputKind::Entity,
        1,
        1,
        {SketchSelectionKind::Curve}};
  if (commandId == QStringLiteral("sketch.join"))
    return CommandForm{
        QStringLiteral("Join"),
        QStringLiteral("Choose a shared curve endpoint"),
        {{commandId + QStringLiteral(".external-constraints"),
          QStringLiteral("Affected constraints"),
          FieldKind::Choice,
          QStringLiteral("refuse"),
          {},
          {{QStringLiteral("refuse"), QStringLiteral("Keep and stop")},
           {QStringLiteral("detach"), QStringLiteral("Remove and continue")}}}},
        false,
        false,
        SketchInputKind::Entity,
        1,
        1,
        {SketchSelectionKind::Point}};
  if (commandId == QStringLiteral("sketch.bspline.convert-to-nurbs"))
    return CommandForm{
        QStringLiteral("Convert to NURBS"),
        QStringLiteral("Choose one analytic curve"),
        {{commandId + QStringLiteral(".external-constraints"),
          QStringLiteral("Incompatible constraints"),
          FieldKind::Choice,
          QStringLiteral("refuse"),
          {},
          {{QStringLiteral("refuse"), QStringLiteral("Keep and stop")},
           {QStringLiteral("detach"), QStringLiteral("Remove and continue")}}}},
        false,
        false,
        SketchInputKind::Entity,
        1,
        1,
        {SketchSelectionKind::Curve}};
  if (commandId == QStringLiteral("sketch.polygon"))
    return CommandForm{
        QStringLiteral("Polygon"),
        QStringLiteral("Choose the center and a vertex"),
        {{commandId + QStringLiteral(".method"),
          QStringLiteral("Shape"),
          FieldKind::Choice,
          QStringLiteral("triangle"),
          {},
          {{QStringLiteral("triangle"), QStringLiteral("Triangle")},
           {QStringLiteral("square"), QStringLiteral("Square")},
           {QStringLiteral("pentagon"), QStringLiteral("Pentagon")},
           {QStringLiteral("hexagon"), QStringLiteral("Hexagon")},
           {QStringLiteral("heptagon"), QStringLiteral("Heptagon")},
           {QStringLiteral("octagon"), QStringLiteral("Octagon")},
           {QStringLiteral("regular"), QStringLiteral("Custom")}}},
         {commandId + QStringLiteral(".sides"),
          QStringLiteral("Sides"),
          FieldKind::Expression,
          QStringLiteral("3"),
          QStringLiteral("3"),
          {},
          true},
         sketchToggle(QStringLiteral("construction"),
                      QStringLiteral("Construction"))},
        false,
        false,
        SketchInputKind::PlanePoint,
        2,
        2,
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
  if (commandId == QStringLiteral("sketch.ellipse"))
    return CommandForm{
        QStringLiteral("Ellipse"),
        QStringLiteral("Choose the center, major axis, and minor radius"),
        {{commandId + QStringLiteral(".method"),
          QStringLiteral("Method"),
          FieldKind::Choice,
          QStringLiteral("center"),
          {},
          {{QStringLiteral("center"), QStringLiteral("Center")},
           {QStringLiteral("three-point"), QStringLiteral("Three point")}}},
         sketchToggle(QStringLiteral("construction"),
                      QStringLiteral("Construction"))},
        false,
        false,
        SketchInputKind::PlanePoint,
        3,
        3,
        {}};
  if (commandId == QStringLiteral("sketch.elliptical-arc"))
    return CommandForm{
        QStringLiteral("Elliptical Arc"),
        QStringLiteral("Choose the center, axes, start point, and end point"),
        {sketchToggle(QStringLiteral("construction"),
                      QStringLiteral("Construction"))},
        false,
        false,
        SketchInputKind::PlanePoint,
        5,
        5,
        {}};
  if (commandId == QStringLiteral("sketch.hyperbolic-arc"))
    return CommandForm{
        QStringLiteral("Hyperbolic Arc"),
        QStringLiteral("Choose center, major vertex, start, and end"),
        {sketchToggle(QStringLiteral("construction"),
                      QStringLiteral("Construction"))},
        false,
        false,
        SketchInputKind::PlanePoint,
        4,
        4,
        {}};
  if (commandId == QStringLiteral("sketch.parabolic-arc"))
    return CommandForm{QStringLiteral("Parabolic Arc"),
                       QStringLiteral("Choose focus, vertex, start, and end"),
                       {sketchToggle(QStringLiteral("construction"),
                                     QStringLiteral("Construction"))},
                       false,
                       false,
                       SketchInputKind::PlanePoint,
                       4,
                       4,
                       {}};
  if (commandId == QStringLiteral("sketch.slot"))
    return CommandForm{QStringLiteral("Slot"),
                       QStringLiteral("Choose start, end, and width"),
                       {sketchToggle(QStringLiteral("construction"),
                                     QStringLiteral("Construction"))},
                       false,
                       false,
                       SketchInputKind::PlanePoint,
                       3,
                       3,
                       {}};
  if (commandId == QStringLiteral("sketch.arc-slot"))
    return CommandForm{
        QStringLiteral("Arc Slot"),
        QStringLiteral("Choose center, radius, sweep, and width"),
        {sketchToggle(QStringLiteral("construction"),
                      QStringLiteral("Construction"))},
        false,
        false,
        SketchInputKind::PlanePoint,
        4,
        4,
        {}};
  if (commandId == QStringLiteral("sketch.oblong"))
    return CommandForm{QStringLiteral("Oblong"),
                       QStringLiteral("Choose two centers and the width"),
                       {sketchToggle(QStringLiteral("construction"),
                                     QStringLiteral("Construction"))},
                       false,
                       false,
                       SketchInputKind::PlanePoint,
                       3,
                       3,
                       {}};
  if (commandId == QStringLiteral("sketch.dimension"))
    return CommandForm{
        QStringLiteral("Dimension"),
        QStringLiteral("Choose geometry, then enter a value"),
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
         {commandId + QStringLiteral(".expression"), QStringLiteral("Value"),
          FieldKind::Expression, gridSpacing, gridSpacing}},
        false,
        false,
        SketchInputKind::Entity,
        1,
        2,
        {SketchSelectionKind::Any, SketchSelectionKind::Any}};
  if (commandId.startsWith(QStringLiteral("sketch."))) {
    const QString title = commandLabel(sketchCommandRecords(), commandId);
    const LocalSketchConstraintDefinition *definition =
        localSketchConstraintDefinition(commandId);
    const int minimumSelections =
        definition == nullptr
            ? 2
            : static_cast<int>(definition->minimumSelectionCount);
    const int maximumSelections =
        definition == nullptr || definition->maximumSelectionCount == 0U
            ? minimumSelections
            : static_cast<int>(definition->maximumSelectionCount);
    std::vector<SketchSelectionKind> selections;
    if (commandId == QStringLiteral("sketch.coincident")) {
      selections = {SketchSelectionKind::Point, SketchSelectionKind::Point};
    } else if (commandId == QStringLiteral("sketch.midpoint")) {
      selections = {SketchSelectionKind::Point, SketchSelectionKind::Curve};
    } else if (commandId == QStringLiteral("sketch.point-on-object")) {
      selections = {SketchSelectionKind::Point, SketchSelectionKind::Curve};
    } else if (commandId == QStringLiteral("sketch.symmetric")) {
      selections = {SketchSelectionKind::Point, SketchSelectionKind::Point,
                    SketchSelectionKind::Any};
    } else if (commandId == QStringLiteral("sketch.lock")) {
      selections = {SketchSelectionKind::Point};
    } else if (commandId == QStringLiteral("sketch.block") ||
               commandId == QStringLiteral("sketch.group")) {
      selections.assign(static_cast<std::size_t>(maximumSelections),
                        SketchSelectionKind::Any);
    } else {
      selections.assign(static_cast<std::size_t>(maximumSelections),
                        SketchSelectionKind::Curve);
    }
    return CommandForm{
        title,
        maximumSelections == 1
            ? QStringLiteral("Choose compatible Sketch geometry")
            : QStringLiteral("Choose compatible Sketch entities"),
        {},
        false,
        false,
        SketchInputKind::Entity,
        minimumSelections,
        maximumSelections,
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

struct SketchPrimitivePresentation {
  const char *type;
  const char *nameStem;
  const char *structureKind;
};

SketchPrimitivePresentation
sketchPrimitivePresentation(render::SketchPrimitiveKind kind) {
  switch (kind) {
  case render::SketchPrimitiveKind::Point:
    return {"Point", "Point", "sketch-point"};
  case render::SketchPrimitiveKind::Line:
    return {"Line", "Edge", "sketch-line"};
  case render::SketchPrimitiveKind::Circle:
    return {"Circle", "Circle", "sketch-circle"};
  case render::SketchPrimitiveKind::Arc:
    return {"Arc", "Arc", "sketch-arc"};
  case render::SketchPrimitiveKind::Ellipse:
    return {"Ellipse", "Ellipse", "sketch-ellipse"};
  case render::SketchPrimitiveKind::EllipticalArc:
    return {"Elliptical Arc", "Elliptical Arc", "sketch-elliptical-arc"};
  case render::SketchPrimitiveKind::HyperbolicArc:
    return {"Hyperbolic Arc", "Hyperbolic Arc", "sketch-hyperbolic-arc"};
  case render::SketchPrimitiveKind::ParabolicArc:
    return {"Parabolic Arc", "Parabolic Arc", "sketch-parabolic-arc"};
  case render::SketchPrimitiveKind::BSpline:
    return {"B-spline", "B-spline", "sketch-bspline"};
  }
  return {"Sketch geometry", "Geometry", "sketch-geometry"};
}

QString sketchPrimitiveName(render::SketchPrimitiveKind kind,
                            std::size_t index) {
  return QStringLiteral("%1 %2")
      .arg(QLatin1StringView{sketchPrimitivePresentation(kind).nameStem})
      .arg(index);
}

FieldDescriptor readOnlyTextField(QString id, QString label, QString value) {
  return {std::move(id),
          std::move(label),
          FieldKind::Text,
          std::move(value),
          {},
          {},
          true};
}

class DesktopController final : public FrontendController {
public:
  DesktopController(std::vector<UiOption> themeOptions, const QString &themeId,
                    const QString &defaultLengthUnitId,
                    const QString &interfaceDensityId,
                    const QString &navigationProfileId,
                    const QString &zoomDirectionId,
                    std::unique_ptr<LocalSketchSession> sketchSession = {})
      : localMode_(sketchSession != nullptr),
        localSketchSession_(std::move(sketchSession)) {
    snapshot_.generation = 1;
    snapshot_.projectName = QStringLiteral("Motor Bracket");
    snapshot_.branchLabel = QStringLiteral("main");
    snapshot_.revisionLabel = QStringLiteral("UI contract mode");
    snapshot_.projectRevision = QStringLiteral("capture.project.1");
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
         QStringLiteral("Separate from design commands"),
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
        QStringLiteral("capture projection"),
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
        QStringLiteral("capture projection"),
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
        QStringLiteral("capture projection"),
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
    snapshot_.historyCommands = historyCommandRecords();
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
        {QStringLiteral("job.backend"), QStringLiteral("Design engine"),
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
         QStringLiteral("The capture workspace has no interrupted writes."),
         QStringLiteral("current"), false},
    };
    snapshot_.operations = {
        {QStringLiteral("operation.services"), QStringLiteral("Design engine"),
         QStringLiteral("service"), QStringLiteral("unavailable"),
         QStringLiteral("No backend process has been started."), -1},
        {QStringLiteral("operation.frontend"),
         QStringLiteral("Frontend projection"), QStringLiteral("projection"),
         QStringLiteral("current"),
         QStringLiteral("Deterministic capture data is active."), 100},
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
    snapshot_.sketchEditing =
        workspaceId == QStringLiteral("sketch") && hasSelectedSketch();
    snapshot_.inspectorTitle = QStringLiteral("%1 workspace").arg(label);
    refreshWorkspace();
  }

  void selectEntity(const QString &entityId) override {
    snapshot_.selectedEntityId = entityId;
    if (entityId == mountingProfileFunction_.id)
      snapshot_.selectedFunction = mountingProfileFunction_;
    else if (entityId == basePlateFunction_.id)
      snapshot_.selectedFunction = basePlateFunction_;
    snapshot_.selectionSummary = humanSelectionName(entityId);
    snapshot_.selectedSketchScopes.clear();
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
    projectHumanSelection(entityId, {});
    if (snapshot_.sketchScene)
      rebuildSketchProjection();
    refreshContextCommands();
    ++snapshot_.generation;
  }

  void selectSketchEntity(const SketchSelectionScope &selection) override {
    if (!snapshot_.sketchEditing || !snapshot_.sketchScene)
      return;
    const auto entity = SketchEntityId::parse(selection.entityId.toStdString());
    const render::PackedSketchPrimitive *primitive =
        entity ? snapshot_.sketchScene->findPrimitive(*entity) : nullptr;
    const auto projected = std::ranges::find(
        snapshot_.sketchProjection.primitives, selection.entityId,
        &SketchPrimitiveProjection::id);
    if (!primitive || projected == snapshot_.sketchProjection.primitives.end() ||
        (!selection.pointKey.isEmpty() &&
         std::ranges::find(projected->pointKeys, selection.pointKey) ==
             projected->pointKeys.end()))
      return;
    snapshot_.selectedEntityId = selection.entityId;
    snapshot_.selectedSketchScopes.clear();
    projectHumanSelection(selection.entityId, selection.pointKey);
    rebuildSketchProjection();
    refreshContextCommands();
    ++snapshot_.generation;
  }

  void requestCommand(const QString &commandId) override {
    const QString statePrefix = QStringLiteral("capture.state.");
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
    if (commandId == QStringLiteral("sketch.edit")) {
      if (!hasSelectedSketch()) {
        snapshot_.activeCommandId.clear();
        snapshot_.fields.clear();
        snapshot_.commandDraft = {};
        clearSketchInteraction();
        snapshot_.inspectorTitle = QStringLiteral("Edit Sketch");
        snapshot_.inspectorStatus = QStringLiteral("Select a Sketch to edit");
        ++snapshot_.generation;
        return;
      }
      snapshot_.activeWorkspaceId = QStringLiteral("sketch");
      snapshot_.sketchEditing = true;
      snapshot_.activeCommandId.clear();
      snapshot_.fields.clear();
      snapshot_.commandDraft = {};
      clearSketchInteraction();
      refreshContextCommands();
      snapshot_.inspectorTitle = QStringLiteral("Sketch 1");
      snapshot_.inspectorStatus = QStringLiteral("Editing Sketch");
      restoreWorkspaceViewport();
      ++snapshot_.generation;
      return;
    }
    bool *splinePresentation = nullptr;
    QString splinePresentationName;
    if (commandId == QStringLiteral("sketch.bspline.control-polygon")) {
      splinePresentation = &snapshot_.sketchControlPolygonVisible;
      splinePresentationName = QStringLiteral("Control polygon");
    } else if (commandId ==
               QStringLiteral("sketch.bspline.curvature-comb")) {
      splinePresentation = &snapshot_.sketchCurvatureCombVisible;
      splinePresentationName = QStringLiteral("Curvature comb");
    } else if (commandId == QStringLiteral("sketch.bspline.degree-labels")) {
      splinePresentation = &snapshot_.sketchDegreeLabelsVisible;
      splinePresentationName = QStringLiteral("Degree labels");
    } else if (commandId == QStringLiteral("sketch.bspline.knot-labels")) {
      splinePresentation = &snapshot_.sketchKnotLabelsVisible;
      splinePresentationName = QStringLiteral("Knot multiplicity");
    } else if (commandId == QStringLiteral("sketch.bspline.weight-labels")) {
      splinePresentation = &snapshot_.sketchWeightLabelsVisible;
      splinePresentationName = QStringLiteral("Pole weights");
    }
    if (localMode_ && splinePresentation) {
      if (!snapshot_.sketchEditing || !hasSelectedBSpline()) {
        snapshot_.inspectorStatus = QStringLiteral("Select one B-spline to show ") +
                                    splinePresentationName.toLower();
        ++snapshot_.generation;
        return;
      }
      *splinePresentation = !*splinePresentation;
      snapshot_.inspectorStatus =
          splinePresentationName +
          (*splinePresentation ? QStringLiteral(" shown")
                               : QStringLiteral(" hidden"));
      refreshContextCommands();
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
          {QStringLiteral("corner"), QStringLiteral("Corner")},
          {QStringLiteral("center"), QStringLiteral("Center")}};
    } else if (localMode_ && commandId == QStringLiteral("sketch.arc")) {
      form->fields.front().options = {
          {QStringLiteral("three-point"), QStringLiteral("Three point")},
          {QStringLiteral("center"), QStringLiteral("Center")}};
    } else if (localMode_ && commandId == QStringLiteral("sketch.ellipse")) {
      form->fields.front().options = {
          {QStringLiteral("center"), QStringLiteral("Center")},
          {QStringLiteral("three-point"), QStringLiteral("Three point")}};
    }
    if (!descriptor->workspaceId.isEmpty() &&
        descriptor->workspaceId != snapshot_.activeWorkspaceId) {
      snapshot_.activeWorkspaceId = descriptor->workspaceId;
      snapshot_.sketchEditing = false;
      refreshContextCommands();
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
    if (localMode_ && commandId == QStringLiteral("model.sketch.create") &&
        localSketchPlaneFromId(snapshot_.selectedEntityId)) {
      snapshot_.fields.front().value = snapshot_.selectedEntityId;
      static_cast<void>(submitLocalSketchCreation());
    }
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
    if (fieldId == QStringLiteral("sketch.polygon.method")) {
      const QString method = std::get<QString>(value);
      const std::map<QString, QString> presetSides{
          {QStringLiteral("triangle"), QStringLiteral("3")},
          {QStringLiteral("square"), QStringLiteral("4")},
          {QStringLiteral("pentagon"), QStringLiteral("5")},
          {QStringLiteral("hexagon"), QStringLiteral("6")},
          {QStringLiteral("heptagon"), QStringLiteral("7")},
          {QStringLiteral("octagon"), QStringLiteral("8")},
      };
      const auto sides = std::ranges::find_if(
          snapshot_.fields, [](const FieldDescriptor &field) {
            return field.id == QStringLiteral("sketch.polygon.sides");
          });
      if (sides != snapshot_.fields.end()) {
        const auto preset = presetSides.find(method);
        sides->readOnly = preset != presetSides.end();
        sides->value =
            preset == presetSides.end() ? QStringLiteral("9") : preset->second;
        sides->effectiveValue = std::get<QString>(sides->value);
      }
    }
    if (fieldId == QStringLiteral("sketch.dimension.kind")) {
      const QString kind = std::get<QString>(value);
      const auto dimensionValue = std::ranges::find_if(
          snapshot_.fields, [](const FieldDescriptor &field) {
            return field.id == QStringLiteral("sketch.dimension.expression");
          });
      if (dimensionValue != snapshot_.fields.end()) {
        dimensionValue->value =
            kind == QStringLiteral("angle")
                ? QStringLiteral("45 deg")
                : gridSpacingFor(snapshot_.projectLengthUnitId);
        dimensionValue->effectiveValue =
            std::get<QString>(dimensionValue->value);
      }
    }
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
    if (!snapshot_.sketchEditing ||
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
      if (const auto tool = localSketchToolKind(request.commandId);
          localMode_ && tool && isLocalSketchBSpline(*tool) &&
          !sketchInputs_.empty()) {
        const auto samePoint = [](PlanePoint first, PlanePoint second) {
          return std::hypot(first.xMetres - second.xMetres,
                            first.yMetres - second.yMetres) <= 1.0e-12;
        };
        if (samePoint(request.planePoint, sketchInputs_.back().planePoint))
          return false;
        if (isLocalSketchPeriodicBSpline(*tool) && sketchInputs_.size() >= 2U &&
            samePoint(request.planePoint, sketchInputs_.front().planePoint))
          return submitLocalSketchTool();
      }
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
      if ((localMode_ && localBSplineEditKind(request.commandId) &&
           primitive->kind != SketchPrimitiveKind::BSpline) ||
          (localMode_ &&
           request.commandId ==
               QStringLiteral("sketch.bspline.convert-to-nurbs") &&
           primitive->kind == SketchPrimitiveKind::BSpline) ||
          (required == SketchSelectionKind::Point && !pointExists) ||
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
    if (localMode_ && localSketchToolKind(request.commandId) &&
        snapshot_.sketchInteraction.maximumInputCount > 0 &&
        snapshot_.sketchInteraction.inputCount ==
            snapshot_.sketchInteraction.maximumInputCount)
      return submitLocalSketchTool();
    if (localMode_ && localSketchConstraintDefinition(request.commandId) &&
        snapshot_.sketchInteraction.inputCount ==
            snapshot_.sketchInteraction.maximumInputCount)
      return submitLocalSketchConstraint();
    if (localMode_ &&
        (request.commandId == QStringLiteral("sketch.trim") ||
         request.commandId == QStringLiteral("sketch.split") ||
         request.commandId == QStringLiteral("sketch.join") ||
         request.commandId ==
             QStringLiteral("sketch.bspline.convert-to-nurbs")) &&
        snapshot_.sketchInteraction.inputCount ==
            snapshot_.sketchInteraction.maximumInputCount)
      return submitLocalSketchCurveModify();
    return true;
  }

  bool removeLastSketchInput() override {
    if (!snapshot_.sketchEditing || sketchInputs_.empty() ||
        snapshot_.sketchInteraction.inputKind == SketchInputKind::None ||
        snapshot_.commandDraft.state == CommandDraftState::Pending)
      return false;
    sketchInputs_.pop_back();
    rebuildSketchProjection();
    updateSketchReadiness();
    ++snapshot_.generation;
    return true;
  }

  bool toggleSketchConstruction() override {
    if (!localMode_ || !localSketchSession_ || !snapshot_.sketchEditing ||
        snapshot_.selectedSketchScopes.size() != 1U ||
        !snapshot_.selectedSketchScopes.front().pointKey.isEmpty() ||
        localSketchSession_->pendingOperationCount() != 0U)
      return false;
    localEditEntity_ = snapshot_.selectedSketchScopes.front().entityId;
    const bool queued = localSketchSession_->toggleConstruction(
        {localEditEntity_}, [this](Result<LocalSketchProjection> result) {
          completeLocalOperation(QStringLiteral("sketch.construction.toggle"),
                                 std::move(result));
        });
    if (queued)
      setLocalOperationPending();
    snapshot_.inspectorStatus =
        queued ? QStringLiteral("Updating construction geometry")
               : QStringLiteral("Finish the current operation first");
    ++snapshot_.generation;
    return queued;
  }

  bool dragSketchCurve(const QString &entityId, PlanePoint first,
                       PlanePoint current) override {
    if (!localMode_ || !localSketchSession_ || !snapshot_.sketchEditing ||
        !snapshot_.activeCommandId.isEmpty() || entityId.isEmpty() ||
        localSketchSession_->pendingOperationCount() != 0U)
      return false;
    localSketchSession_->cancelPreview();
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
    else
      snapshot_.sketchScene = localCommittedSketchScene_;
    snapshot_.inspectorStatus =
        queued ? QStringLiteral("Resizing Sketch geometry")
               : QStringLiteral("Finish the current operation first");
    ++snapshot_.generation;
    return queued;
  }

  bool previewSketchCurve(const QString &entityId, PlanePoint first,
                          PlanePoint current) override {
    if (!localMode_ || !localSketchSession_ || !snapshot_.sketchEditing ||
        !snapshot_.activeCommandId.isEmpty() || entityId.isEmpty() ||
        localSketchSession_->pendingOperationCount() != 0U)
      return false;
    return localSketchSession_->previewCurveDrag(
        {entityId, first.xMetres, first.yMetres, current.xMetres,
         current.yMetres},
        [this](Result<std::shared_ptr<const render::SketchSceneSnapshot>>
                   result) {
          if (!result)
            return;
          snapshot_.sketchScene = std::move(*result);
          ++snapshot_.generation;
          notifyChanged();
        });
  }

  void clearSketchCurvePreview() override {
    if (!localSketchSession_)
      return;
    localSketchSession_->cancelPreview();
    if (snapshot_.sketchScene == localCommittedSketchScene_)
      return;
    snapshot_.sketchScene = localCommittedSketchScene_;
    ++snapshot_.generation;
    notifyChanged();
  }

  bool previewSketchCurveModify(const QString &entityId,
                                PlanePoint reference) override {
    const QString commandId = snapshot_.activeCommandId;
    if (!localMode_ || !localSketchSession_ || !snapshot_.sketchEditing ||
        (commandId != QStringLiteral("sketch.trim") &&
         commandId != QStringLiteral("sketch.split")) || entityId.isEmpty() ||
        localSketchSession_->pendingOperationCount() != 0U)
      return false;
    if (commandId == QStringLiteral("sketch.split")) {
      const QString markerId = QStringLiteral("draft.curve-modify.1");
      const auto marker = std::ranges::find(
          snapshot_.sketchProjection.primitives, markerId,
          &SketchPrimitiveProjection::id);
      if (marker != snapshot_.sketchProjection.primitives.end()) {
        if (marker->points.size() == 1U && marker->points.front() == reference)
          return true;
        marker->points = {reference};
      } else {
        rebuildSketchProjection();
        appendDraftPoint(reference, markerId, false);
      }
      ++snapshot_.generation;
      notifyChanged();
      return true;
    }
    const QString expectedRevision = snapshot_.projectRevision;
    const auto publish =
        [this, commandId,
         expectedRevision](std::vector<LocalSketchToolPoint> points) {
          if (snapshot_.activeCommandId != commandId ||
              snapshot_.projectRevision != expectedRevision)
            return;
          const auto previous = snapshot_.sketchProjection.primitives;
          rebuildSketchProjection();
          for (std::size_t index = 0U; index < points.size(); ++index) {
            const LocalSketchToolPoint point = points[index];
            appendDraftPoint(
                {point.xMetres, point.yMetres},
                QStringLiteral("draft.curve-modify.%1").arg(index + 1U),
                false);
          }
          if (snapshot_.sketchProjection.primitives == previous)
            return;
          ++snapshot_.generation;
          notifyChanged();
        };
    return localSketchSession_->previewTrim(
        {entityId, reference.xMetres, reference.yMetres},
        [publish](const Result<LocalTrimPreview> &result) mutable {
          if (result)
            publish(result->boundaries);
        });
  }

  void clearSketchCurveModifyPreview() override {
    if (!localSketchSession_)
      return;
    localSketchSession_->cancelPreview();
    const bool visible = std::ranges::any_of(
        snapshot_.sketchProjection.primitives,
        [](const SketchPrimitiveProjection &primitive) {
          return primitive.id.startsWith(
              QStringLiteral("draft.curve-modify."));
        });
    if (!visible)
      return;
    rebuildSketchProjection();
    ++snapshot_.generation;
    notifyChanged();
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
        localSketchToolKind(request.commandId))
      return submitLocalSketchTool();
    if (localMode_ && mode == CommandDraftMode::Apply &&
        request.commandId == QStringLiteral("sketch.dimension"))
      return submitLocalSketchDimension();
    if (localMode_ && mode == CommandDraftMode::Apply &&
        localBSplineEditKind(request.commandId))
      return submitLocalBSplineEdit();
    if (localMode_ && mode == CommandDraftMode::Apply &&
        localSketchTransformCommand(request.commandId))
      return submitLocalSketchTransform();
    if (localMode_ && mode == CommandDraftMode::Apply &&
        localSketchCurveModifyCommand(request.commandId))
      return submitLocalSketchCurveModify();
    if (localMode_ && mode == CommandDraftMode::Apply &&
        localSketchConstraintDefinition(request.commandId))
      return submitLocalSketchConstraint();
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
    if (localSketchSession_)
      localSketchSession_->cancelPreview();
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
        QStringLiteral("capture.project.%1").arg(snapshot_.generation + 1);
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
          QStringLiteral("capture revision %1").arg(snapshot_.generation + 1);
      basePlateFunction_.revision = snapshot_.selectedFunction.revision;
      mountingProfileFunction_.revision = snapshot_.selectedFunction.revision;
      snapshot_.sketchProjection.sourceRevision =
          snapshot_.selectedFunction.revision;
      snapshot_.projectRevision =
          QStringLiteral("capture.project.%1").arg(snapshot_.generation + 1);
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
  [[nodiscard]] bool hasSelectedSketch() const {
    if (snapshot_.selectedEntityId.isEmpty())
      return false;
    return snapshot_.selectedEntityId == localSketchFunction_.id ||
           snapshot_.selectedEntityId == mountingProfileFunction_.id;
  }

  [[nodiscard]] const StructureItem *
  structureItem(const QString &entityId) const {
    const auto found = std::ranges::find_if(
        snapshot_.structure,
        [&entityId](const StructureItem &item) { return item.id == entityId; });
    return found == snapshot_.structure.end() ? nullptr : &*found;
  }

  struct OwnedSketchMember {
    const sketch::SketchObject *object = nullptr;
    const sketch::SketchObjectMember *member = nullptr;
  };

  [[nodiscard]] std::optional<OwnedSketchMember>
  ownedSketchMember(const QString &entityId) const {
    auto parsed = SketchEntityId::parse(entityId.toStdString());
    if (!parsed)
      return std::nullopt;
    for (const sketch::SketchObject &object : localSketchObjects_)
      for (const sketch::SketchObjectMember &member : object.members)
        if (member.entity == *parsed)
          return OwnedSketchMember{&object, &member};
    return std::nullopt;
  }

  [[nodiscard]] const sketch::SketchObject *
  sketchObject(const QString &objectId) const {
    auto parsed = SketchObjectId::parse(objectId.toStdString());
    if (!parsed)
      return nullptr;
    const auto found = std::ranges::find(localSketchObjects_, *parsed,
                                         &sketch::SketchObject::id);
    return found == localSketchObjects_.end() ? nullptr : &*found;
  }

  [[nodiscard]] const sketch::Constraint *
  sketchConstraint(const QString &constraintId) const {
    auto parsed = SketchConstraintId::parse(constraintId.toStdString());
    if (!parsed)
      return nullptr;
    const auto found = std::ranges::find_if(
        localSketchConstraints_, [&parsed](const sketch::Constraint &value) {
          return sketch::constraintId(value) == *parsed;
        });
    return found == localSketchConstraints_.end() ? nullptr : &*found;
  }

  [[nodiscard]] QString humanSelectionName(const QString &entityId) const {
    if (const auto owned = ownedSketchMember(entityId)) {
      const QString label = QString::fromStdString(owned->object->label);
      return owned->object->members.size() == 1U
                 ? label
                 : label + QStringLiteral(" · ") +
                       sketchMemberLabel(owned->member->role);
    }
    if (const StructureItem *item = structureItem(entityId))
      return item->label;
    return QStringLiteral("Model geometry");
  }

  [[nodiscard]] bool hasSelectedBSpline() const {
    if (!snapshot_.sketchScene || snapshot_.selectedSketchScopes.size() != 1U ||
        !snapshot_.selectedSketchScopes.front().pointKey.isEmpty())
      return false;
    const auto entity = SketchEntityId::parse(
        snapshot_.selectedSketchScopes.front().entityId.toStdString());
    const render::PackedSketchPrimitive *primitive =
        entity ? snapshot_.sketchScene->findPrimitive(*entity) : nullptr;
    return primitive &&
           primitive->kind == render::SketchPrimitiveKind::BSpline;
  }

  void refreshContextCommands() {
    const bool sketchSelected = hasSelectedSketch();
    const auto splinePresentationState = [this](QStringView command)
        -> std::optional<bool> {
      if (command == QStringLiteral("sketch.bspline.control-polygon"))
        return snapshot_.sketchControlPolygonVisible;
      if (command == QStringLiteral("sketch.bspline.curvature-comb"))
        return snapshot_.sketchCurvatureCombVisible;
      if (command == QStringLiteral("sketch.bspline.degree-labels"))
        return snapshot_.sketchDegreeLabelsVisible;
      if (command == QStringLiteral("sketch.bspline.knot-labels"))
        return snapshot_.sketchKnotLabelsVisible;
      if (command == QStringLiteral("sketch.bspline.weight-labels"))
        return snapshot_.sketchWeightLabelsVisible;
      return std::nullopt;
    };
    const auto update = [this, sketchSelected, &splinePresentationState](
                            std::vector<CommandDescriptor> &commands) {
      for (CommandDescriptor &command : commands) {
        if (command.id == QStringLiteral("sketch.edit")) {
          command.available = sketchSelected;
          command.unavailableReason =
              sketchSelected ? QString{}
                             : QStringLiteral("Select a Sketch to edit");
        } else if (const auto state =
                       splinePresentationState(command.id)) {
          command.available = snapshot_.sketchEditing && hasSelectedBSpline();
          command.checked = *state;
          command.unavailableReason =
              command.available
                  ? QString{}
                  : QStringLiteral("Select one B-spline in an open Sketch");
        } else if (command.id.startsWith(QStringLiteral("sketch."))) {
          const bool implemented = hasFrontendCommandContractId(command.id);
          command.available = snapshot_.sketchEditing && implemented;
          command.unavailableReason =
              command.available ? QString{}
              : !snapshot_.sketchEditing
                  ? QStringLiteral("Open a Sketch to use this tool")
                  : commandUnavailableReason(command.id);
        }
      }
    };
    update(snapshot_.commandCatalog);
    snapshot_.commands = commandsFor(snapshot_.activeWorkspaceId,
                                     snapshot_.sketchEditing, sketchSelected);
    update(snapshot_.commands);
  }

  void projectHumanSelection(const QString &entityId,
                             const QString &pointKey) {
    const StructureItem *item = structureItem(entityId);
    const QString label = humanSelectionName(entityId);
    snapshot_.selectionSummary = label;
    const auto appendBSplineFields =
        [this](const render::PackedSketchPrimitive *primitive) {
          if (!primitive ||
              primitive->kind != render::SketchPrimitiveKind::BSpline ||
              !snapshot_.sketchScene ||
              primitive->spline >= snapshot_.sketchScene->splines().size())
            return;
          const render::PackedSketchSpline &spline =
              snapshot_.sketchScene->splines()[primitive->spline];
          const auto weights = snapshot_.sketchScene->splineWeights().subspan(
              spline.firstWeight, spline.controlPointCount);
          const bool rational =
              !weights.empty() &&
              std::ranges::any_of(weights,
                                  [first = weights.front()](double weight) {
                                    return weight != first;
                                  });
          snapshot_.fields.push_back(readOnlyTextField(
              QStringLiteral("selection.degree"), QStringLiteral("Degree"),
              QString::number(spline.degree)));
          snapshot_.fields.push_back(
              readOnlyTextField(QStringLiteral("selection.control-points"),
                                QStringLiteral("Control points"),
                                QString::number(spline.controlPointCount)));
          snapshot_.fields.push_back(readOnlyTextField(
              QStringLiteral("selection.form"), QStringLiteral("Form"),
              spline.periodic ? QStringLiteral("Periodic")
                              : QStringLiteral("Open")));
          snapshot_.fields.push_back(readOnlyTextField(
              QStringLiteral("selection.weights"), QStringLiteral("Weights"),
              rational ? QStringLiteral("Rational")
                       : QStringLiteral("Uniform")));
        };

    if (localMode_ && snapshot_.sketchScene) {
      auto sketchId = SketchEntityId::parse(entityId.toStdString());
      const render::PackedSketchPrimitive *primitive =
          sketchId ? snapshot_.sketchScene->findPrimitive(*sketchId) : nullptr;
      if (primitive) {
        if (!pointKey.isEmpty()) {
          const auto projected = std::ranges::find(
              snapshot_.sketchProjection.primitives, entityId,
              &SketchPrimitiveProjection::id);
          if (projected == snapshot_.sketchProjection.primitives.end())
            return;
          const auto key = std::ranges::find(projected->pointKeys, pointKey);
          if (key == projected->pointKeys.end())
            return;
          const std::size_t index =
              static_cast<std::size_t>(key - projected->pointKeys.begin());
          if (index >= projected->points.size())
            return;
          const PlanePoint point = projected->points[index];
          const QString pointLabel = sketchPointLabel(pointKey);
          snapshot_.selectedSketchScopes = {{entityId, pointKey}};
          snapshot_.selectionSummary = label + QStringLiteral(" · ") + pointLabel;
          snapshot_.inspectorTitle = snapshot_.selectionSummary;
          snapshot_.inspectorStatus =
              snapshot_.sketchEditing ? QStringLiteral("Selected in Sketch")
                                      : QStringLiteral("Open the Sketch to edit");
          snapshot_.fields = {
              readOnlyTextField(QStringLiteral("selection.type"),
                                QStringLiteral("Type"),
                                QStringLiteral("Point")),
              readOnlyTextField(QStringLiteral("selection.parent"),
                                QStringLiteral("Geometry"), label),
              readOnlyTextField(
                  QStringLiteral("selection.x"), QStringLiteral("X"),
                  formatDisplayedLength(millimetersFromMetres(point.xMetres),
                                        snapshot_.projectLengthUnitId)),
              readOnlyTextField(
                  QStringLiteral("selection.y"), QStringLiteral("Y"),
                  formatDisplayedLength(millimetersFromMetres(point.yMetres),
                                        snapshot_.projectLengthUnitId)),
          };
          return;
        }
        snapshot_.selectedSketchScopes = {{entityId, {}}};
        snapshot_.inspectorTitle = label;
        snapshot_.inspectorStatus =
            snapshot_.sketchEditing
                ? QStringLiteral(
                      "Drag to resize · Press X to toggle construction")
                : QStringLiteral("Open the Sketch to edit this geometry");
        const auto styles = snapshot_.sketchScene->styles();
        const bool construction = primitive->style < styles.size() &&
                                  styles[primitive->style].role ==
                                      render::SketchStyleRole::Construction;
        snapshot_.fields = {
            readOnlyTextField(
                QStringLiteral("selection.type"), QStringLiteral("Type"),
                QLatin1StringView{
                    sketchPrimitivePresentation(primitive->kind).type}),
            readOnlyTextField(
                QStringLiteral("selection.role"), QStringLiteral("Role"),
                construction ? QStringLiteral("Construction geometry")
                             : QStringLiteral("Profile geometry")),
        };
        const auto points = snapshot_.sketchScene->points();
        if (primitive->kind == render::SketchPrimitiveKind::Line &&
            primitive->firstPoint + 1U < points.size()) {
          const auto &first = points[primitive->firstPoint];
          const auto &second = points[primitive->firstPoint + 1U];
          snapshot_.fields.insert(
              snapshot_.fields.begin() + 1,
              readOnlyTextField(
                  QStringLiteral("selection.length"), QStringLiteral("Length"),
                  formatDisplayedLength(
                      millimetersFromMetres(
                          std::hypot(second.x - first.x, second.y - first.y)),
                      snapshot_.projectLengthUnitId)));
        } else if (primitive->kind == render::SketchPrimitiveKind::Circle ||
                   primitive->kind == render::SketchPrimitiveKind::Arc) {
          snapshot_.fields.insert(
              snapshot_.fields.begin() + 1,
              readOnlyTextField(QStringLiteral("selection.radius"),
                                QStringLiteral("Radius"),
                                formatDisplayedLength(
                                    millimetersFromMetres(primitive->radius),
                                    snapshot_.projectLengthUnitId)));
        } else if (primitive->kind == render::SketchPrimitiveKind::Ellipse ||
                   primitive->kind ==
                       render::SketchPrimitiveKind::EllipticalArc ||
                   primitive->kind ==
                       render::SketchPrimitiveKind::HyperbolicArc) {
          snapshot_.fields.insert(
              snapshot_.fields.begin() + 1,
              readOnlyTextField(QStringLiteral("selection.major-radius"),
                                QStringLiteral("Major radius"),
                                formatDisplayedLength(
                                    millimetersFromMetres(primitive->radius),
                                    snapshot_.projectLengthUnitId)));
          snapshot_.fields.insert(
              snapshot_.fields.begin() + 2,
              readOnlyTextField(
                  QStringLiteral("selection.minor-radius"),
                  QStringLiteral("Minor radius"),
                  formatDisplayedLength(
                      millimetersFromMetres(primitive->secondaryRadius),
                      snapshot_.projectLengthUnitId)));
          snapshot_.fields.insert(
              snapshot_.fields.begin() + 3,
              readOnlyTextField(
                  QStringLiteral("selection.rotation"),
                  QStringLiteral("Rotation"),
                  QStringLiteral("%1°").arg(primitive->rotationAngleRadians *
                                                180.0 / std::numbers::pi,
                                            0, 'f', 1)));
          if (primitive->kind == render::SketchPrimitiveKind::EllipticalArc)
            snapshot_.fields.insert(
                snapshot_.fields.begin() + 4,
                readOnlyTextField(QStringLiteral("selection.sweep"),
                                  QStringLiteral("Sweep"),
                                  QStringLiteral("%1°").arg(
                                      std::abs(primitive->sweepAngleRadians) *
                                          180.0 / std::numbers::pi,
                                      0, 'f', 1)));
        } else if (primitive->kind ==
                   render::SketchPrimitiveKind::ParabolicArc) {
          snapshot_.fields.insert(
              snapshot_.fields.begin() + 1,
              readOnlyTextField(QStringLiteral("selection.focal-length"),
                                QStringLiteral("Focal length"),
                                formatDisplayedLength(
                                    millimetersFromMetres(primitive->radius),
                                    snapshot_.projectLengthUnitId)));
          snapshot_.fields.insert(
              snapshot_.fields.begin() + 2,
              readOnlyTextField(
                  QStringLiteral("selection.transverse-span"),
                  QStringLiteral("Transverse span"),
                  formatDisplayedLength(millimetersFromMetres(std::abs(
                                            primitive->sweepAngleRadians)),
                                        snapshot_.projectLengthUnitId)));
          snapshot_.fields.insert(
              snapshot_.fields.begin() + 3,
              readOnlyTextField(
                  QStringLiteral("selection.rotation"),
                  QStringLiteral("Axis angle"),
                  QStringLiteral("%1°").arg(primitive->rotationAngleRadians *
                                                180.0 / std::numbers::pi,
                                            0, 'f', 1)));
        } else if (primitive->kind == render::SketchPrimitiveKind::BSpline) {
          appendBSplineFields(primitive);
        }
        return;
      }
    }

    if (const sketch::Constraint *constraint = sketchConstraint(entityId)) {
      std::vector<SketchEntityId> entities;
      std::visit(
          [&entities]<typename Value>(const Value &value) {
            using Type = std::decay_t<Value>;
            const auto appendPoint =
                [&entities](const sketch::PointRef &point) {
                  entities.push_back(point.entity);
                };
            if constexpr (std::is_same_v<Type, sketch::Coincident> ||
                          std::is_same_v<Type, sketch::Distance> ||
                          std::is_same_v<Type, sketch::HorizontalDistance> ||
                          std::is_same_v<Type, sketch::VerticalDistance>) {
              appendPoint(value.first);
              appendPoint(value.second);
            } else if constexpr (std::is_same_v<Type, sketch::Horizontal> ||
                                 std::is_same_v<Type, sketch::Vertical>) {
              entities.push_back(value.line);
            } else if constexpr (std::is_same_v<Type, sketch::Parallel> ||
                                 std::is_same_v<Type, sketch::Perpendicular> ||
                                 std::is_same_v<Type, sketch::Tangent> ||
                                 std::is_same_v<Type, sketch::Concentric> ||
                                 std::is_same_v<Type, sketch::Equal> ||
                                 std::is_same_v<Type, sketch::Collinear> ||
                                 std::is_same_v<Type, sketch::AngleBetween>) {
              entities.push_back(value.first);
              entities.push_back(value.second);
            } else if constexpr (std::is_same_v<Type, sketch::Midpoint>) {
              appendPoint(value.point);
              entities.push_back(value.line);
            } else if constexpr (std::is_same_v<Type, sketch::PointOnObject>) {
              appendPoint(value.point);
              entities.push_back(value.curve);
            } else if constexpr (std::is_same_v<Type, sketch::Symmetric>) {
              appendPoint(value.first);
              appendPoint(value.second);
              entities.push_back(value.axis);
            } else if constexpr (std::is_same_v<Type,
                                                sketch::SymmetricAboutPoint>) {
              appendPoint(value.first);
              appendPoint(value.second);
              appendPoint(value.center);
            } else if constexpr (std::is_same_v<Type, sketch::Lock>) {
              appendPoint(value.point);
            } else if constexpr (std::is_same_v<Type, sketch::Block>) {
              entities.push_back(value.entity);
            } else if constexpr (std::is_same_v<Type, sketch::Group>) {
              entities.insert(entities.end(), value.entities.begin(),
                              value.entities.end());
            } else if constexpr (std::is_same_v<Type, sketch::Radius> ||
                                 std::is_same_v<Type, sketch::Diameter>) {
              entities.push_back(value.curve);
            }
          },
          *constraint);
      snapshot_.selectedSketchScopes.clear();
      QStringList geometry;
      for (const SketchEntityId &selected : entities) {
        const QString id = QString::fromStdString(selected.toString());
        const SketchSelectionScope scope{id, {}};
        if (std::ranges::find(snapshot_.selectedSketchScopes, scope) ==
            snapshot_.selectedSketchScopes.end()) {
          snapshot_.selectedSketchScopes.push_back(scope);
          geometry.push_back(humanSelectionName(id));
        }
      }
      snapshot_.inspectorTitle = item ? item->label : label;
      snapshot_.inspectorStatus =
          snapshot_.sketchEditing ? QStringLiteral("Applied in Sketch")
                                  : QStringLiteral("Open the Sketch to edit");
      snapshot_.fields = {
          readOnlyTextField(QStringLiteral("selection.type"),
                            QStringLiteral("Type"),
                            sketchConstraintType(*constraint)),
          readOnlyTextField(QStringLiteral("selection.geometry"),
                            QStringLiteral("Geometry"),
                            geometry.join(QStringLiteral(" and "))),
      };
      if (const auto value =
              sketchConstraintValue(*constraint, snapshot_.projectLengthUnitId))
        snapshot_.fields.push_back(
            readOnlyTextField(QStringLiteral("selection.value"),
                              QStringLiteral("Value"), *value));
      return;
    }

    if (const sketch::SketchObject *object = sketchObject(entityId)) {
      snapshot_.selectedSketchScopes.clear();
      snapshot_.selectedSketchScopes.reserve(object->members.size());
      for (const sketch::SketchObjectMember &member : object->members)
        snapshot_.selectedSketchScopes.push_back(
            {QString::fromStdString(member.entity.toString()), {}});
      double minimumX = std::numeric_limits<double>::infinity();
      double minimumY = std::numeric_limits<double>::infinity();
      double maximumX = -std::numeric_limits<double>::infinity();
      double maximumY = -std::numeric_limits<double>::infinity();
      double totalLineLength = 0.0;
      if (snapshot_.sketchScene) {
        const auto points = snapshot_.sketchScene->points();
        for (const sketch::SketchObjectMember &member : object->members) {
          const auto *primitive =
              snapshot_.sketchScene->findPrimitive(member.entity);
          if (!primitive ||
              primitive->kind != render::SketchPrimitiveKind::Line ||
              primitive->firstPoint + 1U >= points.size())
            continue;
          for (std::size_t offset = 0; offset < 2U; ++offset) {
            const auto &point = points[primitive->firstPoint + offset];
            minimumX = std::min(minimumX, point.x);
            minimumY = std::min(minimumY, point.y);
            maximumX = std::max(maximumX, point.x);
            maximumY = std::max(maximumY, point.y);
          }
          const auto &first = points[primitive->firstPoint];
          const auto &second = points[primitive->firstPoint + 1U];
          totalLineLength += std::hypot(second.x - first.x, second.y - first.y);
        }
      }
      snapshot_.inspectorTitle = QString::fromStdString(object->label);
      const auto presentation = sketchObjectPresentation(object->kind);
      snapshot_.inspectorStatus =
          snapshot_.sketchEditing
              ? object->members.size() == 1U
                    ? QStringLiteral("Selected in Sketch")
                    : QStringLiteral("Select a member to edit")
              : QStringLiteral("Open the Sketch to edit");
      snapshot_.fields = {
          readOnlyTextField(QStringLiteral("selection.type"),
                            QStringLiteral("Type"),
                            QLatin1StringView{presentation.type}),
      };
      if (object->kind == sketch::SketchObjectKind::Rectangle) {
        snapshot_.fields.push_back(
            readOnlyTextField(QStringLiteral("selection.edges"),
                              QStringLiteral("Edges"), QStringLiteral("4")));
      }
      if (object->kind == sketch::SketchObjectKind::Rectangle &&
          std::isfinite(minimumX) && std::isfinite(minimumY)) {
        snapshot_.fields.insert(
            snapshot_.fields.begin() + 1,
            readOnlyTextField(QStringLiteral("selection.width"),
                              QStringLiteral("Width"),
                              formatDisplayedLength(
                                  millimetersFromMetres(maximumX - minimumX),
                                  snapshot_.projectLengthUnitId)));
        snapshot_.fields.insert(
            snapshot_.fields.begin() + 2,
            readOnlyTextField(QStringLiteral("selection.height"),
                              QStringLiteral("Height"),
                              formatDisplayedLength(
                                  millimetersFromMetres(maximumY - minimumY),
                                  snapshot_.projectLengthUnitId)));
      }
      if (object->kind == sketch::SketchObjectKind::Polyline) {
        snapshot_.fields.insert(
            snapshot_.fields.begin() + 1,
            readOnlyTextField(
                QStringLiteral("selection.length"), QStringLiteral("Length"),
                formatDisplayedLength(millimetersFromMetres(totalLineLength),
                                      snapshot_.projectLengthUnitId)));
        snapshot_.fields.push_back(readOnlyTextField(
            QStringLiteral("selection.segments"), QStringLiteral("Segments"),
            QString::number(object->members.size())));
      }
      if (object->kind == sketch::SketchObjectKind::RegularPolygon) {
        snapshot_.fields.insert(
            snapshot_.fields.begin() + 1,
            readOnlyTextField(
                QStringLiteral("selection.perimeter"),
                QStringLiteral("Perimeter"),
                formatDisplayedLength(millimetersFromMetres(totalLineLength),
                                      snapshot_.projectLengthUnitId)));
        snapshot_.fields.push_back(readOnlyTextField(
            QStringLiteral("selection.sides"), QStringLiteral("Sides"),
            QString::number(object->members.size())));
        if (!object->members.empty() && snapshot_.sketchScene) {
          const auto points = snapshot_.sketchScene->points();
          double centerX = 0.0;
          double centerY = 0.0;
          std::size_t vertexCount = 0U;
          for (const sketch::SketchObjectMember &member : object->members) {
            const auto *side =
                snapshot_.sketchScene->findPrimitive(member.entity);
            if (!side || side->kind != render::SketchPrimitiveKind::Line ||
                side->firstPoint >= points.size())
              continue;
            centerX += points[side->firstPoint].x;
            centerY += points[side->firstPoint].y;
            ++vertexCount;
          }
          if (vertexCount == object->members.size()) {
            centerX /= static_cast<double>(vertexCount);
            centerY /= static_cast<double>(vertexCount);
            const auto *firstSide = snapshot_.sketchScene->findPrimitive(
                object->members.front().entity);
            const auto &vertex = points[firstSide->firstPoint];
            snapshot_.fields.push_back(readOnlyTextField(
                QStringLiteral("selection.radius"),
                QStringLiteral("Circumradius"),
                formatDisplayedLength(
                    millimetersFromMetres(
                        std::hypot(vertex.x - centerX, vertex.y - centerY)),
                    snapshot_.projectLengthUnitId)));
          }
        }
      }
      const auto memberPrimitive = [&](std::string_view role) {
        const auto member = std::ranges::find(
            object->members, role, &sketch::SketchObjectMember::role);
        return member == object->members.end() || !snapshot_.sketchScene
                   ? static_cast<const render::PackedSketchPrimitive *>(nullptr)
                   : snapshot_.sketchScene->findPrimitive(member->entity);
      };
      const auto displayedLength = [&](double metres) {
        return formatDisplayedLength(millimetersFromMetres(metres),
                                     snapshot_.projectLengthUnitId);
      };
      const render::PackedSketchPrimitive *only =
          object->members.size() == 1U && snapshot_.sketchScene
              ? snapshot_.sketchScene->findPrimitive(
                    object->members.front().entity)
              : nullptr;
      if (only && only->kind == render::SketchPrimitiveKind::Point &&
          only->firstPoint < snapshot_.sketchScene->points().size()) {
        const auto &point = snapshot_.sketchScene->points()[only->firstPoint];
        snapshot_.fields.push_back(
            readOnlyTextField(QStringLiteral("selection.x"),
                              QStringLiteral("X"), displayedLength(point.x)));
        snapshot_.fields.push_back(
            readOnlyTextField(QStringLiteral("selection.y"),
                              QStringLiteral("Y"), displayedLength(point.y)));
      } else if (only && only->kind == render::SketchPrimitiveKind::Line &&
                 only->firstPoint + 1U <
                     snapshot_.sketchScene->points().size()) {
        const auto points = snapshot_.sketchScene->points();
        const auto &first = points[only->firstPoint];
        const auto &second = points[only->firstPoint + 1U];
        snapshot_.fields.push_back(readOnlyTextField(
            QStringLiteral("selection.length"), QStringLiteral("Length"),
            displayedLength(
                std::hypot(second.x - first.x, second.y - first.y))));
      } else if (only && (only->kind == render::SketchPrimitiveKind::Circle ||
                          only->kind == render::SketchPrimitiveKind::Arc)) {
        snapshot_.fields.push_back(readOnlyTextField(
            QStringLiteral("selection.radius"), QStringLiteral("Radius"),
            displayedLength(only->radius)));
        if (only->kind == render::SketchPrimitiveKind::Arc)
          snapshot_.fields.push_back(readOnlyTextField(
              QStringLiteral("selection.sweep"), QStringLiteral("Sweep"),
              QStringLiteral("%1°").arg(std::abs(only->sweepAngleRadians) *
                                            180.0 / std::numbers::pi,
                                        0, 'f', 1)));
      } else if (only &&
                 (only->kind == render::SketchPrimitiveKind::Ellipse ||
                  only->kind == render::SketchPrimitiveKind::EllipticalArc ||
                  only->kind == render::SketchPrimitiveKind::HyperbolicArc)) {
        snapshot_.fields.push_back(readOnlyTextField(
            QStringLiteral("selection.major-radius"),
            QStringLiteral("Major radius"), displayedLength(only->radius)));
        snapshot_.fields.push_back(
            readOnlyTextField(QStringLiteral("selection.minor-radius"),
                              QStringLiteral("Minor radius"),
                              displayedLength(only->secondaryRadius)));
        snapshot_.fields.push_back(readOnlyTextField(
            QStringLiteral("selection.rotation"), QStringLiteral("Rotation"),
            QStringLiteral("%1°").arg(only->rotationAngleRadians * 180.0 /
                                          std::numbers::pi,
                                      0, 'f', 1)));
        if (only->kind == render::SketchPrimitiveKind::EllipticalArc)
          snapshot_.fields.push_back(readOnlyTextField(
              QStringLiteral("selection.sweep"), QStringLiteral("Sweep"),
              QStringLiteral("%1°").arg(std::abs(only->sweepAngleRadians) *
                                            180.0 / std::numbers::pi,
                                        0, 'f', 1)));
      } else if (only &&
                 only->kind == render::SketchPrimitiveKind::ParabolicArc) {
        snapshot_.fields.push_back(readOnlyTextField(
            QStringLiteral("selection.focal-length"),
            QStringLiteral("Focal length"), displayedLength(only->radius)));
        snapshot_.fields.push_back(readOnlyTextField(
            QStringLiteral("selection.transverse-span"),
            QStringLiteral("Transverse span"),
            displayedLength(std::abs(only->sweepAngleRadians))));
        snapshot_.fields.push_back(readOnlyTextField(
            QStringLiteral("selection.rotation"), QStringLiteral("Axis angle"),
            QStringLiteral("%1°").arg(only->rotationAngleRadians * 180.0 /
                                          std::numbers::pi,
                                      0, 'f', 1)));
      } else if (only && only->kind == render::SketchPrimitiveKind::BSpline) {
        appendBSplineFields(only);
      } else if (object->kind == sketch::SketchObjectKind::Slot ||
                 object->kind == sketch::SketchObjectKind::Oblong) {
        const auto *side = memberPrimitive("top_side");
        const auto *cap = memberPrimitive("start_cap");
        if (side &&
            side->firstPoint + 1U < snapshot_.sketchScene->points().size()) {
          const auto points = snapshot_.sketchScene->points();
          const auto &first = points[side->firstPoint];
          const auto &second = points[side->firstPoint + 1U];
          snapshot_.fields.push_back(
              readOnlyTextField(QStringLiteral("selection.center-distance"),
                                QStringLiteral("Center distance"),
                                displayedLength(std::hypot(
                                    second.x - first.x, second.y - first.y))));
        }
        if (cap)
          snapshot_.fields.push_back(readOnlyTextField(
              QStringLiteral("selection.width"), QStringLiteral("Width"),
              displayedLength(2.0 * cap->radius)));
      } else if (object->kind == sketch::SketchObjectKind::ArcSlot) {
        const auto *outer = memberPrimitive("outer");
        const auto *inner = memberPrimitive("inner");
        if (outer && inner) {
          snapshot_.fields.push_back(readOnlyTextField(
              QStringLiteral("selection.centerline-radius"),
              QStringLiteral("Centerline radius"),
              displayedLength((outer->radius + inner->radius) / 2.0)));
          snapshot_.fields.push_back(readOnlyTextField(
              QStringLiteral("selection.width"), QStringLiteral("Width"),
              displayedLength(outer->radius - inner->radius)));
          snapshot_.fields.push_back(readOnlyTextField(
              QStringLiteral("selection.sweep"), QStringLiteral("Sweep"),
              QStringLiteral("%1°").arg(std::abs(outer->sweepAngleRadians) *
                                            180.0 / std::numbers::pi,
                                        0, 'f', 1)));
        }
      }
      return;
    }

    snapshot_.inspectorTitle = label;
    if (hasSelectedSketch()) {
      const std::size_t objectCount = localSketchObjects_.size();
      snapshot_.inspectorStatus = snapshot_.sketchEditing
                                      ? QStringLiteral("Editing Sketch")
                                      : QStringLiteral("Ready to edit");
      snapshot_.fields = {
          readOnlyTextField(QStringLiteral("selection.type"),
                            QStringLiteral("Type"), QStringLiteral("Sketch")),
          readOnlyTextField(
              QStringLiteral("selection.attachment"), QStringLiteral("Plane"),
              snapshot_.gridPlaneLabel + QStringLiteral(" Plane")),
          readOnlyTextField(
              QStringLiteral("selection.object-count"),
              QStringLiteral("Objects"),
              QStringLiteral("%1 item%2")
                  .arg(objectCount)
                  .arg(objectCount == 1U ? QString{} : QStringLiteral("s"))),
          readOnlyTextField(QStringLiteral("selection.profile-count"),
                            QStringLiteral("Profiles"),
                            QString::number(localSketchProfileCount_)),
      };
      return;
    }

    const QString kind = item ? item->kind : QStringLiteral("geometry");
    QString type = QStringLiteral("Model geometry");
    QString status = QStringLiteral("Selected");
    if (kind == QStringLiteral("plane")) {
      type = QStringLiteral("Datum plane");
      status = QStringLiteral("Ready for Sketch attachment");
    } else if (kind == QStringLiteral("component")) {
      type = QStringLiteral("Component");
    } else if (kind == QStringLiteral("group")) {
      type = QStringLiteral("Group");
    } else if (kind == QStringLiteral("project")) {
      type = QStringLiteral("Project");
    } else if (kind == QStringLiteral("sketch-rectangle")) {
      type = QStringLiteral("Rectangle");
    } else if (kind == QStringLiteral("sketch-curve-group")) {
      type = QStringLiteral("Modified geometry");
    } else if (kind == QStringLiteral("sketch-point")) {
      type = QStringLiteral("Point");
    } else if (kind == QStringLiteral("sketch-line")) {
      type = QStringLiteral("Line");
    } else if (kind == QStringLiteral("sketch-circle")) {
      type = QStringLiteral("Circle");
    } else if (kind == QStringLiteral("sketch-arc")) {
      type = QStringLiteral("Arc");
    } else if (kind == QStringLiteral("sketch-slot")) {
      type = QStringLiteral("Slot");
    } else if (kind == QStringLiteral("sketch-arc-slot")) {
      type = QStringLiteral("Arc Slot");
    } else if (kind == QStringLiteral("sketch-ellipse")) {
      type = QStringLiteral("Ellipse");
    } else if (kind == QStringLiteral("sketch-elliptical-arc")) {
      type = QStringLiteral("Elliptical Arc");
    } else if (kind == QStringLiteral("sketch-hyperbolic-arc")) {
      type = QStringLiteral("Hyperbolic Arc");
    } else if (kind == QStringLiteral("sketch-parabolic-arc")) {
      type = QStringLiteral("Parabolic Arc");
    }
    snapshot_.inspectorStatus = status;
    snapshot_.fields = {readOnlyTextField(QStringLiteral("selection.type"),
                                          QStringLiteral("Type"), type)};
  }

  void refreshInspectorContext() {
    if (!snapshot_.selectedEntityId.isEmpty()) {
      projectHumanSelection(snapshot_.selectedEntityId, {});
      return;
    }
    if (snapshot_.activeWorkspaceId == QStringLiteral("sketch")) {
      snapshot_.inspectorTitle = snapshot_.sketchEditing
                                     ? QStringLiteral("Sketch 1")
                                     : QStringLiteral("Sketch workspace");
      snapshot_.inspectorStatus =
          snapshot_.sketchEditing
              ? QStringLiteral("Editing Sketch")
              : QStringLiteral("Select a plane or create a new Sketch");
      return;
    }
    snapshot_.inspectorStatus =
        localMode_ ? (localBackendState_ == LocalBackendState::Ready
                          ? QStringLiteral("Ready")
                      : localBackendState_ == LocalBackendState::Failed
                          ? QStringLiteral("Design engine failed")
                          : QStringLiteral("Starting the design engine"))
                   : QStringLiteral("Engineering backend disconnected");
  }

  std::vector<SketchPrimitiveProjection> baseSketchPrimitives() const {
    if (!localMode_)
      return mountingProfileProjection();
    std::vector<SketchPrimitiveProjection> result;
    if (!snapshot_.sketchScene)
      return result;
    const auto points = snapshot_.sketchScene->points();
    const auto styles = snapshot_.sketchScene->styles();
    result.reserve(snapshot_.sketchScene->primitives().size());
    for (const render::PackedSketchPrimitive &source :
         snapshot_.sketchScene->primitives()) {
      SketchPrimitiveProjection primitive;
      primitive.id = QString::fromStdString(source.entity.toString());
      primitive.radiusMetres = source.radius;
      primitive.startAngleRadians = source.startAngleRadians;
      primitive.sweepAngleRadians = source.sweepAngleRadians;
      primitive.secondaryRadiusMetres = source.secondaryRadius;
      primitive.rotationAngleRadians = source.rotationAngleRadians;
      primitive.construction =
          source.style < styles.size() &&
          styles[source.style].role == render::SketchStyleRole::Construction;
      primitive.selected = std::ranges::any_of(
          snapshot_.selectedSketchScopes,
          [&primitive](const SketchSelectionScope &selection) {
            return selection.entityId == primitive.id &&
                   selection.pointKey.isEmpty();
          });
      for (const SketchSelectionScope &selection :
           snapshot_.selectedSketchScopes)
        if (selection.entityId == primitive.id &&
            !selection.pointKey.isEmpty())
          primitive.selectedPointKeys.push_back(selection.pointKey);
      if (source.kind == render::SketchPrimitiveKind::BSpline) {
        const render::PackedSketchSpline &spline =
            snapshot_.sketchScene->splines()[source.spline];
        const std::size_t count = spline.controlPointCount;
        primitive.kind = SketchPrimitiveKind::BSpline;
        primitive.splineDegree = spline.degree;
        primitive.splinePeriodic = spline.periodic;
        const auto coordinates =
            snapshot_.sketchScene->splineControlPointCoordinates().subspan(
                static_cast<std::size_t>(spline.firstControlPoint) * 2U,
                count * 2U);
        primitive.points.reserve(count);
        primitive.pointKeys.reserve(count);
        for (std::size_t index = 0U; index < count; ++index) {
          primitive.points.push_back(
              {coordinates[index * 2U], coordinates[index * 2U + 1U]});
          primitive.pointKeys.push_back(
              QStringLiteral("control.%1").arg(index + 1U));
        }
        const auto knots = snapshot_.sketchScene->splineKnots().subspan(
            spline.firstKnot, count + spline.degree + 1U);
        primitive.splineKnots.assign(knots.begin(), knots.end());
        const auto weights = snapshot_.sketchScene->splineWeights().subspan(
            spline.firstWeight, count);
        primitive.splineWeights.assign(weights.begin(), weights.end());
        result.push_back(std::move(primitive));
        continue;
      }
      if (source.firstPoint >= points.size())
        continue;
      const auto appendPoint = [&](std::size_t index) {
        primitive.points.push_back({points[index].x, points[index].y});
      };
      appendPoint(source.firstPoint);
      switch (source.kind) {
      case render::SketchPrimitiveKind::Point:
        primitive.kind = SketchPrimitiveKind::Point;
        primitive.pointKeys = {QStringLiteral("point")};
        break;
      case render::SketchPrimitiveKind::Line:
        if (source.firstPoint + 1U >= points.size())
          continue;
        primitive.kind = SketchPrimitiveKind::Line;
        appendPoint(source.firstPoint + 1U);
        primitive.pointKeys = {QStringLiteral("start"), QStringLiteral("end")};
        break;
      case render::SketchPrimitiveKind::Circle:
        primitive.kind = SketchPrimitiveKind::Circle;
        primitive.pointKeys = {QStringLiteral("center")};
        break;
      case render::SketchPrimitiveKind::Arc:
        primitive.kind = SketchPrimitiveKind::Arc;
        primitive.pointKeys = {QStringLiteral("start"), QStringLiteral("end"),
                               QStringLiteral("center")};
        break;
      case render::SketchPrimitiveKind::Ellipse:
        primitive.kind = SketchPrimitiveKind::Ellipse;
        primitive.pointKeys = {QStringLiteral("center"),
                               QStringLiteral("major"),
                               QStringLiteral("minor")};
        break;
      case render::SketchPrimitiveKind::EllipticalArc:
        primitive.kind = SketchPrimitiveKind::EllipticalArc;
        primitive.pointKeys = {QStringLiteral("center"),
                               QStringLiteral("major"), QStringLiteral("minor"),
                               QStringLiteral("start"), QStringLiteral("end")};
        break;
      case render::SketchPrimitiveKind::HyperbolicArc:
        primitive.kind = SketchPrimitiveKind::HyperbolicArc;
        primitive.pointKeys = {
            QStringLiteral("center"), QStringLiteral("major"),
            QStringLiteral("minor"),  QStringLiteral("focus"),
            QStringLiteral("start"),  QStringLiteral("end")};
        break;
      case render::SketchPrimitiveKind::ParabolicArc:
        primitive.kind = SketchPrimitiveKind::ParabolicArc;
        primitive.pointKeys = {QStringLiteral("center"),
                               QStringLiteral("focus"), QStringLiteral("start"),
                               QStringLiteral("end")};
        break;
      case render::SketchPrimitiveKind::BSpline:
        std::unreachable();
      }
      result.push_back(std::move(primitive));
    }
    return result;
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
    snapshot_.viewportDetail = QStringLiteral("Starting the design engine");
    snapshot_.modelHealth = QStringLiteral("Starting");
    snapshot_.inspectorStatus = QStringLiteral("Starting the design engine");
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
        {QStringLiteral("job.engineering"), QStringLiteral("Design engine"),
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
         QStringLiteral("Design engine"), QStringLiteral("service"),
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
        snapshot_.jobs = {
            {QStringLiteral("job.engineering"), QStringLiteral("Design engine"),
             QStringLiteral("Ready"), 100},
        };
        snapshot_.operations = {
            {QStringLiteral("operation.engineering"),
             QStringLiteral("Design engine"), QStringLiteral("service"),
             QStringLiteral("current"),
             QStringLiteral("Ready to accept canonical commands."), 100},
        };
        refreshInspectorContext();
      } else {
        const QString summary = QString::fromStdString(result.error().summary);
        snapshot_.inspectorStatus = summary;
        snapshot_.jobs = {
            {QStringLiteral("job.engineering"), QStringLiteral("Design engine"),
             QStringLiteral("Failed"), -1},
        };
        snapshot_.operations = {
            {QStringLiteral("operation.engineering"),
             QStringLiteral("Design engine"), QStringLiteral("service"),
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
               : QStringLiteral("Finish the current operation first");
    ++snapshot_.generation;
    return queued;
  }

  bool submitLocalSketchTool() {
    const auto kind = localSketchToolKind(
        snapshot_.activeCommandId, activeChoice(QStringLiteral(".method")));
    if (!localSketchSession_ || !kind || sketchInputs_.empty() ||
        snapshot_.commandDraft.state == CommandDraftState::Pending)
      return false;
    LocalSketchToolGesture gesture;
    gesture.kind = *kind;
    gesture.construction = activeSketchConstruction();
    gesture.closed = activeSketchToggle(QStringLiteral(".close-profile"));
    gesture.sideCount = activeSketchSideCount();
    gesture.degree = activeSketchDegree();
    gesture.points.reserve(sketchInputs_.size());
    for (const SketchInputRequest &input : sketchInputs_)
      gesture.points.push_back(
          {input.planePoint.xMetres, input.planePoint.yMetres});
    const QString command = snapshot_.activeCommandId;
    const QString label = snapshot_.inspectorTitle;
    const bool queued = localSketchSession_->applyTool(
        std::move(gesture),
        [this, command](Result<LocalSketchProjection> result) {
          completeLocalOperation(command, std::move(result));
        });
    snapshot_.commandDraft.state =
        queued ? CommandDraftState::Pending : CommandDraftState::Rejected;
    if (queued)
      setLocalOperationPending();
    snapshot_.inspectorStatus =
        queued ? QStringLiteral("Applying %1 to canonical source").arg(label)
               : QStringLiteral("Finish the current operation first");
    ++snapshot_.generation;
    return queued;
  }

  bool submitLocalSketchConstraint() {
    const LocalSketchConstraintDefinition *definition =
        localSketchConstraintDefinition(snapshot_.activeCommandId);
    const std::size_t minimum =
        definition == nullptr ? 0U : definition->minimumSelectionCount;
    const std::size_t maximum =
        definition == nullptr || definition->maximumSelectionCount == 0U
            ? minimum
            : definition->maximumSelectionCount;
    if (!localSketchSession_ || !definition || sketchInputs_.size() < minimum ||
        sketchInputs_.size() > maximum ||
        snapshot_.commandDraft.state == CommandDraftState::Pending)
      return false;
    LocalSketchConstraintGesture gesture{definition->kind, {}, std::nullopt};
    gesture.selections.reserve(sketchInputs_.size());
    for (const SketchInputRequest &input : sketchInputs_)
      gesture.selections.push_back({input.entityId, input.subElementKey});
    const QString command = snapshot_.activeCommandId;
    const bool queued = localSketchSession_->applyConstraint(
        std::move(gesture),
        [this, command](Result<LocalSketchProjection> result) {
          completeLocalOperation(command, std::move(result));
        });
    snapshot_.commandDraft.state =
        queued ? CommandDraftState::Pending : CommandDraftState::Rejected;
    if (queued)
      setLocalOperationPending();
    snapshot_.inspectorStatus =
        queued ? QStringLiteral("Applying %1 constraint")
                     .arg(QString::fromLatin1(
                         definition->label.data(),
                         static_cast<qsizetype>(definition->label.size())))
               : QStringLiteral("Finish the current operation first");
    ++snapshot_.generation;
    return queued;
  }

  bool submitLocalBSplineEdit() {
    const auto kind = localBSplineEditKind(snapshot_.activeCommandId);
    const auto reject = [this](QString guidance) {
      snapshot_.commandDraft.state = CommandDraftState::Rejected;
      snapshot_.inspectorStatus = std::move(guidance);
      ++snapshot_.generation;
      return false;
    };
    if (!localSketchSession_ || !kind || sketchInputs_.size() != 1U ||
        !sketchInputs_.front().subElementKey.isEmpty() ||
        snapshot_.commandDraft.state == CommandDraftState::Pending)
      return reject(QStringLiteral("Choose one B-spline"));

    const auto fieldText =
        [this](QStringView suffix) -> std::optional<QString> {
      const auto field = std::ranges::find_if(
          snapshot_.fields, [suffix](const FieldDescriptor &candidate) {
            return candidate.id.endsWith(suffix);
          });
      if (field == snapshot_.fields.end() ||
          !std::holds_alternative<QString>(field->value))
        return std::nullopt;
      return std::get<QString>(field->value).trimmed();
    };
    const auto oneBasedIndex =
        [&](QStringView suffix) -> std::optional<std::size_t> {
      const auto text = fieldText(suffix);
      if (!text)
        return std::nullopt;
      bool valid = false;
      const qulonglong value = text->toULongLong(&valid);
      if (!valid || value == 0U ||
          value - 1U > std::numeric_limits<std::size_t>::max())
        return std::nullopt;
      return static_cast<std::size_t>(value - 1U);
    };

    LocalBSplineEdit edit{*kind, sketchInputs_.front().entityId};
    if (*kind == LocalBSplineEditKind::IncreaseKnotMultiplicity ||
        *kind == LocalBSplineEditKind::DecreaseKnotMultiplicity) {
      const auto index = oneBasedIndex(QStringLiteral(".knot"));
      if (!index)
        return reject(
            QStringLiteral("Knot number must be a positive whole number"));
      edit.index = *index;
    } else if (*kind == LocalBSplineEditKind::InsertKnot) {
      const auto parameter = fieldText(QStringLiteral(".parameter"));
      bool valid = false;
      edit.value = parameter ? parameter->toDouble(&valid) : 0.0;
      if (!valid || !std::isfinite(edit.value))
        return reject(QStringLiteral("Parameter must be a finite number"));
    } else if (*kind == LocalBSplineEditKind::SetPoleWeight) {
      const auto index = oneBasedIndex(QStringLiteral(".pole"));
      const auto weight = fieldText(QStringLiteral(".weight"));
      bool valid = false;
      edit.value = weight ? weight->toDouble(&valid) : 0.0;
      if (!index)
        return reject(
            QStringLiteral("Pole number must be a positive whole number"));
      if (!valid || !std::isfinite(edit.value) || edit.value <= 0.0)
        return reject(QStringLiteral("Weight must be greater than zero"));
      edit.index = *index;
    }
    if (*kind == LocalBSplineEditKind::DecreaseDegree ||
        *kind == LocalBSplineEditKind::DecreaseKnotMultiplicity) {
      const auto deviation = fieldText(QStringLiteral(".maximum-deviation"));
      auto parsed = deviation ? parseDisplayedLengthMetres(
                                    *deviation, snapshot_.projectLengthUnitId)
                              : std::nullopt;
      if (!parsed || *parsed < 0.0)
        return reject(
            QStringLiteral("Maximum deviation needs a non-negative length"));
      edit.maximumDeviationMetres = *parsed;
    }

    const QString command = snapshot_.activeCommandId;
    const QString label = snapshot_.inspectorTitle;
    const bool queued = localSketchSession_->editBSpline(
        std::move(edit), [this, command](Result<LocalSketchProjection> result) {
          completeLocalOperation(command, std::move(result));
        });
    snapshot_.commandDraft.state =
        queued ? CommandDraftState::Pending : CommandDraftState::Rejected;
    if (queued)
      setLocalOperationPending();
    snapshot_.inspectorStatus =
        queued ? QStringLiteral("Applying %1").arg(label)
               : QStringLiteral("Finish the current operation first");
    ++snapshot_.generation;
    return queued;
  }

  bool submitLocalSketchTransform() {
    const auto commandKind =
        localSketchTransformCommand(snapshot_.activeCommandId);
    const auto reject = [this](QString guidance) {
      snapshot_.commandDraft.state = CommandDraftState::Rejected;
      snapshot_.inspectorStatus = std::move(guidance);
      ++snapshot_.generation;
      return false;
    };
    if (!localSketchSession_ || !commandKind || sketchInputs_.empty() ||
        sketchInputs_.size() > 1024U ||
        std::ranges::any_of(
            sketchInputs_,
            [](const auto &input) { return !input.subElementKey.isEmpty(); }) ||
        snapshot_.commandDraft.state == CommandDraftState::Pending)
      return reject(QStringLiteral("Choose one or more whole Sketch objects"));

    const auto lengthField = [this](QStringView suffix) {
      const auto text = activeFieldText(suffix);
      return text ? parseDisplayedLengthMetres(*text,
                                               snapshot_.projectLengthUnitId)
                  : std::optional<double>{};
    };
    const auto angleField = [this](QStringView suffix) {
      const auto text = activeFieldText(suffix);
      return text ? parseDisplayedAngleRadians(*text) : std::optional<double>{};
    };
    const auto requirePoint =
        [&](QStringView xSuffix, QStringView ySuffix,
            QString label) -> std::optional<std::pair<double, double>> {
      const auto x = lengthField(xSuffix);
      const auto y = lengthField(ySuffix);
      if (!x || !y) {
        reject(QStringLiteral("%1 needs two valid lengths").arg(label));
        return std::nullopt;
      }
      return std::pair{*x, *y};
    };

    LocalSketchTransform transform;
    transform.entityIds.reserve(sketchInputs_.size());
    for (const SketchInputRequest &input : sketchInputs_)
      transform.entityIds.push_back(input.entityId);
    transform.dimensions = activeChoice(QStringLiteral(".dimensions")) ==
                                   QStringLiteral("equalize")
                               ? LocalDimensionCopyPolicy::Equalize
                               : LocalDimensionCopyPolicy::Preserve;
    transform.externalConstraints =
        activeChoice(QStringLiteral(".external-constraints")) ==
                QStringLiteral("detach")
            ? LocalExternalConstraintPolicy::Detach
            : LocalExternalConstraintPolicy::Refuse;

    if (*commandKind == LocalSketchTransformCommand::Translate) {
      const auto first =
          requirePoint(QStringLiteral(".first-x"), QStringLiteral(".first-y"),
                       QStringLiteral("Translation"));
      if (!first)
        return false;
      if (std::hypot(first->first, first->second) <= 1.0e-12)
        return reject(QStringLiteral("Translation must not be zero"));
      if (activeChoice(QStringLiteral(".mode")) == QStringLiteral("move")) {
        transform.mode = LocalSketchTransformMode::Replace;
        transform.transforms.push_back(
            {0.0, 0.0, first->first, first->second, 0.0, 1.0, false});
      } else {
        const auto copies =
            activeWholeNumber(QStringLiteral(".copies"), 1U, 4096U);
        const auto rows = activeWholeNumber(QStringLiteral(".rows"), 1U, 4096U);
        const auto second = requirePoint(QStringLiteral(".second-x"),
                                         QStringLiteral(".second-y"),
                                         QStringLiteral("Second direction"));
        if (!copies || !rows || !second)
          return reject(QStringLiteral(
              "Array copies and rows must be whole numbers from 1 to 4096"));
        const bool secondIsZero =
            std::hypot(second->first, second->second) <= 1.0e-12;
        if ((*rows == 1U && !secondIsZero) || (*rows > 1U && secondIsZero))
          return reject(
              *rows == 1U
                  ? QStringLiteral(
                        "Use zero for the second direction with one row")
                  : QStringLiteral(
                        "Multiple rows need a nonzero second direction"));
        if (*rows > (4097U / (*copies + 1U)) ||
            *rows * (*copies + 1U) - 1U > 4096U)
          return reject(QStringLiteral("Array exceeds 4096 copied instances"));
        transform.mode = LocalSketchTransformMode::Copy;
        transform.transforms.reserve(*rows * (*copies + 1U) - 1U);
        for (std::size_t row = 0U; row < *rows; ++row) {
          for (std::size_t copy = 0U; copy <= *copies; ++copy) {
            if (row == 0U && copy == 0U)
              continue;
            transform.transforms.push_back(
                {0.0, 0.0,
                 first->first * static_cast<double>(copy) +
                     second->first * static_cast<double>(row),
                 first->second * static_cast<double>(copy) +
                     second->second * static_cast<double>(row),
                 0.0, 1.0, false});
          }
        }
      }
    } else if (*commandKind == LocalSketchTransformCommand::Rotate) {
      const auto center =
          requirePoint(QStringLiteral(".center-x"), QStringLiteral(".center-y"),
                       QStringLiteral("Rotation center"));
      const auto angle = angleField(QStringLiteral(".angle"));
      if (!center || !angle || std::abs(*angle) <= 1.0e-12 ||
          std::abs(*angle) >= 2.0 * std::numbers::pi - 1.0e-12)
        return reject(QStringLiteral(
            "Total angle must be nonzero and less than 360 degrees"));
      if (activeChoice(QStringLiteral(".mode")) == QStringLiteral("move")) {
        transform.mode = LocalSketchTransformMode::Replace;
        transform.transforms.push_back(
            {center->first, center->second, 0.0, 0.0, *angle, 1.0, false});
      } else {
        const auto copies =
            activeWholeNumber(QStringLiteral(".copies"), 1U, 4096U);
        if (!copies)
          return reject(
              QStringLiteral("Copies must be a whole number from 1 to 4096"));
        transform.mode = LocalSketchTransformMode::Copy;
        transform.transforms.reserve(*copies);
        for (std::size_t copy = 1U; copy <= *copies; ++copy)
          transform.transforms.push_back({center->first, center->second, 0.0,
                                          0.0,
                                          *angle * static_cast<double>(copy) /
                                              static_cast<double>(*copies),
                                          1.0, false});
      }
    } else if (*commandKind == LocalSketchTransformCommand::Scale) {
      const auto center =
          requirePoint(QStringLiteral(".center-x"), QStringLiteral(".center-y"),
                       QStringLiteral("Scale center"));
      const auto factorText = activeFieldText(QStringLiteral(".factor"));
      bool valid = false;
      const double factor = factorText ? factorText->toDouble(&valid) : 0.0;
      if (!center || !valid || !std::isfinite(factor) || factor <= 1.0e-7 ||
          factor > 1.0e6 || std::abs(factor - 1.0) <= 1.0e-12)
        return reject(
            QStringLiteral("Scale factor must be greater than 0.0000001, no "
                           "greater than 1000000, and not 1"));
      transform.mode =
          activeChoice(QStringLiteral(".mode")) == QStringLiteral("copy")
              ? LocalSketchTransformMode::Copy
              : LocalSketchTransformMode::Replace;
      transform.transforms.push_back(
          {center->first, center->second, 0.0, 0.0, 0.0, factor, false});
    } else {
      const auto axis =
          requirePoint(QStringLiteral(".axis-x"), QStringLiteral(".axis-y"),
                       QStringLiteral("Axis point"));
      const auto angle = angleField(QStringLiteral(".axis-angle"));
      if (!axis || !angle)
        return reject(QStringLiteral("Mirror axis needs a point and angle"));
      transform.mode =
          activeChoice(QStringLiteral(".mode")) == QStringLiteral("replace")
              ? LocalSketchTransformMode::Replace
              : LocalSketchTransformMode::Copy;
      transform.transforms.push_back(
          {axis->first, axis->second, 0.0, 0.0, 2.0 * *angle, 1.0, true});
    }

    const QString command = snapshot_.activeCommandId;
    const QString label = snapshot_.inspectorTitle;
    const bool queued = localSketchSession_->transform(
        std::move(transform),
        [this, command](Result<LocalSketchProjection> result) {
          completeLocalOperation(command, std::move(result));
        });
    snapshot_.commandDraft.state =
        queued ? CommandDraftState::Pending : CommandDraftState::Rejected;
    if (queued)
      setLocalOperationPending();
    snapshot_.inspectorStatus =
        queued ? QStringLiteral("Applying %1").arg(label)
               : QStringLiteral("Finish the current operation first");
    ++snapshot_.generation;
    return queued;
  }

  bool submitLocalSketchCurveModify() {
    const auto command =
        localSketchCurveModifyCommand(snapshot_.activeCommandId);
    const auto reject = [this](QString guidance) {
      snapshot_.commandDraft.state = CommandDraftState::Rejected;
      snapshot_.inspectorStatus = std::move(guidance);
      ++snapshot_.generation;
      return false;
    };
    if (!localSketchSession_ || !command || sketchInputs_.empty() ||
        sketchInputs_.size() > 1024U ||
        snapshot_.commandDraft.state == CommandDraftState::Pending ||
        (*command != LocalSketchCurveModifyCommand::Join &&
         std::ranges::any_of(sketchInputs_, [](const auto &input) {
           return !input.subElementKey.isEmpty();
         })))
      return reject(QStringLiteral("Choose whole Sketch curves"));

    const auto constraintPolicy =
        activeChoice(QStringLiteral(".external-constraints")) ==
                QStringLiteral("detach")
            ? LocalExternalConstraintPolicy::Detach
            : LocalExternalConstraintPolicy::Refuse;
    const QString commandId = snapshot_.activeCommandId;
    const QString label = snapshot_.inspectorTitle;
    bool queued = false;
    if (*command == LocalSketchCurveModifyCommand::Offset) {
      const auto text = activeFieldText(QStringLiteral(".distance"));
      const auto distance =
          text
              ? parseDisplayedLengthMetres(*text, snapshot_.projectLengthUnitId)
              : std::optional<double>{};
      if (!distance || std::abs(*distance) <= 1.0e-12)
        return reject(QStringLiteral("Offset distance must not be zero"));
      LocalOffsetEdit edit;
      edit.distanceMetres = *distance;
      edit.sourceMode =
          activeChoice(QStringLiteral(".source")) == QStringLiteral("delete")
              ? LocalOffsetSourceMode::Delete
              : LocalOffsetSourceMode::Keep;
      edit.constraints = constraintPolicy;
      edit.entityIds.reserve(sketchInputs_.size());
      for (const SketchInputRequest &input : sketchInputs_)
        edit.entityIds.push_back(input.entityId);
      queued = localSketchSession_->offset(
          std::move(edit),
          [this, commandId](Result<LocalSketchProjection> result) {
            completeLocalOperation(commandId, std::move(result));
          });
    } else if (*command == LocalSketchCurveModifyCommand::Trim) {
      if (sketchInputs_.size() != 1U)
        return reject(QStringLiteral("Choose one curve segment"));
      localSketchSession_->cancelPreview();
      LocalTrimEdit edit;
      edit.curve = {sketchInputs_.front().entityId,
                    sketchInputs_.front().planePoint.xMetres,
                    sketchInputs_.front().planePoint.yMetres};
      edit.constraints = constraintPolicy;
      queued = localSketchSession_->trim(
          std::move(edit),
          [this, commandId](Result<LocalSketchProjection> result) {
            completeLocalOperation(commandId, std::move(result));
          });
    } else if (*command == LocalSketchCurveModifyCommand::Split) {
      if (sketchInputs_.size() != 1U)
        return reject(QStringLiteral("Choose one location on a curve"));
      localSketchSession_->cancelPreview();
      LocalSplitEdit edit;
      edit.curve = {sketchInputs_.front().entityId,
                    sketchInputs_.front().planePoint.xMetres,
                    sketchInputs_.front().planePoint.yMetres};
      edit.constraints = constraintPolicy;
      queued = localSketchSession_->split(
          std::move(edit),
          [this, commandId](Result<LocalSketchProjection> result) {
            completeLocalOperation(commandId, std::move(result));
          });
    } else if (*command == LocalSketchCurveModifyCommand::Join) {
      if (sketchInputs_.size() != 1U ||
          sketchInputs_[0].subElementKey.isEmpty())
        return reject(QStringLiteral("Choose one shared curve endpoint"));
      LocalJoinEdit edit;
      edit.first = {sketchInputs_[0].entityId,
                    sketchInputs_[0].subElementKey};
      edit.constraints = constraintPolicy;
      queued = localSketchSession_->join(
          std::move(edit),
          [this, commandId](Result<LocalSketchProjection> result) {
            completeLocalOperation(commandId, std::move(result));
          });
    } else if (*command == LocalSketchCurveModifyCommand::ConvertToNurbs) {
      if (sketchInputs_.size() != 1U)
        return reject(QStringLiteral("Choose one analytic curve"));
      queued = localSketchSession_->convertToNurbs(
          {sketchInputs_.front().entityId, constraintPolicy},
          [this, commandId](Result<LocalSketchProjection> result) {
            completeLocalOperation(commandId, std::move(result));
          });
    } else if (*command == LocalSketchCurveModifyCommand::Extend) {
      if (sketchInputs_.size() != 2U ||
          sketchInputs_[0].kind != SketchInputKind::Entity ||
          sketchInputs_[1].kind != SketchInputKind::PlanePoint)
        return reject(
            QStringLiteral("Choose a curve endpoint, then its target"));
      const auto primitive = std::ranges::find_if(
          snapshot_.sketchProjection.primitives,
          [this](const SketchPrimitiveProjection &candidate) {
            return !candidate.draft &&
                   candidate.id == sketchInputs_[0].entityId;
          });
      if (primitive == snapshot_.sketchProjection.primitives.end() ||
          (primitive->kind != SketchPrimitiveKind::Line &&
           primitive->kind != SketchPrimitiveKind::Arc))
        return reject(
            QStringLiteral("Extend currently needs a line or circular arc"));
      LocalExtendEdit edit;
      edit.curve = {sketchInputs_[0].entityId,
                    sketchInputs_[0].planePoint.xMetres,
                    sketchInputs_[0].planePoint.yMetres};
      edit.targetXMetres = sketchInputs_[1].planePoint.xMetres;
      edit.targetYMetres = sketchInputs_[1].planePoint.yMetres;
      edit.constraints = constraintPolicy;
      queued = localSketchSession_->extend(
          std::move(edit),
          [this, commandId](Result<LocalSketchProjection> result) {
            completeLocalOperation(commandId, std::move(result));
          });
    } else {
      if (sketchInputs_.size() != 2U)
        return reject(QStringLiteral("Choose exactly two lines"));
      const bool lines = std::ranges::all_of(
          sketchInputs_, [this](const SketchInputRequest &input) {
            const auto primitive = std::ranges::find_if(
                snapshot_.sketchProjection.primitives,
                [&input](const SketchPrimitiveProjection &candidate) {
                  return !candidate.draft && candidate.id == input.entityId;
                });
            return primitive != snapshot_.sketchProjection.primitives.end() &&
                   primitive->kind == SketchPrimitiveKind::Line;
          });
      if (!lines)
        return reject(
            QStringLiteral("Fillet and Chamfer currently need two lines"));
      const auto text = activeFieldText(QStringLiteral(".size"));
      const auto size =
          text
              ? parseDisplayedLengthMetres(*text, snapshot_.projectLengthUnitId)
              : std::optional<double>{};
      if (!size || *size <= 1.0e-12)
        return reject(QStringLiteral("Corner size must be greater than zero"));
      const auto pick = [](const SketchInputRequest &input) {
        return LocalCurvePick{input.entityId, input.planePoint.xMetres,
                              input.planePoint.yMetres};
      };
      LocalCornerEdit edit;
      edit.kind = *command == LocalSketchCurveModifyCommand::Fillet
                      ? LocalCornerEditKind::Fillet
                      : LocalCornerEditKind::Chamfer;
      edit.first = pick(sketchInputs_[0]);
      edit.second = pick(sketchInputs_[1]);
      edit.sizeMetres = *size;
      edit.constraints = constraintPolicy;
      queued = localSketchSession_->modifyCorner(
          std::move(edit),
          [this, commandId](Result<LocalSketchProjection> result) {
            completeLocalOperation(commandId, std::move(result));
          });
    }
    snapshot_.commandDraft.state =
        queued ? CommandDraftState::Pending : CommandDraftState::Rejected;
    if (queued)
      setLocalOperationPending();
    snapshot_.inspectorStatus =
        queued ? QStringLiteral("Applying %1").arg(label)
               : QStringLiteral("Finish the current operation first");
    ++snapshot_.generation;
    return queued;
  }

  bool submitLocalSketchDimension() {
    const auto reject = [this](QString guidance) {
      snapshot_.commandDraft.state = CommandDraftState::Rejected;
      snapshot_.inspectorStatus = std::move(guidance);
      ++snapshot_.generation;
      return false;
    };
    if (!localSketchSession_ || sketchInputs_.empty() ||
        sketchInputs_.size() > 2U ||
        snapshot_.commandDraft.state == CommandDraftState::Pending)
      return reject(QStringLiteral("Choose dimension geometry first"));
    const auto primitiveFor = [this](const SketchInputRequest &input) {
      return std::ranges::find_if(
          snapshot_.sketchProjection.primitives,
          [&input](const SketchPrimitiveProjection &primitive) {
            return !primitive.draft && primitive.id == input.entityId;
          });
    };
    const QString requested = activeChoice(QStringLiteral(".kind"));
    const bool twoPoints =
        sketchInputs_.size() == 2U &&
        std::ranges::all_of(sketchInputs_, [](const SketchInputRequest &input) {
          return !input.subElementKey.isEmpty();
        });
    const bool twoLines =
        sketchInputs_.size() == 2U &&
        std::ranges::all_of(
            sketchInputs_, [&](const SketchInputRequest &input) {
              const auto primitive = primitiveFor(input);
              return input.subElementKey.isEmpty() &&
                     primitive != snapshot_.sketchProjection.primitives.end() &&
                     primitive->kind == SketchPrimitiveKind::Line;
            });
    const auto firstPrimitive = primitiveFor(sketchInputs_.front());
    if (firstPrimitive == snapshot_.sketchProjection.primitives.end())
      return reject(
          QStringLiteral("The selected geometry is no longer current"));
    LocalSketchConstraintKind kind;
    if (requested == QStringLiteral("automatic")) {
      if (twoPoints)
        kind = LocalSketchConstraintKind::Distance;
      else if (twoLines)
        kind = LocalSketchConstraintKind::Angle;
      else if (sketchInputs_.size() == 1U &&
               sketchInputs_.front().subElementKey.isEmpty() &&
               firstPrimitive->kind == SketchPrimitiveKind::Line)
        kind = LocalSketchConstraintKind::Distance;
      else if (sketchInputs_.size() == 1U &&
               sketchInputs_.front().subElementKey.isEmpty() &&
               (firstPrimitive->kind == SketchPrimitiveKind::Circle ||
                firstPrimitive->kind == SketchPrimitiveKind::Arc))
        kind = LocalSketchConstraintKind::Radius;
      else
        return reject(QStringLiteral(
            "Automatic dimension needs a line, radial curve, two points, or "
            "two lines"));
    } else if (requested == QStringLiteral("distance")) {
      kind = LocalSketchConstraintKind::Distance;
    } else if (requested == QStringLiteral("horizontal-distance")) {
      kind = LocalSketchConstraintKind::HorizontalDistance;
    } else if (requested == QStringLiteral("vertical-distance")) {
      kind = LocalSketchConstraintKind::VerticalDistance;
    } else if (requested == QStringLiteral("radius")) {
      kind = LocalSketchConstraintKind::Radius;
    } else if (requested == QStringLiteral("diameter")) {
      kind = LocalSketchConstraintKind::Diameter;
    } else if (requested == QStringLiteral("angle")) {
      kind = LocalSketchConstraintKind::Angle;
    } else {
      return reject(QStringLiteral("Choose a supported dimension type"));
    }
    const bool validSelection =
        (kind == LocalSketchConstraintKind::Distance &&
         (twoPoints || (sketchInputs_.size() == 1U &&
                        sketchInputs_.front().subElementKey.isEmpty() &&
                        firstPrimitive->kind == SketchPrimitiveKind::Line))) ||
        ((kind == LocalSketchConstraintKind::HorizontalDistance ||
          kind == LocalSketchConstraintKind::VerticalDistance) &&
         twoPoints) ||
        ((kind == LocalSketchConstraintKind::Radius ||
          kind == LocalSketchConstraintKind::Diameter) &&
         sketchInputs_.size() == 1U &&
         sketchInputs_.front().subElementKey.isEmpty() &&
         (firstPrimitive->kind == SketchPrimitiveKind::Circle ||
          firstPrimitive->kind == SketchPrimitiveKind::Arc)) ||
        (kind == LocalSketchConstraintKind::Angle && twoLines);
    if (!validSelection)
      return reject(QStringLiteral(
          "The selected geometry does not match this dimension"));
    const QString valueText = activeChoice(QStringLiteral(".expression"));
    const std::optional<double> value =
        kind == LocalSketchConstraintKind::Angle
            ? parseDisplayedAngleRadians(valueText)
            : parseDisplayedLengthMetres(valueText,
                                         snapshot_.projectLengthUnitId);
    if (!value)
      return reject(QStringLiteral("Enter a number with compatible units"));
    LocalSketchConstraintGesture gesture{kind, {}, *value};
    gesture.selections.reserve(sketchInputs_.size());
    for (const SketchInputRequest &input : sketchInputs_)
      gesture.selections.push_back({input.entityId, input.subElementKey});
    const bool queued = localSketchSession_->applyConstraint(
        std::move(gesture), [this](Result<LocalSketchProjection> result) {
          completeLocalOperation(QStringLiteral("sketch.dimension"),
                                 std::move(result));
        });
    snapshot_.commandDraft.state =
        queued ? CommandDraftState::Pending : CommandDraftState::Rejected;
    if (queued)
      setLocalOperationPending();
    snapshot_.inspectorStatus =
        queued ? QStringLiteral("Applying driving dimension")
               : QStringLiteral("Check the selected geometry and value");
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
               : QStringLiteral("Finish the current operation first");
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
          QStringLiteral("Finish the current operation first");
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
    snapshot_.revisionLabel = QStringLiteral("Unsaved changes");
    snapshot_.modelSource = projection.source;
    snapshot_.sourceEditingAvailable = true;
    historyCanUndo_ = projection.canUndo;
    historyCanRedo_ = projection.canRedo;
    setLocalHistoryAvailability(historyCanUndo_, historyCanRedo_);
    localCommittedSketchScene_ = projection.scene;
    snapshot_.sketchScene = localCommittedSketchScene_;
    localSketchObjects_ = projection.objects;
    localSketchConstraints_ = projection.constraints;
    localSketchProfileCount_ = projection.profileCount;
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
         QStringLiteral("sketch")},
    };
    std::unordered_set<SketchEntityId, TypedIdHash<SketchEntityIdTag>> owned;
    for (const sketch::SketchObject &object : localSketchObjects_) {
      const auto presentation = sketchObjectPresentation(object.kind);
      snapshot_.structure.push_back(
          {QString::fromStdString(object.id.toString()),
           QString::fromStdString(object.label), 3,
           QLatin1StringView{presentation.structureKind}});
      for (const sketch::SketchObjectMember &member : object.members) {
        owned.insert(member.entity);
        if (object.members.size() > 1U) {
          QString memberKind =
              sketchMemberStructureKind(object.kind, member.role);
          if (object.kind == sketch::SketchObjectKind::CurveGroup &&
              projection.scene) {
            if (const auto *primitive =
                    projection.scene->findPrimitive(member.entity))
              memberKind = QLatin1StringView{
                  sketchPrimitivePresentation(primitive->kind).structureKind};
          }
          snapshot_.structure.push_back(
              {QString::fromStdString(member.entity.toString()),
               sketchMemberLabel(member.role), 4,
               std::move(memberKind)});
        }
      }
    }
    std::array<std::size_t,
               static_cast<std::size_t>(render::SketchPrimitiveKind::BSpline)>
        geometryCounts{};
    if (projection.scene) {
      for (const render::PackedSketchPrimitive &primitive :
           projection.scene->primitives()) {
        if (owned.contains(primitive.entity))
          continue;
        const std::size_t kindIndex =
            static_cast<std::size_t>(primitive.kind) - 1U;
        const std::size_t index = ++geometryCounts.at(kindIndex);
        const auto presentation = sketchPrimitivePresentation(primitive.kind);
        snapshot_.structure.push_back(
            {QString::fromStdString(primitive.entity.toString()),
             sketchPrimitiveName(primitive.kind, index), 3,
             QLatin1StringView{presentation.structureKind}});
      }
    }
    if (!localSketchConstraints_.empty()) {
      snapshot_.structure.push_back({QStringLiteral("sketch.constraints"),
                                     QStringLiteral("Constraints"), 3,
                                     QStringLiteral("group")});
      std::map<QString, std::size_t> counts;
      for (const sketch::Constraint &constraint : localSketchConstraints_) {
        const QString type = sketchConstraintType(constraint);
        const std::size_t index = ++counts[type];
        snapshot_.structure.push_back(
            {QString::fromStdString(
                 sketch::constraintId(constraint).toString()),
             QStringLiteral("%1 %2").arg(type).arg(index), 4,
             QStringLiteral("sketch-constraint")});
      }
    }
    if (!commandId.startsWith(QStringLiteral("version."))) {
      const QString revisionName = [&] {
        if (commandId == QStringLiteral("model.sketch.create"))
          return QStringLiteral("Create Sketch");
        if (const auto tool = localSketchToolKind(commandId))
          return localSketchToolLabel(*tool);
        if (const LocalSketchConstraintDefinition *constraint =
                localSketchConstraintDefinition(commandId))
          return QString::fromLatin1(
              constraint->label.data(),
              static_cast<qsizetype>(constraint->label.size()));
        if (localSketchCurveModifyCommand(commandId))
          return commandLabel(sketchCommandRecords(), commandId);
        if (commandId == QStringLiteral("sketch.dimension"))
          return QStringLiteral("Dimension");
        if (commandId == QStringLiteral("sketch.curve.drag"))
          return QStringLiteral("Resize Sketch geometry");
        if (commandId == QStringLiteral("sketch.construction.toggle"))
          return QStringLiteral("Toggle construction geometry");
        return QStringLiteral("Source edit");
      }();
      snapshot_.revisions.insert(snapshot_.revisions.begin(),
                                 {projection.projectRevision, revisionName,
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
    snapshot_.selectedEntityId.clear();
    snapshot_.selectedSketchScopes.clear();
    snapshot_.sketchProjection.primitives = baseSketchPrimitives();
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
      snapshot_.sketchScene = localCommittedSketchScene_;
      if (localSketchToolKind(commandId) ||
          commandId == QStringLiteral("sketch.trim") ||
          commandId == QStringLiteral("sketch.split") ||
          commandId == QStringLiteral("sketch.join") ||
          commandId ==
              QStringLiteral("sketch.bspline.convert-to-nurbs")) {
        sketchInputs_.clear();
        rebuildSketchProjection();
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
      snapshot_.sketchEditing = true;
      snapshot_.selectedEntityId = localSketchFunction_.id;
      refreshContextCommands();
      snapshot_.activeCommandId.clear();
      snapshot_.fields.clear();
      snapshot_.commandDraft = {};
      clearSketchInteraction();
      snapshot_.inspectorTitle = QStringLiteral("Sketch");
      snapshot_.inspectorStatus =
          QStringLiteral("Sketch created · choose a geometry tool");
    } else if (const auto tool = localSketchToolKind(
                   commandId, activeChoice(QStringLiteral(".method")))) {
      sketchInputs_.clear();
      snapshot_.sketchProjection.primitives.clear();
      snapshot_.activeCommandId.clear();
      snapshot_.fields.clear();
      snapshot_.commandDraft = {};
      clearSketchInteraction();
      snapshot_.inspectorTitle = QStringLiteral("Sketch");
      snapshot_.inspectorStatus = QStringLiteral("%1 created · Select active")
                                      .arg(localSketchToolLabel(*tool));
    } else if (const LocalSketchConstraintDefinition *constraint =
                   localSketchConstraintDefinition(commandId)) {
      sketchInputs_.clear();
      snapshot_.activeCommandId.clear();
      snapshot_.fields.clear();
      snapshot_.commandDraft = {};
      clearSketchInteraction();
      snapshot_.inspectorTitle = QStringLiteral("Sketch");
      snapshot_.inspectorStatus =
          QStringLiteral("%1 applied · Select active")
              .arg(QString::fromLatin1(
                  constraint->label.data(),
                  static_cast<qsizetype>(constraint->label.size())));
    } else if (localBSplineEditKind(commandId)) {
      sketchInputs_.clear();
      snapshot_.activeCommandId.clear();
      snapshot_.fields.clear();
      snapshot_.commandDraft = {};
      clearSketchInteraction();
      snapshot_.inspectorTitle = QStringLiteral("Sketch");
      snapshot_.inspectorStatus =
          QStringLiteral("B-spline updated · Select active");
    } else if (commandId == QStringLiteral("sketch.trim") ||
               commandId == QStringLiteral("sketch.split") ||
               commandId == QStringLiteral("sketch.join") ||
               commandId ==
                   QStringLiteral("sketch.bspline.convert-to-nurbs")) {
      sketchInputs_.clear();
      snapshot_.sketchInteraction.expectedRevision = snapshot_.projectRevision;
      snapshot_.sketchInteraction.inputKind = SketchInputKind::Entity;
      snapshot_.sketchInteraction.inputCount = 0;
      snapshot_.commandDraft.baseRevision = snapshot_.projectRevision;
      snapshot_.commandDraft.state = CommandDraftState::Editing;
      snapshot_.commandDraft.previewSupported = false;
      snapshot_.commandDraft.applySupported = false;
      rebuildSketchProjection();
      const bool trim = commandId == QStringLiteral("sketch.trim");
      const bool split = commandId == QStringLiteral("sketch.split");
      const bool join = commandId == QStringLiteral("sketch.join");
      snapshot_.inspectorTitle =
          trim    ? QStringLiteral("Trim")
          : split ? QStringLiteral("Split")
          : join  ? QStringLiteral("Join")
                  : QStringLiteral("Convert to NURBS");
      snapshot_.inspectorStatus = trim
                                      ? QStringLiteral(
                                            "Trim complete · choose another "
                                            "curve segment")
                                  : split
                                      ? QStringLiteral(
                                            "Split complete · choose another "
                                            "curve location")
                                  : join
                                      ? QStringLiteral(
                                            "Join complete · choose another "
                                            "shared endpoint")
                                      : QStringLiteral(
                                            "Converted · choose another "
                                            "analytic curve");
    } else if (localSketchTransformCommand(commandId) ||
               localSketchCurveModifyCommand(commandId)) {
      sketchInputs_.clear();
      snapshot_.activeCommandId.clear();
      snapshot_.fields.clear();
      snapshot_.commandDraft = {};
      clearSketchInteraction();
      snapshot_.inspectorTitle = QStringLiteral("Sketch");
      snapshot_.inspectorStatus =
          QStringLiteral("%1 complete · Select active")
              .arg(commandLabel(sketchCommandRecords(), commandId));
    } else if (commandId == QStringLiteral("sketch.dimension")) {
      sketchInputs_.clear();
      snapshot_.activeCommandId.clear();
      snapshot_.fields.clear();
      snapshot_.commandDraft = {};
      clearSketchInteraction();
      snapshot_.inspectorTitle = QStringLiteral("Sketch");
      snapshot_.inspectorStatus =
          QStringLiteral("Driving dimension applied · Select active");
    } else if (commandId == QStringLiteral("sketch.curve.drag") ||
               commandId == QStringLiteral("sketch.construction.toggle")) {
      snapshot_.activeCommandId.clear();
      snapshot_.fields.clear();
      snapshot_.commandDraft = {};
      clearSketchInteraction();
      snapshot_.selectedEntityId = localEditEntity_;
      projectHumanSelection(localEditEntity_, {});
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

  bool activeSketchToggle(const QString &suffix) const {
    const auto field = std::ranges::find_if(
        snapshot_.fields, [&suffix](const FieldDescriptor &candidate) {
          return candidate.id.endsWith(suffix);
        });
    return field != snapshot_.fields.end() &&
           std::holds_alternative<bool>(field->value) &&
           std::get<bool>(field->value);
  }

  bool activeSketchConstruction() const {
    return activeSketchToggle(QStringLiteral(".construction"));
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

  std::optional<QString> activeFieldText(QStringView suffix) const {
    const auto field = std::ranges::find_if(
        snapshot_.fields, [suffix](const FieldDescriptor &candidate) {
          return candidate.id.endsWith(suffix);
        });
    if (field == snapshot_.fields.end() ||
        !std::holds_alternative<QString>(field->value))
      return std::nullopt;
    return std::get<QString>(field->value).trimmed();
  }

  std::optional<std::size_t> activeWholeNumber(QStringView suffix,
                                               std::size_t minimum,
                                               std::size_t maximum) const {
    const auto text = activeFieldText(suffix);
    if (!text)
      return std::nullopt;
    bool valid = false;
    const qulonglong value = text->toULongLong(&valid);
    if (!valid || value < minimum || value > maximum)
      return std::nullopt;
    return static_cast<std::size_t>(value);
  }

  std::size_t activeSketchSideCount() const {
    const auto field = std::ranges::find_if(
        snapshot_.fields, [](const FieldDescriptor &candidate) {
          return candidate.id.endsWith(QStringLiteral(".sides"));
        });
    if (field == snapshot_.fields.end() ||
        !std::holds_alternative<QString>(field->value))
      return 0U;
    bool valid = false;
    const qulonglong count =
        std::get<QString>(field->value).toULongLong(&valid);
    return valid && count <= std::numeric_limits<std::size_t>::max()
               ? static_cast<std::size_t>(count)
               : 0U;
  }

  std::uint32_t activeSketchDegree() const {
    const auto field = std::ranges::find_if(
        snapshot_.fields, [](const FieldDescriptor &candidate) {
          return candidate.id.endsWith(QStringLiteral(".degree"));
        });
    if (field == snapshot_.fields.end() ||
        !std::holds_alternative<QString>(field->value))
      return 3U;
    bool valid = false;
    const uint degree = std::get<QString>(field->value).toUInt(&valid);
    return valid && degree >= 1U && degree <= 25U
               ? static_cast<std::uint32_t>(degree)
               : 0U;
  }

  void beginSketchInteraction(const CommandForm &form) {
    sketchInputs_.clear();
    std::vector<SketchInputKind> inputSequence{form.sketchInputKind};
    if (snapshot_.activeCommandId == QStringLiteral("sketch.extend"))
      inputSequence = {SketchInputKind::Entity, SketchInputKind::PlanePoint};
    snapshot_.sketchInteraction = {
        snapshot_.activeCommandId,
        snapshot_.projectRevision,
        form.sketchInputKind,
        form.minimumSketchInputs,
        form.maximumSketchInputs,
        0,
        form.guidance,
        form.sketchSelectionSequence,
        std::move(inputSequence),
    };
    rebuildSketchProjection();
    updateSketchInteractionRule();
    updateSketchReadiness();
  }

  void clearSketchInteraction() {
    sketchInputs_.clear();
    snapshot_.sketchInputPlanePoints.clear();
    snapshot_.sketchInteraction = {};
    snapshot_.sketchProjection.primitives = baseSketchPrimitives();
    if (snapshot_.activeWorkspaceId == QStringLiteral("sketch"))
      snapshot_.selectionSummary = QStringLiteral("Nothing selected");
  }

  void updateSketchInteractionRule() {
    if (snapshot_.sketchInteraction.inputKind != SketchInputKind::PlanePoint)
      return;
    if (localMode_) {
      const QString method = activeChoice(QStringLiteral(".method"));
      if (const LocalSketchToolDefinition *definition =
              localSketchToolDefinition(snapshot_.activeCommandId, method)) {
        snapshot_.sketchInteraction.minimumInputCount =
            static_cast<int>(definition->minimumInputPointCount);
        snapshot_.sketchInteraction.maximumInputCount =
            static_cast<int>(definition->maximumInputPointCount);
      }
    } else if (snapshot_.activeCommandId ==
                   QStringLiteral("sketch.rectangle") ||
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
    snapshot_.sketchInputPlanePoints.clear();
    snapshot_.sketchInputPlanePoints.reserve(sketchInputs_.size());
    for (const SketchInputRequest &input : sketchInputs_) {
      if (input.kind == SketchInputKind::PlanePoint)
        snapshot_.sketchInputPlanePoints.push_back(input.planePoint);
    }
    interaction.inputCount = static_cast<int>(sketchInputs_.size());
    if (!interaction.inputSequence.empty())
      interaction.inputKind = interaction.inputSequence[std::min(
          sketchInputs_.size(), interaction.inputSequence.size() - 1U)];
    if (interaction.inputKind == SketchInputKind::None)
      return;
    const bool invalidSplineDegree =
        localMode_ && localSketchToolKind(snapshot_.activeCommandId) &&
        isLocalSketchBSpline(*localSketchToolKind(snapshot_.activeCommandId)) &&
        activeSketchDegree() == 0U;
    const bool ready =
        interaction.inputCount >= interaction.minimumInputCount &&
        !invalidSplineDegree;
    const std::size_t entitySelections =
        static_cast<std::size_t>(std::ranges::count(
            sketchInputs_, SketchInputKind::Entity, &SketchInputRequest::kind));
    if (entitySelections != 0U ||
        interaction.inputKind == SketchInputKind::Entity) {
      snapshot_.selectionSummary =
          entitySelections == 0U ? QStringLiteral("Nothing selected")
          : entitySelections == 1U
              ? QStringLiteral("1 Sketch selection")
              : QStringLiteral("%1 Sketch selections").arg(entitySelections);
    }
    snapshot_.commandDraft.previewSupported =
        ready && !(localMode_ && snapshot_.activeCommandId ==
                                     QStringLiteral("sketch.dimension"));
    snapshot_.commandDraft.applySupported = ready;
    snapshot_.inspectorStatus =
        invalidSplineDegree
            ? QStringLiteral("Degree must be a whole number from 1 to 25")
        : ready
            ? (interaction.maximumInputCount == 0
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

  void appendDraftPoint(const PlanePoint &point, QString id,
                        bool construction) {
    snapshot_.sketchProjection.primitives.push_back({std::move(id),
                                                     SketchPrimitiveKind::Point,
                                                     {point},
                                                     {QStringLiteral("point")},
                                                     {},
                                                     0.0,
                                                     construction,
                                                     false,
                                                     true});
  }

  void rebuildSketchProjection() {
    snapshot_.sketchProjection.primitives = baseSketchPrimitives();
    const bool hasEntityInput =
        std::ranges::any_of(sketchInputs_, [](const SketchInputRequest &input) {
          return input.kind == SketchInputKind::Entity;
        });
    if (hasEntityInput ||
        snapshot_.sketchInteraction.inputKind == SketchInputKind::Entity) {
      for (SketchPrimitiveProjection &primitive :
           snapshot_.sketchProjection.primitives) {
        primitive.selected = std::ranges::any_of(
            sketchInputs_, [&primitive](const SketchInputRequest &input) {
              return input.kind == SketchInputKind::Entity &&
                     input.entityId == primitive.id &&
                     input.subElementKey.isEmpty();
            });
        for (const SketchInputRequest &input : sketchInputs_) {
          if (input.kind == SketchInputKind::Entity &&
              input.entityId == primitive.id && !input.subElementKey.isEmpty())
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
    if (const LocalSketchToolDefinition *definition = localSketchToolDefinition(
            QStringView{command},
            QStringView{activeChoice(QStringLiteral(".method"))})) {
      LocalSketchToolGesture gesture{definition->kind, {}, construction};
      gesture.closed = activeSketchToggle(QStringLiteral(".close-profile"));
      gesture.sideCount = activeSketchSideCount();
      gesture.degree = activeSketchDegree();
      gesture.points.reserve(points.size());
      for (const PlanePoint &point : points)
        gesture.points.push_back({point.xMetres, point.yMetres});
      auto projected = projectLocalSketchToolGesture(gesture);
      if (projected)
        snapshot_.sketchProjection.primitives.insert(
            snapshot_.sketchProjection.primitives.end(),
            std::make_move_iterator(projected->begin()),
            std::make_move_iterator(projected->end()));
      return;
    }
    if (command == QStringLiteral("sketch.polyline")) {
      if (points.size() == 1)
        appendDraftPoint(points.front(), QStringLiteral("draft.anchor"),
                         construction);
      for (std::size_t index = 1; index < points.size(); ++index)
        appendDraftLine(points[index - 1], points[index],
                        static_cast<int>(index - 1), construction);
      return;
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
                ? QStringLiteral("Ready")
            : localBackendState_ == LocalBackendState::Failed
                ? QStringLiteral("Design engine failed")
                : QStringLiteral("Starting the design engine");
        snapshot_.modelHealth =
            localBackendState_ == LocalBackendState::Ready
                ? QStringLiteral("Ready")
            : localBackendState_ == LocalBackendState::Failed
                ? QStringLiteral("Design engine failed")
                : QStringLiteral("Starting");
      }
      return;
    }
    snapshot_.viewportState = QStringLiteral("unavailable");
    if (snapshot_.activeWorkspaceId == QStringLiteral("sketch")) {
      snapshot_.viewportState = QStringLiteral("current");
      if (!snapshot_.sketchEditing) {
        snapshot_.viewportHeadline = QStringLiteral("Sketch workspace");
        snapshot_.viewportDetail =
            hasSelectedSketch()
                ? QStringLiteral("Edit the selected Sketch or create another")
            : localSketchPlaneFromId(snapshot_.selectedEntityId)
                ? QStringLiteral("Create a Sketch on the selected plane")
                : QStringLiteral("Select a plane or create a new Sketch");
        snapshot_.modelHealth =
            localMode_ ? QStringLiteral("Ready")
                       : QStringLiteral("Sketch frontend contract current");
        return;
      }
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

  void refreshWorkspace() {
    refreshContextCommands();
    snapshot_.activeCommandId.clear();
    snapshot_.fields.clear();
    snapshot_.commandDraft = {};
    clearSketchInteraction();
    refreshInspectorContext();
    restoreWorkspaceViewport();
    ++snapshot_.generation;
  }

  FrontendSnapshot snapshot_;
  mutable FrontendSnapshotPtr published_;
  std::vector<SketchInputRequest> sketchInputs_;
  FunctionSummary basePlateFunction_;
  FunctionSummary mountingProfileFunction_;
  FunctionSummary localSketchFunction_;
  std::vector<sketch::SketchObject> localSketchObjects_;
  std::vector<sketch::Constraint> localSketchConstraints_;
  std::size_t localSketchProfileCount_ = 0U;
  enum class LocalBackendState { Starting, Ready, Failed };
  bool localMode_ = false;
  LocalBackendState localBackendState_ = LocalBackendState::Starting;
  bool historyCanUndo_ = false;
  bool historyCanRedo_ = false;
  QString localEditEntity_;
  std::shared_ptr<const render::SketchSceneSnapshot> localCommittedSketchScene_;
  std::unique_ptr<LocalSketchSession> localSketchSession_;
  ChangeHandler changeHandler_;
};

} // namespace

std::unique_ptr<FrontendController> makeCaptureDesktopController(
    std::vector<UiOption> themeOptions, const QString &themeId,
    const QString &defaultLengthUnitId, const QString &interfaceDensityId,
    const QString &navigationProfileId, const QString &zoomDirectionId) {
  return std::make_unique<DesktopController>(
      std::move(themeOptions), themeId, defaultLengthUnitId, interfaceDensityId,
      navigationProfileId, zoomDirectionId);
}

std::unique_ptr<FrontendController> makeDesktopController(
    std::unique_ptr<LocalSketchSession> sketchSession,
    std::vector<UiOption> themeOptions, const QString &themeId,
    const QString &defaultLengthUnitId, const QString &interfaceDensityId,
    const QString &navigationProfileId, const QString &zoomDirectionId) {
  return std::make_unique<DesktopController>(
      std::move(themeOptions), themeId, defaultLengthUnitId, interfaceDensityId,
      navigationProfileId, zoomDirectionId, std::move(sketchSession));
}

} // namespace kearne::ui
