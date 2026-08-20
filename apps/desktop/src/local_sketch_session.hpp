#pragma once

#include <kearne/base/value.hpp>
#include <kearne/render/sketch_scene.hpp>

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

struct LocalRectangleGesture {
  double firstXMetres = 0.0;
  double firstYMetres = 0.0;
  double oppositeXMetres = 0.0;
  double oppositeYMetres = 0.0;
  bool construction = false;
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

  explicit LocalSketchSession(LocalSketchSessionConfig config,
                              QObject *parent = nullptr);
  ~LocalSketchSession() override;

  LocalSketchSession(const LocalSketchSession &) = delete;
  LocalSketchSession &operator=(const LocalSketchSession &) = delete;

  void whenReady(ReadinessCompletion completion);
  [[nodiscard]] bool create(LocalSketchCreation creation,
                            Completion completion);
  [[nodiscard]] bool applyRectangle(LocalRectangleGesture gesture,
                                    Completion completion);
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
