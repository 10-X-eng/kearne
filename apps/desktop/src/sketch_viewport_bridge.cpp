#include "sketch_viewport_bridge.hpp"

#include "ui_session.hpp"

#include <QCoreApplication>
#include <QEventLoop>
#include <QQuickItem>
#include <QThread>

#include <limits>
#include <new>
#include <utility>

namespace kearne::ui {

Result<std::unique_ptr<SketchViewportBridge>>
SketchViewportBridge::create(QQuickItem &host, UiSession &session,
                             SketchCameraController &camera) {
  try {
    auto bridge = std::unique_ptr<SketchViewportBridge>(
        new SketchViewportBridge{host, session, camera});
    if (auto initialized = bridge->initialize(); !initialized)
      return std::unexpected(std::move(initialized.error()));
    return bridge;
  } catch (const std::bad_alloc &) {
    return std::unexpected(
        diagnostic("desktop.sketch.viewport-allocation",
                   "native Sketch viewport allocation failed"));
  }
}

SketchViewportBridge::SketchViewportBridge(QQuickItem &host, UiSession &session,
                                           SketchCameraController &camera)
    : host_(host), session_(session), camera_(camera),
      item_(std::make_unique<SketchSceneItem>(&host_)),
      publication_(std::make_unique<SketchScenePublicationController>(
          *item_, executor_)) {}

SketchViewportBridge::~SketchViewportBridge() {
  if (!stopped_)
    static_cast<void>(shutdown());
}

Result<void> SketchViewportBridge::initialize() {
  if (QThread::currentThread() != host_.thread() ||
      QThread::currentThread() != session_.thread() ||
      QThread::currentThread() != camera_.thread())
    return std::unexpected(
        diagnostic("desktop.sketch.viewport-thread",
                   "native Sketch viewport must be created on the UI thread"));
  if (!publication_->metrics().subscribed)
    return std::unexpected(publication_->lastDiagnostic());

  item_->setObjectName(QStringLiteral("nativeSketchScene"));
  item_->setParentItem(&host_);
  synchronizeGeometry();
  QObject::connect(&host_, &QQuickItem::widthChanged, this,
                   [this] { synchronizeGeometry(); });
  QObject::connect(&host_, &QQuickItem::heightChanged, this,
                   [this] { synchronizeGeometry(); });
  QObject::connect(&session_, &UiSession::projectionChanged, this,
                   [this] { record(publishScene()); });
  QObject::connect(&camera_, &SketchCameraController::cameraChanged, this,
                   [this] { record(publishCamera()); });
  if (auto cameraResult = publishCamera(); !cameraResult)
    return cameraResult;
  return publishScene();
}

void SketchViewportBridge::synchronizeGeometry() {
  item_->setPosition({0.0, 0.0});
  item_->setSize({host_.width(), host_.height()});
}

Result<void> SketchViewportBridge::publishCamera() {
  auto result = publication_->publishCamera(camera_.camera());
  if (!result)
    return std::unexpected(std::move(result.error()));
  switch (*result) {
  case SketchCameraDecision::Accepted:
  case SketchCameraDecision::Duplicate:
    return {};
  case SketchCameraDecision::StaleGeneration:
    return std::unexpected(diagnostic(
        "desktop.sketch.camera-stale",
        "native Sketch viewport rejected a stale camera generation"));
  case SketchCameraDecision::GenerationConflict:
    return std::unexpected(diagnostic(
        "desktop.sketch.camera-generation-conflict",
        "native Sketch viewport rejected conflicting camera state"));
  }
  return std::unexpected(diagnostic(
      "desktop.sketch.camera-decision",
      "native Sketch viewport returned an unknown camera decision"));
}

Result<void> SketchViewportBridge::publishScene() {
  const auto scene = session_.sketchScene();
  if (scene == publishedScene_)
    return {};
  if (!scene) {
    publishedScene_.reset();
    item_->clearPresentation();
    return {};
  }
  if (productGeneration_ == std::numeric_limits<std::uint64_t>::max())
    return std::unexpected(diagnostic(
        "desktop.sketch.viewport-generation-exhausted",
        "native Sketch viewport generation is exhausted", Severity::Fatal));

  if (!publishedScene_ ||
      publishedScene_->stamp().target != scene->stamp().target) {
    if (auto targeted = publication_->retarget(scene->stamp().target);
        !targeted)
      return targeted;
  }
  auto generation = SketchProductGeneration::create(++productGeneration_);
  auto digest = SketchProductDigest::fromBytes(
      scene->stamp().digest.algorithm(), scene->stamp().digest.bytes());
  if (!generation)
    return std::unexpected(std::move(generation.error()));
  if (!digest)
    return std::unexpected(std::move(digest.error()));
  auto offered = publication_->publishProducts(
      {{scene->stamp().target, *generation, *digest}, scene, {}, {}, {}});
  if (!offered)
    return std::unexpected(std::move(offered.error()));
  publishedScene_ = scene;
  return {};
}

void SketchViewportBridge::record(Result<void> result) {
  if (!result)
    lastDiagnostic_ = std::move(result.error());
}

Result<void> SketchViewportBridge::shutdown(std::chrono::milliseconds timeout) {
  if (stopped_)
    return {};
  if (QThread::currentThread() != thread())
    return std::unexpected(
        diagnostic("desktop.sketch.viewport-shutdown-thread",
                   "native Sketch viewport must stop on the UI thread"));

  const auto deadline = std::chrono::steady_clock::now() + timeout;
  while (publication_) {
    auto stopped = publication_->shutdown();
    if (stopped) {
      publication_.reset();
      break;
    }
    if (stopped.error().code != "desktop.sketch.preparation-backpressure" ||
        std::chrono::steady_clock::now() >= deadline)
      return std::unexpected(std::move(stopped.error()));
    QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
  }

  executor_.requestShutdown();
  while (!executor_.waitUntilDrained(std::chrono::milliseconds{0})) {
    if (std::chrono::steady_clock::now() >= deadline)
      return std::unexpected(
          diagnostic("desktop.sketch.viewport-shutdown-timeout",
                     "native Sketch viewport did not drain before shutdown"));
    QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
    QThread::yieldCurrentThread();
  }
  executor_.join();
  item_.reset();
  stopped_ = true;
  return {};
}

bool SketchViewportBridge::presentationCurrent() const {
  const auto expected = session_.sketchScene();
  if (!expected)
    return true;
  const auto frame = item_->presentedFrame();
  return frame && frame->synchronized() &&
         frame->synchronized()->scene() == expected;
}

} // namespace kearne::ui
