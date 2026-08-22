#pragma once

#include <kearne/base/value.hpp>
#include <kearne/render/sketch_scene.hpp>
#include <kearne/sketch/model.hpp>

#include <QObject>
#include <QProcessEnvironment>
#include <QString>
#include <QStringList>
#include <QStringView>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <variant>
#include <vector>

namespace kearne::ui {

struct LocalSketchSessionConfig {
  QString sourceEditorProgram;
  QStringList sourceEditorArguments;
  QProcessEnvironment sourceEditorEnvironment;
  std::size_t maximumPendingOperations = 4U;
};

enum class LocalSketchPlane : std::uint8_t { XY = 1, XZ = 2, YZ = 3 };

[[nodiscard]] QString localSketchPlaneId(LocalSketchPlane plane);
[[nodiscard]] std::optional<LocalSketchPlane>
localSketchPlaneFromId(QStringView id);

struct LocalSketchCreation {
  LocalSketchPlane plane = LocalSketchPlane::XY;
};

enum class LocalSketchToolKind : std::uint8_t {
  Point = 1,
  Line = 2,
  Circle = 3,
  Arc = 4,
  Rectangle = 5,
  Slot = 6,
  ArcSlot = 7,
  ThreePointArc = 8,
  ThreePointCircle = 9,
  CenterRectangle = 10,
  Polyline = 11,
  Triangle = 12,
  Square = 13,
  Pentagon = 14,
  Hexagon = 15,
  Heptagon = 16,
  Octagon = 17,
  RegularPolygon = 18,
  Oblong = 19,
  Ellipse = 20,
  ThreePointEllipse = 21,
  EllipticalArc = 22,
  HyperbolicArc = 23,
  ParabolicArc = 24,
  BSpline = 25,
  PeriodicBSpline = 26,
  InterpolatedBSpline = 27,
  PeriodicInterpolatedBSpline = 28,
};

struct LocalSketchToolPoint {
  double xMetres = 0.0;
  double yMetres = 0.0;
  bool operator==(const LocalSketchToolPoint &) const = default;
};

struct LocalSketchToolGesture {
  LocalSketchToolKind kind = LocalSketchToolKind::Point;
  std::vector<LocalSketchToolPoint> points;
  bool construction = false;
  bool closed = false;
  std::size_t sideCount = 0U;
  std::uint32_t degree = 3U;
};

enum class LocalSketchConstraintKind : std::uint8_t {
  Coincident = 1,
  Horizontal = 2,
  Vertical = 3,
  Parallel = 4,
  Perpendicular = 5,
  Tangent = 6,
  Equal = 7,
  Concentric = 8,
  Midpoint = 9,
  Block = 10,
  Collinear = 11,
  PointOnObject = 12,
  Symmetric = 13,
  Lock = 14,
  Distance = 15,
  HorizontalDistance = 16,
  VerticalDistance = 17,
  Radius = 18,
  Diameter = 19,
  Angle = 20,
  HorizontalVertical = 21,
  Group = 22,
  RemoveAxisAlignment = 23,
};

struct LocalSketchConstraintSelection {
  QString entityId;
  QString pointKey;
};

struct LocalSketchConstraintGesture {
  LocalSketchConstraintKind kind = LocalSketchConstraintKind::Coincident;
  std::vector<LocalSketchConstraintSelection> selections;
  std::optional<double> valueSi;
};

struct LocalSketchConstructionToggle {
  QString entityId;
};

enum class LocalBSplineEditKind : std::uint8_t {
  IncreaseDegree = 1,
  DecreaseDegree = 2,
  IncreaseKnotMultiplicity = 3,
  DecreaseKnotMultiplicity = 4,
  InsertKnot = 5,
  SetPoleWeight = 6,
};

struct LocalBSplineEdit {
  LocalBSplineEditKind kind = LocalBSplineEditKind::IncreaseDegree;
  QString entityId;
  std::size_t index = 0U;
  double value = 0.0;
  double maximumDeviationMetres = 0.0;
};

enum class LocalSketchTransformMode : std::uint8_t { Replace = 1, Copy = 2 };
enum class LocalDimensionCopyPolicy : std::uint8_t {
  Preserve = 1,
  Equalize = 2,
};
enum class LocalExternalConstraintPolicy : std::uint8_t {
  Refuse = 1,
  Detach = 2,
};

struct LocalSimilarityTransform {
  double pivotXMetres = 0.0;
  double pivotYMetres = 0.0;
  double translationXMetres = 0.0;
  double translationYMetres = 0.0;
  double rotationRadians = 0.0;
  double scale = 1.0;
  bool reflected = false;
};

struct LocalSketchTransform {
  std::vector<QString> entityIds;
  LocalSketchTransformMode mode = LocalSketchTransformMode::Replace;
  std::vector<LocalSimilarityTransform> transforms;
  LocalDimensionCopyPolicy dimensions = LocalDimensionCopyPolicy::Preserve;
  LocalExternalConstraintPolicy externalConstraints =
      LocalExternalConstraintPolicy::Refuse;
};

enum class LocalCornerEditKind : std::uint8_t { Fillet = 1, Chamfer = 2 };

struct LocalCurvePick {
  QString entityId;
  double referenceXMetres = 0.0;
  double referenceYMetres = 0.0;
};

struct LocalCornerEdit {
  LocalCornerEditKind kind = LocalCornerEditKind::Fillet;
  LocalCurvePick first;
  LocalCurvePick second;
  double sizeMetres = 0.0;
  LocalExternalConstraintPolicy constraints =
      LocalExternalConstraintPolicy::Refuse;
};

enum class LocalOffsetSourceMode : std::uint8_t { Keep = 1, Delete = 2 };

struct LocalOffsetEdit {
  std::vector<QString> entityIds;
  double distanceMetres = 0.0;
  LocalOffsetSourceMode sourceMode = LocalOffsetSourceMode::Keep;
  LocalExternalConstraintPolicy constraints =
      LocalExternalConstraintPolicy::Refuse;
};

struct LocalExtendEdit {
  LocalCurvePick curve;
  double targetXMetres = 0.0;
  double targetYMetres = 0.0;
  LocalExternalConstraintPolicy constraints =
      LocalExternalConstraintPolicy::Refuse;
};

struct LocalTrimEdit {
  LocalCurvePick curve;
  LocalExternalConstraintPolicy constraints =
      LocalExternalConstraintPolicy::Refuse;
};

struct LocalTrimPreview {
  std::vector<LocalSketchToolPoint> boundaries;
  bool deletesCurve = false;
};

struct LocalSplitEdit {
  LocalCurvePick curve;
  LocalExternalConstraintPolicy constraints =
      LocalExternalConstraintPolicy::Refuse;
};

struct LocalSplitPreview {
  LocalSketchToolPoint point;
};

struct LocalJoinEdit {
  LocalSketchConstraintSelection first;
  LocalSketchConstraintSelection second;
  LocalExternalConstraintPolicy constraints =
      LocalExternalConstraintPolicy::Refuse;
};

struct LocalConvertToNurbsEdit {
  QString entityId;
  LocalExternalConstraintPolicy constraints =
      LocalExternalConstraintPolicy::Refuse;
};

struct LocalSketchCurveDrag {
  QString entityId;
  double firstXMetres = 0.0;
  double firstYMetres = 0.0;
  double currentXMetres = 0.0;
  double currentYMetres = 0.0;
};

struct LocalSourceReplacement {
  QString expectedSourceRevision;
  QString source;
};

struct LocalSketchProjection {
  QString projectRevision;
  QString sourceRevision;
  QString sourcePath;
  QString functionName;
  QString source;
  LocalSketchPlane plane = LocalSketchPlane::XY;
  QString solveStatus;
  int degreesOfFreedom = -1;
  std::size_t profileCount = 0U;
  std::vector<sketch::SketchObject> objects;
  std::vector<sketch::Constraint> constraints;
  std::shared_ptr<const render::SketchSceneSnapshot> scene;
  bool canUndo = false;
  bool canRedo = false;
};

// Owns one serial engineering lane. Source transformation, history mutation,
// solving, and scene projection execute on its private thread; completions are
// delivered on the owning UI thread.
class LocalSketchSession final : public QObject {
public:
  using ReadinessCompletion = std::function<void(Result<void>)>;
  using Completion =
      std::function<void(Result<LocalSketchProjection> projection)>;
  using CurvePreviewCompletion = std::function<void(
      Result<std::shared_ptr<const render::SketchSceneSnapshot>> scene)>;
  using TrimPreviewCompletion =
      std::function<void(Result<LocalTrimPreview> preview)>;
  using SplitPreviewCompletion =
      std::function<void(Result<LocalSplitPreview> preview)>;

  explicit LocalSketchSession(LocalSketchSessionConfig config,
                              QObject *parent = nullptr);
  ~LocalSketchSession() override;

  LocalSketchSession(const LocalSketchSession &) = delete;
  LocalSketchSession &operator=(const LocalSketchSession &) = delete;

  void whenReady(ReadinessCompletion completion);
  [[nodiscard]] bool create(LocalSketchCreation creation,
                            Completion completion);
  [[nodiscard]] bool applyTool(LocalSketchToolGesture gesture,
                               Completion completion);
  [[nodiscard]] bool applyConstraint(LocalSketchConstraintGesture gesture,
                                     Completion completion);
  [[nodiscard]] bool toggleConstruction(LocalSketchConstructionToggle toggle,
                                        Completion completion);
  [[nodiscard]] bool editBSpline(LocalBSplineEdit edit, Completion completion);
  [[nodiscard]] bool transform(LocalSketchTransform transform,
                               Completion completion);
  [[nodiscard]] bool modifyCorner(LocalCornerEdit edit, Completion completion);
  [[nodiscard]] bool offset(LocalOffsetEdit edit, Completion completion);
  [[nodiscard]] bool extend(LocalExtendEdit edit, Completion completion);
  [[nodiscard]] bool trim(LocalTrimEdit edit, Completion completion);
  [[nodiscard]] bool split(LocalSplitEdit edit, Completion completion);
  [[nodiscard]] bool join(LocalJoinEdit edit, Completion completion);
  [[nodiscard]] bool convertToNurbs(LocalConvertToNurbsEdit edit,
                                    Completion completion);
  [[nodiscard]] bool dragCurve(LocalSketchCurveDrag drag,
                               Completion completion);
  [[nodiscard]] bool previewCurveDrag(LocalSketchCurveDrag drag,
                                      CurvePreviewCompletion completion);
  [[nodiscard]] bool previewTrim(LocalCurvePick curve,
                                 TrimPreviewCompletion completion);
  [[nodiscard]] bool previewSplit(LocalCurvePick curve,
                                  SplitPreviewCompletion completion);
  void cancelPreview();
  [[nodiscard]] bool replaceSource(LocalSourceReplacement replacement,
                                   Completion completion);
  [[nodiscard]] bool undo(Completion completion);
  [[nodiscard]] bool redo(Completion completion);
  [[nodiscard]] std::size_t pendingOperationCount() const;

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace kearne::ui
