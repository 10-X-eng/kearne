#include "sketch_viewport_bridge.hpp"

#include "sketch_provisional_projection.hpp"
#include "ui_session.hpp"

#include <QByteArrayView>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QEventLoop>
#include <QQuickItem>
#include <QThread>

#include <algorithm>
#include <limits>
#include <new>
#include <span>
#include <utility>

namespace kearne::ui {
namespace {

QString pointKeyName(sketch::PointKey key) {
  switch (key) {
  case sketch::PointKey::Point:
    return QStringLiteral("point");
  case sketch::PointKey::Start:
    return QStringLiteral("start");
  case sketch::PointKey::End:
    return QStringLiteral("end");
  case sketch::PointKey::Center:
    return QStringLiteral("center");
  case sketch::PointKey::Major:
    return QStringLiteral("major");
  case sketch::PointKey::Minor:
    return QStringLiteral("minor");
  case sketch::PointKey::Focus:
    return QStringLiteral("focus");
  }
  return {};
}

std::optional<sketch::PointKey> pointKey(QStringView value) {
  if (value == QStringLiteral("point"))
    return sketch::PointKey::Point;
  if (value == QStringLiteral("start"))
    return sketch::PointKey::Start;
  if (value == QStringLiteral("end"))
    return sketch::PointKey::End;
  if (value == QStringLiteral("center"))
    return sketch::PointKey::Center;
  if (value == QStringLiteral("major"))
    return sketch::PointKey::Major;
  if (value == QStringLiteral("minor"))
    return sketch::PointKey::Minor;
  if (value == QStringLiteral("focus"))
    return sketch::PointKey::Focus;
  return std::nullopt;
}

constexpr std::array overlayRoles{
    render::SketchOverlayRole::Hovered,
    render::SketchOverlayRole::Selected,
    render::SketchOverlayRole::Preview,
    render::SketchOverlayRole::Diagnostic,
};

render::SketchPickTargets pickTargets(SketchSelectionKind selection) {
  switch (selection) {
  case SketchSelectionKind::Point:
    return render::SketchPickTargets::Points;
  case SketchSelectionKind::Curve:
    return render::SketchPickTargets::Curves;
  case SketchSelectionKind::Any:
    return render::SketchPickTargets::All;
  }
  return render::SketchPickTargets::All;
}

Result<SketchProductDigest> productDigest(
    const render::SketchSceneSnapshot &scene,
    const std::shared_ptr<const render::SketchPresentationOverlay> &overlay,
    const std::shared_ptr<const render::SketchProvisionalGeometry>
        &provisional) {
  QCryptographicHash hash{QCryptographicHash::Sha256};
  const auto append = [&hash](bool present,
                              std::span<const std::uint8_t> bytes) {
    const char marker = present ? 1 : 0;
    hash.addData(QByteArrayView{&marker, 1});
    if (present)
      hash.addData(QByteArrayView{reinterpret_cast<const char *>(bytes.data()),
                                  static_cast<qsizetype>(bytes.size())});
  };
  append(true, scene.stamp().digest.bytes());
  append(static_cast<bool>(overlay),
         overlay ? std::span<const std::uint8_t>{overlay->payloadDigest().bytes}
                 : std::span<const std::uint8_t>{});
  append(static_cast<bool>(provisional),
         provisional ? provisional->stamp().payload.bytes()
                     : std::span<const std::uint8_t>{});
  const QByteArray hashed = hash.result();
  SketchProductDigest::Bytes bytes{};
  if (hashed.size() != static_cast<qsizetype>(bytes.size()))
    return std::unexpected(
        diagnostic("desktop.sketch.product-digest",
                   "native Sketch product digest has an unexpected size",
                   Severity::Fatal));
  std::transform(hashed.cbegin(), hashed.cend(), bytes.begin(),
                 [](char value) { return static_cast<std::uint8_t>(value); });
  return SketchProductDigest::fromBytes("sha256", bytes);
}

} // namespace

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
                   [this] { record(publishProjection()); });
  QObject::connect(&camera_, &SketchCameraController::cameraChanged, this,
                   [this] { record(publishCamera()); });
  session_.setSketchPickHandler([this](QPointF point, double tolerance,
                                       SketchSelectionKind selection)
                                    -> std::optional<SketchPickSelection> {
    auto evidence =
        publication_->pick(point, tolerance, pickTargets(selection));
    if (!evidence || !evidence->item.hit)
      return std::nullopt;
    const auto &hit = *evidence->item.hit;
    return SketchPickSelection{QString::fromStdString(hit.entity.toString()),
                               hit.pointKey ? pointKeyName(*hit.pointKey)
                                            : QString{},
                               {millimetersFromMetres(hit.closestPoint.x),
                                millimetersFromMetres(hit.closestPoint.y)}};
  });
  session_.setSketchHoverHandler(
      [this](std::optional<SketchPickSelection> selection) {
        const std::optional<std::pair<QString, QString>> identity =
            selection ? std::optional{std::pair{selection->entityId,
                                                selection->pointKey}}
                      : std::nullopt;
        record(publishOverlay(identity, selected_));
      });
  if (auto cameraResult = publishCamera(); !cameraResult)
    return cameraResult;
  return publishProjection();
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
    return std::unexpected(
        diagnostic("desktop.sketch.camera-generation-conflict",
                   "native Sketch viewport rejected conflicting camera state"));
  }
  return std::unexpected(
      diagnostic("desktop.sketch.camera-decision",
                 "native Sketch viewport returned an unknown camera decision"));
}

Result<void> SketchViewportBridge::publishProjection() {
  const auto nextScene = session_.sketchScene();
  const bool sceneChanged = nextScene != publishedScene_;
  const bool sameSemanticTarget =
      nextScene && publishedScene_ &&
      nextScene->stamp().target == publishedScene_->stamp().target;
  const auto retainedHover =
      !sceneChanged || sameSemanticTarget ? hovered_ : std::nullopt;
  if (auto scene = publishScene(); !scene)
    return scene;
  if (auto provisional = publishProvisional(); !provisional)
    return provisional;
  return publishOverlay(retainedHover, session_.selectedSketchEntityIds());
}

Result<void> SketchViewportBridge::publishScene() {
  const auto scene = session_.sketchScene();
  if (scene == publishedScene_)
    return {};
  if (!scene) {
    publishedScene_.reset();
    overlay_.reset();
    provisional_.reset();
    overlayRoleSets_ = {};
    hovered_.reset();
    selected_.clear();
    publishedDraft_.clear();
    provisionalCommand_.clear();
    presentationPublished_ = false;
    item_->clearPresentation();
    return {};
  }
  if (!publishedScene_ ||
      publishedScene_->stamp().target != scene->stamp().target) {
    if (auto targeted = publication_->retarget(scene->stamp().target);
        !targeted)
      return targeted;
  }
  publishedScene_ = scene;
  overlay_.reset();
  provisional_.reset();
  overlayRoleSets_ = {};
  hovered_.reset();
  selected_.clear();
  publishedDraft_.clear();
  presentationPublished_ = false;
  return {};
}

Result<void> SketchViewportBridge::publishProvisional() {
  if (!publishedScene_)
    return {};
  std::vector<SketchPrimitiveProjection> draft;
  for (const SketchPrimitiveProjection &primitive :
       session_.sketchPrimitiveProjections()) {
    if (primitive.draft)
      draft.push_back(primitive);
  }
  const QString command = session_.activeCommandId();
  if (command != provisionalCommand_) {
    if (toolInstanceGeneration_ == std::numeric_limits<std::uint64_t>::max())
      return std::unexpected(
          diagnostic("desktop.sketch.tool-instance-generation-exhausted",
                     "native Sketch tool instance generation is exhausted",
                     Severity::Fatal));
    ++toolInstanceGeneration_;
    provisionalCommand_ = command;
    publishedDraft_.clear();
    provisional_.reset();
    presentationPublished_ = false;
  }
  if (draft == publishedDraft_)
    return {};
  if (draft.empty()) {
    publishedDraft_.clear();
    provisional_.reset();
    presentationPublished_ = false;
    return {};
  }
  if (provisionalCommand_.isEmpty())
    return std::unexpected(
        diagnostic("desktop.sketch.provisional-without-tool",
                   "native Sketch preview has no active tool instance"));
  if (provisionalGeneration_ == std::numeric_limits<std::uint64_t>::max())
    return std::unexpected(diagnostic(
        "desktop.sketch.provisional-generation-exhausted",
        "native Sketch preview generation is exhausted", Severity::Fatal));
  auto editSession = render::SketchEditSessionHandle::create(1U);
  auto toolInstance =
      render::SketchToolInstanceHandle::create(toolInstanceGeneration_);
  auto generation =
      render::SketchProvisionalGeneration::create(++provisionalGeneration_);
  if (!editSession)
    return std::unexpected(std::move(editSession.error()));
  if (!toolInstance)
    return std::unexpected(std::move(toolInstance.error()));
  if (!generation)
    return std::unexpected(std::move(generation.error()));
  auto projected = projectSketchProvisional(
      {publishedScene_->stamp(), *editSession, *toolInstance, *generation},
      draft);
  if (!projected)
    return std::unexpected(std::move(projected.error()));
  provisional_ = std::move(*projected);
  publishedDraft_ = std::move(draft);
  presentationPublished_ = false;
  return {};
}

Result<void> SketchViewportBridge::publishOverlay(
    std::optional<std::pair<QString, QString>> hover,
    std::span<const QString> selected) {
  const std::vector<QString> selectedIdentities{selected.begin(),
                                                selected.end()};
  if (hover == hovered_ && selectedIdentities == selected_ &&
      presentationPublished_)
    return {};
  if (!publishedScene_)
    return {};
  const bool wantsOverlay = hover.has_value() || !selected.empty();
  const bool overlayChanged =
      hover != hovered_ || selectedIdentities != selected_ ||
      (wantsOverlay && !overlay_) || (!wantsOverlay && overlay_);
  if (productGeneration_ == std::numeric_limits<std::uint64_t>::max() ||
      (wantsOverlay && overlayChanged &&
       presentationGeneration_ == std::numeric_limits<std::uint64_t>::max()))
    return std::unexpected(diagnostic(
        "desktop.sketch.presentation-generation-exhausted",
        "native Sketch presentation generation is exhausted", Severity::Fatal));

  std::shared_ptr<const render::SketchPresentationOverlay> overlay;
  std::optional<render::SketchOverlayScope> hoveredScope;
  if (hover) {
    auto entity = SketchEntityId::parse(hover->first.toStdString());
    if (!entity || !publishedScene_->findPrimitive(*entity))
      return std::unexpected(
          diagnostic("desktop.sketch.hover-entity",
                     "native Sketch hover references missing geometry"));
    std::optional<sketch::PointKey> point;
    if (!hover->second.isEmpty()) {
      point = pointKey(hover->second);
      if (!point)
        return std::unexpected(
            diagnostic("desktop.sketch.hover-point",
                       "native Sketch hover references an invalid point"));
    }
    hoveredScope = render::SketchOverlayScope{*entity, point};
  }
  std::vector<render::SketchOverlayScope> selectedScopes;
  selectedScopes.reserve(selected.size());
  for (const QString &identity : selected) {
    auto entity = SketchEntityId::parse(identity.toStdString());
    if (!entity || !publishedScene_->findPrimitive(*entity))
      return std::unexpected(
          diagnostic("desktop.sketch.selection-entity",
                     "native Sketch selection references missing geometry"));
    selectedScopes.push_back({*entity, std::nullopt});
  }
  if (wantsOverlay && !overlayChanged) {
    overlay = overlay_;
  } else if (hoveredScope || !selectedScopes.empty()) {
    for (std::size_t index = 0U; index < overlayRoles.size(); ++index) {
      const bool unchanged = (index == 0U && hover == hovered_) ||
                             (index == 1U && selectedIdentities == selected_) ||
                             index > 1U;
      if (unchanged && overlayRoleSets_[index] &&
          overlayRoleSets_[index]->base() == publishedScene_)
        continue;
      std::span<const render::SketchOverlayScope> scopes;
      if (index == 0U && hoveredScope)
        scopes = {&*hoveredScope, 1U};
      else if (index == 1U)
        scopes = selectedScopes;
      auto created = render::SketchOverlayRoleSet::create(
          publishedScene_, overlayRoles[index], scopes);
      if (!created)
        return std::unexpected(std::move(created.error()));
      overlayRoleSets_[index] = std::move(*created);
    }
    auto generation =
        render::SketchPresentationGeneration::create(++presentationGeneration_);
    if (!generation)
      return std::unexpected(std::move(generation.error()));
    auto created = render::SketchPresentationOverlay::create(
        publishedScene_, *generation, overlayRoleSets_);
    if (!created)
      return std::unexpected(std::move(created.error()));
    overlay = std::move(*created);
  }

  auto generation = SketchProductGeneration::create(++productGeneration_);
  if (!generation)
    return std::unexpected(std::move(generation.error()));
  auto digest = productDigest(*publishedScene_, overlay, provisional_);
  if (!digest)
    return std::unexpected(std::move(digest.error()));
  auto offered = publication_->publishProducts(
      {{publishedScene_->stamp().target, *generation, *digest},
       publishedScene_,
       overlay,
       provisional_,
       {}});
  if (!offered)
    return std::unexpected(std::move(offered.error()));
  hovered_ = std::move(hover);
  selected_ = std::move(selectedIdentities);
  overlay_ = std::move(overlay);
  presentationPublished_ = true;
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

  session_.clearSketchPickHandler();
  session_.clearSketchHoverHandler();

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
