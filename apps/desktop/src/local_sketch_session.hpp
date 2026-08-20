#pragma once

#include <kearne/base/value.hpp>
#include <kearne/render/sketch_scene.hpp>

#include <QObject>
#include <QProcessEnvironment>
#include <QString>
#include <QStringList>

#include <cstddef>
#include <functional>
#include <memory>

namespace kearne::ui {

struct LocalSketchSessionConfig {
  QString sourceEditorProgram;
  QStringList sourceEditorArguments;
  QProcessEnvironment sourceEditorEnvironment;
  std::size_t maximumPendingOperations = 4U;
};

struct LocalRectangleGesture {
  double firstXMetres = 0.0;
  double firstYMetres = 0.0;
  double oppositeXMetres = 0.0;
  double oppositeYMetres = 0.0;
  bool construction = false;
};

struct LocalSketchProjection {
  QString projectRevision;
  QString sourceRevision;
  QString sourcePath;
  QString functionName;
  QString source;
  QString solveStatus;
  int degreesOfFreedom = -1;
  std::shared_ptr<const render::SketchSceneSnapshot> scene;
};

// Owns one serial engineering lane. Source transformation, history mutation,
// solving, and scene projection execute on its private thread; completions are
// delivered on the owning UI thread.
class LocalSketchSession final : public QObject {
public:
  using Completion =
      std::function<void(Result<LocalSketchProjection> projection)>;

  explicit LocalSketchSession(LocalSketchSessionConfig config,
                              QObject *parent = nullptr);
  ~LocalSketchSession() override;

  LocalSketchSession(const LocalSketchSession &) = delete;
  LocalSketchSession &operator=(const LocalSketchSession &) = delete;

  [[nodiscard]] bool create(Completion completion);
  [[nodiscard]] bool applyRectangle(LocalRectangleGesture gesture,
                                    Completion completion);
  [[nodiscard]] std::size_t pendingOperationCount() const;

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace kearne::ui
