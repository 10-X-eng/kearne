#include "sketch_scene_fixture.hpp"
#include "sketch_scene_item.hpp"

#include <QCoreApplication>
#include <QEvent>
#include <QEventLoop>
#include <QGuiApplication>
#include <QImage>
#include <QQuickRenderControl>
#include <QQuickRenderTarget>
#include <QQuickWindow>
#include <QSGRendererInterface>
#include <QString>
#if QT_CONFIG(vulkan)
#include <QVulkanInstance>
#endif
#include <rhi/qrhi.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace {

using namespace kearne;
using namespace kearne::render;
using namespace kearne::ui;
using namespace kearne::ui::test;

constexpr int backendUnavailable = 77;
constexpr QSize renderSize{320, 240};
const QColor background{12, 18, 26};

void require(bool condition, const char *message) {
  if (!condition)
    throw std::runtime_error(message);
}

struct Backend {
  QString name;
  QSGRendererInterface::GraphicsApi api;
};

std::optional<Backend> parseBackend(int argc, char *argv[]) {
  if (argc != 3 || std::string_view{argv[1]} != "--backend")
    return std::nullopt;
  const QString name = QString::fromLocal8Bit(argv[2]).toLower();
  if (name == QStringLiteral("opengl"))
    return Backend{name, QSGRendererInterface::OpenGL};
  if (name == QStringLiteral("vulkan"))
    return Backend{name, QSGRendererInterface::Vulkan};
  if (name == QStringLiteral("metal"))
    return Backend{name, QSGRendererInterface::Metal};
  if (name == QStringLiteral("d3d11"))
    return Backend{name, QSGRendererInterface::Direct3D11};
  if (name == QStringLiteral("d3d12"))
    return Backend{name, QSGRendererInterface::Direct3D12};
  return std::nullopt;
}

class OffscreenQuickRenderer final {
public:
  OffscreenQuickRenderer() : window_(&control_) {
    window_.setColor(background);
    window_.setGeometry(0, 0, renderSize.width(), renderSize.height());
    window_.contentItem()->setSize(renderSize);
  }

  OffscreenQuickRenderer(const OffscreenQuickRenderer &) = delete;
  OffscreenQuickRenderer &operator=(const OffscreenQuickRenderer &) = delete;

  ~OffscreenQuickRenderer() { release(); }

  QQuickWindow &window() { return window_; }

  bool initialize(QString &failure) {
    if (!control_.initialize()) {
      failure = QStringLiteral("QQuickRenderControl::initialize failed");
      return false;
    }
    initialized_ = true;
    rhi_ = control_.rhi();
    if (!rhi_) {
      failure = QStringLiteral("initialized scene graph has no QRhi");
      return false;
    }
    const auto actualApi = window_.rendererInterface()->graphicsApi();
    if (actualApi == QSGRendererInterface::Software ||
        actualApi == QSGRendererInterface::Null) {
      failure = QStringLiteral("scene graph selected a non-RHI backend");
      return false;
    }

    texture_.reset(rhi_->newTexture(QRhiTexture::RGBA8, renderSize, 1,
                                    QRhiTexture::RenderTarget |
                                        QRhiTexture::UsedAsTransferSource));
    if (!texture_ || !texture_->create()) {
      failure = QStringLiteral("cannot create RGBA8 readback texture");
      return false;
    }
    depthStencil_.reset(
        rhi_->newRenderBuffer(QRhiRenderBuffer::DepthStencil, renderSize, 1));
    if (!depthStencil_ || !depthStencil_->create()) {
      failure = QStringLiteral("cannot create depth/stencil buffer");
      return false;
    }
    QRhiTextureRenderTargetDescription description{
        QRhiColorAttachment{texture_.get()}};
    description.setDepthStencilBuffer(depthStencil_.get());
    renderTarget_.reset(rhi_->newTextureRenderTarget(description));
    if (!renderTarget_) {
      failure = QStringLiteral("cannot allocate texture render target");
      return false;
    }
    renderPass_.reset(renderTarget_->newCompatibleRenderPassDescriptor());
    renderTarget_->setRenderPassDescriptor(renderPass_.get());
    if (!renderPass_ || !renderTarget_->create()) {
      failure = QStringLiteral("cannot create texture render target");
      return false;
    }
    QQuickRenderTarget quickTarget =
        QQuickRenderTarget::fromRhiRenderTarget(renderTarget_.get());
    quickTarget.setDevicePixelRatio(1.0);
    window_.setRenderTarget(quickTarget);
    return true;
  }

  void invalidate() { release(); }

  bool reinitialize(QString &failure) { return initialize(failure); }

  QImage render() {
    QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
    QCoreApplication::processEvents(QEventLoop::AllEvents);
    control_.polishItems();
    control_.beginFrame();
    control_.sync();
    control_.render();

    QRhiReadbackResult readback;
    QRhiResourceUpdateBatch *updates = rhi_->nextResourceUpdateBatch();
    updates->readBackTexture(texture_.get(), &readback);
    QRhiCommandBuffer *commands = control_.commandBuffer();
    require(commands != nullptr, "offscreen frame has no RHI command buffer");
    commands->resourceUpdate(updates);
    control_.endFrame();
    require(readback.pixelSize == renderSize && !readback.data.isEmpty(),
            "offscreen RHI readback did not complete");

    QImage wrapped{reinterpret_cast<const uchar *>(readback.data.constData()),
                   readback.pixelSize.width(), readback.pixelSize.height(),
                   QImage::Format_RGBA8888_Premultiplied};
    if (!rhi_->isYUpInFramebuffer())
      return wrapped.copy();
#if QT_VERSION >= QT_VERSION_CHECK(6, 9, 0)
    return wrapped.flipped(Qt::Vertical);
#else
    return wrapped.mirrored(false, true);
#endif
  }

  QString driver() const {
    return QString::fromUtf8(rhi_->driverInfo().deviceName);
  }

private:
  void release() {
    window_.setRenderTarget({});
    if (initialized_)
      control_.invalidate();
    renderPass_.reset();
    renderTarget_.reset();
    depthStencil_.reset();
    texture_.reset();
    rhi_ = nullptr;
    initialized_ = false;
  }

  QQuickRenderControl control_;
  QQuickWindow window_;
  QRhi *rhi_ = nullptr;
  std::unique_ptr<QRhiTexture> texture_;
  std::unique_ptr<QRhiRenderBuffer> depthStencil_;
  std::unique_ptr<QRhiTextureRenderTarget> renderTarget_;
  std::unique_ptr<QRhiRenderPassDescriptor> renderPass_;
  bool initialized_ = false;
};

struct PixelSummary {
  QRect bounds;
  std::size_t pixels = 0;
  std::uint64_t red = 0;
  std::uint64_t green = 0;
  std::uint64_t blue = 0;
  QPointF centroid;
};

bool differsFromBackground(QRgb pixel) {
  constexpr int tolerance = 10;
  return std::max({std::abs(qRed(pixel) - background.red()),
                   std::abs(qGreen(pixel) - background.green()),
                   std::abs(qBlue(pixel) - background.blue())}) > tolerance;
}

PixelSummary summarize(const QImage &source) {
  const QImage image = source.convertToFormat(QImage::Format_ARGB32);
  PixelSummary result;
  int minimumX = image.width();
  int minimumY = image.height();
  int maximumX = -1;
  int maximumY = -1;
  double xTotal = 0.0;
  double yTotal = 0.0;
  for (int y = 0; y < image.height(); ++y) {
    const auto *row = reinterpret_cast<const QRgb *>(image.constScanLine(y));
    for (int x = 0; x < image.width(); ++x) {
      const QRgb pixel = row[x];
      if (!differsFromBackground(pixel))
        continue;
      ++result.pixels;
      result.red += static_cast<std::uint64_t>(qRed(pixel));
      result.green += static_cast<std::uint64_t>(qGreen(pixel));
      result.blue += static_cast<std::uint64_t>(qBlue(pixel));
      xTotal += static_cast<double>(x);
      yTotal += static_cast<double>(y);
      minimumX = std::min(minimumX, x);
      minimumY = std::min(minimumY, y);
      maximumX = std::max(maximumX, x);
      maximumY = std::max(maximumY, y);
    }
  }
  if (result.pixels != 0U) {
    result.bounds =
        QRect{QPoint{minimumX, minimumY}, QPoint{maximumX, maximumY}};
    result.centroid = QPointF{xTotal / static_cast<double>(result.pixels),
                              yTotal / static_cast<double>(result.pixels)};
  }
  return result;
}

std::size_t differingPixels(const QImage &first, const QImage &second) {
  require(first.size() == second.size(), "rendered frame sizes disagree");
  const QImage left = first.convertToFormat(QImage::Format_ARGB32);
  const QImage right = second.convertToFormat(QImage::Format_ARGB32);
  std::size_t count = 0;
  for (int y = 0; y < left.height(); ++y) {
    const auto *leftRow = reinterpret_cast<const QRgb *>(left.constScanLine(y));
    const auto *rightRow =
        reinterpret_cast<const QRgb *>(right.constScanLine(y));
    for (int x = 0; x < left.width(); ++x) {
      const QRgb firstPixel = leftRow[x];
      const QRgb secondPixel = rightRow[x];
      if (std::max({std::abs(qRed(firstPixel) - qRed(secondPixel)),
                    std::abs(qGreen(firstPixel) - qGreen(secondPixel)),
                    std::abs(qBlue(firstPixel) - qBlue(secondPixel))}) > 4)
        ++count;
    }
  }
  return count;
}

bool hasForegroundNear(const QImage &source, QPointF point, int radius = 4) {
  const QImage image = source.convertToFormat(QImage::Format_ARGB32);
  const int centerX = qRound(point.x());
  const int centerY = qRound(point.y());
  for (int y = std::max(0, centerY - radius);
       y <= std::min(image.height() - 1, centerY + radius); ++y) {
    const auto *row = reinterpret_cast<const QRgb *>(image.constScanLine(y));
    for (int x = std::max(0, centerX - radius);
         x <= std::min(image.width() - 1, centerX + radius); ++x)
      if (differsFromBackground(row[x]))
        return true;
  }
  return false;
}

bool hasGreenNear(const QImage &source, QPointF point, int radius = 4) {
  const QImage image = source.convertToFormat(QImage::Format_ARGB32);
  const int centerX = qRound(point.x());
  const int centerY = qRound(point.y());
  for (int y = std::max(0, centerY - radius);
       y <= std::min(image.height() - 1, centerY + radius); ++y) {
    const auto *row = reinterpret_cast<const QRgb *>(image.constScanLine(y));
    for (int x = std::max(0, centerX - radius);
         x <= std::min(image.width() - 1, centerX + radius); ++x)
      if (qGreen(row[x]) > qRed(row[x]) + 40 &&
          qGreen(row[x]) > qBlue(row[x]) + 40)
        return true;
  }
  return false;
}

bool hasBlueNear(const QImage &source, QPointF point, int radius = 4) {
  const QImage image = source.convertToFormat(QImage::Format_ARGB32);
  const int centerX = qRound(point.x());
  const int centerY = qRound(point.y());
  for (int y = std::max(0, centerY - radius);
       y <= std::min(image.height() - 1, centerY + radius); ++y) {
    const auto *row = reinterpret_cast<const QRgb *>(image.constScanLine(y));
    for (int x = std::max(0, centerX - radius);
         x <= std::min(image.width() - 1, centerX + radius); ++x)
      if (qBlue(row[x]) > qRed(row[x]) + 40 &&
          qBlue(row[x]) > qGreen(row[x]) + 40)
        return true;
  }
  return false;
}

bool hasMagentaNear(const QImage &source, QPointF point, int radius = 4) {
  const QImage image = source.convertToFormat(QImage::Format_ARGB32);
  const int centerX = qRound(point.x());
  const int centerY = qRound(point.y());
  for (int y = std::max(0, centerY - radius);
       y <= std::min(image.height() - 1, centerY + radius); ++y) {
    const auto *row = reinterpret_cast<const QRgb *>(image.constScanLine(y));
    for (int x = std::max(0, centerX - radius);
         x <= std::min(image.width() - 1, centerX + radius); ++x)
      if (qRed(row[x]) > qGreen(row[x]) + 40 &&
          qBlue(row[x]) > qGreen(row[x]) + 40)
        return true;
  }
  return false;
}

SketchScenePalette palette(QRgb color) {
  return {color, color, color, color, color};
}

void publishFixture(OffscreenQuickRenderer &renderer, SketchSceneItem &item,
                    SceneStamp sceneStamp, std::uint64_t seed, QRgb color) {
  item.setSize(renderSize);
  item.retarget(sceneStamp.target);
  item.setPalette(palette(color));
  require(item.publishCamera({2, {}, 0.0005, 0.0}) ==
              SketchCameraDecision::Accepted,
          "lifecycle fixture camera failed");
  auto prepared =
      prepareSketchScene(scene(24, seed, sceneStamp), item.requestedLod());
  require(prepared && item.publishProducts(preparedProductPacket(*prepared)) ==
                          PreparedSketchSceneOffer{
                              PreparedSketchSceneDecision::Accepted, false},
          "lifecycle fixture scene failed");
  require(summarize(renderer.render()).pixels > 0U && item.presentedScene() &&
              item.presentedScene()->scene()->stamp() == sceneStamp,
          "lifecycle fixture did not render its exact scene");
}

void verifyIndependentControlLifecycle(OffscreenQuickRenderer &survivor) {
  if (survivor.window().rendererInterface()->graphicsApi() !=
      QSGRendererInterface::OpenGL)
    return;

  {
    const SceneStamp survivorStamp = stamp(30, 1, 30, 30, 30, 1);
    SketchSceneItem survivorItem{survivor.window().contentItem()};
    publishFixture(survivor, survivorItem, survivorStamp, 81,
                   qRgb(48, 180, 224));

    {
      OffscreenQuickRenderer closing;
      QString failure;
      require(closing.initialize(failure),
              "second render control could not initialize");
      const SceneStamp closingStamp = stamp(31, 1, 31, 31, 31, 1);
      SketchSceneItem closingItem{closing.window().contentItem()};
      publishFixture(closing, closingItem, closingStamp, 82,
                     qRgb(224, 128, 48));
    }

    require(survivorItem.publishCamera({3, {0.004, -0.002}, 0.0005, 0.2}) ==
                SketchCameraDecision::Accepted,
            "surviving render control rejected a camera update");
    const QImage continued = survivor.render();
    require(
        summarize(continued).pixels > 0U && survivorItem.presentedScene() &&
            survivorItem.presentedScene()->scene()->stamp() == survivorStamp &&
            survivorItem.presentedScene()->transform().camera().generation ==
                3U,
        "closing one render control corrupted the surviving control");

    survivorItem.setVisible(false);
    auto hiddenPick = survivorItem.pick({160.0, 120.0}, 8.0);
    require(!hiddenPick &&
                hiddenPick.error().code == "desktop.sketch.not-presented" &&
                !survivorItem.presentedScene() &&
                summarize(survivor.render()).pixels == 0U,
            "hidden sketch scene retained pixels or pick evidence");
    survivorItem.setVisible(true);
    require(summarize(survivor.render()).pixels > 0U &&
                survivorItem.presentedScene() &&
                survivorItem.presentedScene()->scene()->stamp() ==
                    survivorStamp,
            "reshown sketch scene lost its coherent state");
    survivorItem.setOpacity(0.0);
    auto transparentPick = survivorItem.pick({160.0, 120.0}, 8.0);
    require(!transparentPick &&
                transparentPick.error().code ==
                    "desktop.sketch.not-presented" &&
                !survivorItem.presentedScene() &&
                summarize(survivor.render()).pixels == 0U,
            "transparent sketch scene retained pixels or pick evidence");
    survivorItem.setOpacity(1.0);
    require(summarize(survivor.render()).pixels > 0U &&
                survivorItem.presentedScene(),
            "opaque sketch scene did not resynchronize");
  }

  static_cast<void>(survivor.render());
  const SceneStamp recreatedStamp = stamp(32, 1, 32, 32, 32, 1);
  SketchSceneItem recreated{survivor.window().contentItem()};
  publishFixture(survivor, recreated, recreatedStamp, 83, qRgb(160, 96, 224));
}

void verifyDeviceLossLifecycle(OffscreenQuickRenderer &renderer) {
  const SceneStamp sceneStamp = stamp(33, 1, 33, 33, 33, 1);
  SketchSceneItem item{renderer.window().contentItem()};
  publishFixture(renderer, item, sceneStamp, 84, qRgb(52, 184, 132));
  const QImage before = renderer.render();
  auto beforePick = item.pick({160.0, 120.0}, 8.0);
  require(beforePick && beforePick->scene == sceneStamp,
          "pre-invalidation pick lost its exact stamp");

  QString failure;
  renderer.invalidate();
  auto invalidatedPick = item.pick({160.0, 120.0}, 8.0);
  require(!invalidatedPick &&
              invalidatedPick.error().code == "desktop.sketch.not-presented" &&
              !item.presentedScene(),
          "device loss retained stale pick evidence");
  require(renderer.reinitialize(failure),
          "render control could not recover after invalidation");
  item.update();
  QImage restored;
  for (std::size_t frame = 0; frame < 8U; ++frame) {
    restored = renderer.render();
    if (summarize(restored).pixels > 0U)
      break;
  }
  auto restoredPick = item.pick({160.0, 120.0}, 8.0);
  require(summarize(restored).pixels > 0U && restoredPick &&
              restoredPick->scene == sceneStamp && item.presentedScene() &&
              item.presentedScene()->scene()->stamp() == sceneStamp &&
              differingPixels(before, restored) <= 4U,
          "render invalidation did not recreate the exact coherent scene");
}

void verifyPatternPixelsMatchPicking(OffscreenQuickRenderer &renderer) {
  const SceneStamp sceneStamp = stamp(36, 1, 36, 36, 36, 1);
  constexpr Point2d center{1.0e6, -1.0e6};
  std::vector<SketchStyle> dashedStyles{{SketchStyleRole::Construction,
                                         SketchLinePattern::Dashed, 4.0F, 8.0F,
                                         0U}};
  std::vector<Point2d> points{{center.x - 0.05, center.y},
                              {center.x + 0.05, center.y}};
  std::vector<PackedSketchPrimitive> primitives{
      {id<SketchEntityId>(360U), *SketchPrimitiveHandle::create(1U), 0U, 0U,
       SketchPrimitiveKind::Line,
       SketchPrimitiveFlags::Visible | SketchPrimitiveFlags::Selectable, 0.0,
       0.0, 0.0}};
  auto snapshot =
      SketchSceneSnapshot::create(sceneStamp, std::move(dashedStyles),
                                  std::move(points), std::move(primitives));
  require(snapshot.has_value(), "native dashed fixture was invalid");
  auto generated =
      std::make_shared<const SketchSceneSnapshot>(std::move(*snapshot));

  SketchSceneItem item{renderer.window().contentItem()};
  item.setSize(renderSize);
  item.retarget(sceneStamp.target);
  item.setPalette(palette(qRgb(232, 116, 44)));
  const SketchCamera2d camera{2U, center, 0.001, 0.43};
  require(item.publishCamera(camera) == SketchCameraDecision::Accepted,
          "native dashed camera failed");
  auto prepared = prepareSketchScene(generated, item.requestedLod());
  auto packet = prepared ? preparedProductPacket(*prepared) : nullptr;
  require(prepared && packet && item.publishProducts(packet).has_value(),
          "native dashed scene failed");
  const QImage frame = renderer.render();
  const auto synchronized = item.presentedScene();
  require(static_cast<bool>(synchronized),
          "native dashed frame was not synchronized");

  struct Sample {
    double pathLogicalPixels;
    bool visible;
  };
  for (const Sample sample :
       {Sample{5.0, true}, Sample{16.0, false}, Sample{45.0, true},
        Sample{56.0, false}, Sample{85.0, true}, Sample{96.0, false}}) {
    const Point2d canonical{center.x - 0.05 +
                                sample.pathLogicalPixels *
                                    camera.metresPerLogicalPixel,
                            center.y};
    const QPointF query = synchronized->transform().toItem(canonical);
    const auto picked = item.pick(query, 0.0, SketchPickTargets::Curves);
    const bool pickedVisible = picked && static_cast<bool>(picked->hit);
    const bool rasterVisible = hasForegroundNear(frame, query, 0);
    if (!picked || pickedVisible != sample.visible ||
        rasterVisible != sample.visible)
      throw std::runtime_error(
          "shader dash pixels and exact picking disagreed: path=" +
          std::to_string(sample.pathLogicalPixels) +
          " expected=" + std::to_string(sample.visible) +
          " pick=" + std::to_string(pickedVisible) +
          " raster=" + std::to_string(rasterVisible) +
          " error=" + (picked ? std::string{} : picked.error().code));
  }

  const std::uint64_t builds = item.geometryBuildCount();
  auto nextSource = productPacket(generated, 2U, 2U);
  auto nextPacket = PreparedSketchProducts::create(nextSource, *prepared);
  require(nextPacket && item.publishProducts(*nextPacket).has_value(),
          "same-scene product update was rejected");
  static_cast<void>(renderer.render());
  const auto updated = item.presentedScene();
  require(updated && updated->products() == *nextPacket &&
              updated->products()->source() == nextSource &&
              updated->prepared() == *prepared &&
              item.geometryBuildCount() == builds,
          "presented frame lost or rebuilt a same-scene product packet");
}

void verifyProvisionalGeometry(OffscreenQuickRenderer &renderer) {
  const SceneStamp sceneStamp = stamp(37, 1, 37, 37, 37, 1);
  auto generated = scene(1, 95, sceneStamp);
  SketchSceneItem item{renderer.window().contentItem()};
  item.setSize(renderSize);
  item.retarget(sceneStamp.target);
  SketchScenePalette colors;
  colors.regular = qRgb(224, 52, 68);
  colors.preview = qRgb(36, 214, 112);
  item.setPalette(colors);
  require(item.publishCamera({2, {}, 0.001, 0.0}) ==
              SketchCameraDecision::Accepted,
          "provisional fixture camera failed");

  auto preparedBase = prepareSketchScene(generated, item.requestedLod());
  auto basePacket =
      preparedBase ? preparedProductPacket(*preparedBase) : nullptr;
  require(basePacket && item.publishProducts(basePacket).has_value(),
          "provisional fixture base failed");
  const QImage baseFrame = renderer.render();
  require(item.presentedFrame() &&
              item.presentedFrame()->synchronized()->products() == basePacket,
          "provisional fixture base was not presented");

  auto edit = SketchEditSessionHandle::create(1U);
  auto tool = SketchToolInstanceHandle::create(1U);
  auto generation = SketchProvisionalGeneration::create(1U);
  auto handle = SketchProvisionalPrimitiveHandle::create(1U);
  require(edit && tool && generation && handle,
          "provisional fixture identity failed");
  const SketchProvisionalStamp provisionalStamp{
      {sceneStamp, *edit, *tool},
      *generation,
      digest<SketchProvisionalDigest>(37U)};
  const std::array<PackedSketchProvisionalPrimitive, 1> primitives{{
      {*handle,
       {{{-0.06, 0.06}, {0.06, 0.06}}},
       2U,
       SketchPrimitiveKind::Line,
       SketchProvisionalClassification::Regular,
       0.0,
       0.0,
       0.0},
  }};
  auto provisional =
      SketchProvisionalGeometry::create(provisionalStamp, primitives);
  require(provisional.has_value(), "provisional fixture geometry failed");
  auto source = std::make_shared<const SketchSceneProducts>(
      SketchSceneProducts{productStamp(sceneStamp.target, 2U, 2U),
                          generated,
                          {},
                          *provisional,
                          {}});
  auto prepared =
      prepareSketchProducts(source, item.requestedLod(), {}, basePacket);
  require(prepared && (*prepared)->base() == *preparedBase &&
              (*prepared)->provisional() &&
              item.publishProducts(*prepared).has_value(),
          "provisional fixture product failed");

  QImage provisionalFrame;
  for (std::size_t frame = 0U; frame < 32U; ++frame) {
    provisionalFrame = renderer.render();
    const auto presented = item.presentedFrame();
    if (presented && presented->synchronized()->products() == *prepared)
      break;
  }
  const auto presented = item.presentedFrame();
  require(presented && presented->synchronized()->products() == *prepared &&
              presented->provisionalChunks() &&
              presented->provisionalChunks()->size() > 0U,
          "provisional packet did not reach the exact render pass");
  const QPointF lineCenter =
      presented->synchronized()->transform().toItem({0.0, 0.06});
  require(!hasForegroundNear(baseFrame, lineCenter, 3) &&
              hasForegroundNear(provisionalFrame, lineCenter, 3) &&
              differingPixels(baseFrame, provisionalFrame) > 80U,
          "provisional geometry was not drawn by the native renderer");
  const auto pick = item.pick(lineCenter, 0.0, SketchPickTargets::Curves);
  require(pick && !pick->hit && pick->presented == presented,
          "provisional geometry corrupted base picking evidence");

  auto removedSource = productPacket(generated, 3U, 3U);
  auto removed = PreparedSketchProducts::create(removedSource, *preparedBase);
  require(removed && item.publishProducts(*removed).has_value(),
          "provisional removal product failed");
  QImage removedFrame;
  for (std::size_t frame = 0U; frame < 64U; ++frame) {
    removedFrame = renderer.render();
    const auto current = item.presentedFrame();
    if (current && current->synchronized()->products() == *removed)
      break;
  }
  require(item.presentedFrame() &&
              item.presentedFrame()->synchronized()->products() == *removed &&
              !item.presentedFrame()->provisionalChunks() &&
              !hasForegroundNear(removedFrame, lineCenter, 3),
          "removed provisional geometry remained resident or visible");
}

void verifyOverlaySpans(OffscreenQuickRenderer &renderer) {
  const SceneStamp sceneStamp = stamp(38, 1, 38, 38, 38, 1);
  auto generated = scene(4, 96, sceneStamp);
  const PackedSketchPrimitive *line = nullptr;
  for (const PackedSketchPrimitive &primitive : generated->primitives())
    if (primitive.kind == SketchPrimitiveKind::Line) {
      line = &primitive;
      break;
    }
  require(line != nullptr, "overlay fixture has no line");

  SketchSceneItem item{renderer.window().contentItem()};
  item.setSize(renderSize);
  item.retarget(sceneStamp.target);
  SketchScenePalette colors;
  colors.regular = qRgb(224, 52, 68);
  colors.selected = qRgb(36, 214, 112);
  item.setPalette(colors);
  require(item.publishCamera({2, {}, 0.001, 0.0}) ==
              SketchCameraDecision::Accepted,
          "overlay fixture camera failed");
  auto preparedBase = prepareSketchScene(generated, item.requestedLod());
  auto basePacket =
      preparedBase ? preparedProductPacket(*preparedBase) : nullptr;
  require(basePacket && item.publishProducts(basePacket).has_value(),
          "overlay fixture base failed");
  const QImage baseFrame = renderer.render();
  const std::uint64_t baseBuilds = item.geometryBuildCount();
  const std::size_t baseUploadedBytes = item.gpuUploadMetrics().bytesUploaded;

  constexpr std::array roles{
      SketchOverlayRole::Hovered, SketchOverlayRole::Selected,
      SketchOverlayRole::Preview, SketchOverlayRole::Diagnostic};
  std::array<SketchOverlayRoleSetPtr, 4> roleSets;
  for (std::size_t index = 0U; index < roleSets.size(); ++index) {
    const std::array<SketchOverlayScope, 1> selected{{
        {line->entity, std::nullopt},
    }};
    const auto scopes = roles[index] == SketchOverlayRole::Selected
                            ? std::span<const SketchOverlayScope>{selected}
                            : std::span<const SketchOverlayScope>{};
    auto role = SketchOverlayRoleSet::create(generated, roles[index], scopes);
    require(role.has_value(), "overlay fixture role failed");
    roleSets[index] = std::move(*role);
  }
  auto overlayGeneration = SketchPresentationGeneration::create(1U);
  require(overlayGeneration.has_value(), "overlay fixture generation failed");
  auto overlay = SketchPresentationOverlay::create(
      generated, *overlayGeneration, roleSets);
  require(overlay.has_value(), "overlay fixture packet failed");
  auto source = std::make_shared<const SketchSceneProducts>(SketchSceneProducts{
      productStamp(sceneStamp.target, 2U, 2U), generated, *overlay, {}, {}});
  auto prepared =
      prepareSketchProducts(source, item.requestedLod(), {}, basePacket);
  require(prepared && (*prepared)->overlay() &&
              (*prepared)->overlay()->metrics().pointInstanceCount == 0U &&
              (*prepared)->overlay()->metrics().drawSpanCount > 0U &&
              item.publishProducts(*prepared).has_value(),
          "overlay fixture product failed");

  QImage selectedFrame;
  for (std::size_t frame = 0U; frame < 32U; ++frame) {
    selectedFrame = renderer.render();
    const auto current = item.presentedFrame();
    if (current && current->synchronized()->products() == *prepared)
      break;
  }
  const auto presented = item.presentedFrame();
  const Point2d start = generated->points()[line->firstPoint];
  const Point2d end = generated->points()[line->firstPoint + 1U];
  const QPointF center =
      presented ? presented->synchronized()->transform().toItem(
                      {(start.x + end.x) * 0.5, (start.y + end.y) * 0.5})
                : QPointF{};
  require(presented && presented->synchronized()->products() == *prepared,
          "overlay packet did not reach the exact render pass");
  require(!hasGreenNear(baseFrame, center, 3) &&
              hasGreenNear(selectedFrame, center, 3),
          "overlay span did not use its selected palette role");
  require(item.geometryBuildCount() == baseBuilds &&
              item.gpuUploadMetrics().bytesUploaded == baseUploadedBytes,
          "overlay span rebuilt or reuploaded base geometry");
}

void verifyCompleteProductRendering(OffscreenQuickRenderer &renderer) {
  const SceneStamp sceneStamp = stamp(39, 1, 39, 39, 39, 1);
  auto generated = scene(4, 97, sceneStamp);
  const PackedSketchPrimitive *line = nullptr;
  for (const PackedSketchPrimitive &primitive : generated->primitives())
    if (primitive.kind == SketchPrimitiveKind::Line) {
      line = &primitive;
      break;
    }
  require(line != nullptr, "complete product fixture has no line");

  SketchSceneItem item{renderer.window().contentItem()};
  item.setSize(renderSize);
  item.retarget(sceneStamp.target);
  SketchScenePalette colors;
  colors.regular = qRgb(224, 52, 68);
  colors.selected = qRgb(36, 214, 112);
  colors.preview = qRgb(210, 72, 224);
  colors.construction = qRgb(54, 116, 232);
  item.setPalette(colors);
  require(item.publishCamera({2, {}, 0.001, 0.0}) ==
              SketchCameraDecision::Accepted,
          "complete product fixture camera failed");
  auto preparedBase = prepareSketchScene(generated, item.requestedLod());
  auto basePacket =
      preparedBase ? preparedProductPacket(*preparedBase) : nullptr;
  require(basePacket && item.publishProducts(basePacket).has_value(),
          "complete product fixture base failed");
  const QImage baseFrame = renderer.render();

  constexpr std::array roles{
      SketchOverlayRole::Hovered, SketchOverlayRole::Selected,
      SketchOverlayRole::Preview, SketchOverlayRole::Diagnostic};
  std::array<SketchOverlayRoleSetPtr, 4> roleSets;
  for (std::size_t index = 0U; index < roleSets.size(); ++index) {
    const std::array<SketchOverlayScope, 1> selected{{
        {line->entity, sketch::PointKey::Start},
    }};
    const auto scopes = roles[index] == SketchOverlayRole::Selected
                            ? std::span<const SketchOverlayScope>{selected}
                            : std::span<const SketchOverlayScope>{};
    auto role = SketchOverlayRoleSet::create(generated, roles[index], scopes);
    require(role.has_value(), "complete product overlay role failed");
    roleSets[index] = std::move(*role);
  }
  auto overlayGeneration = SketchPresentationGeneration::create(1U);
  require(overlayGeneration.has_value(),
          "complete product overlay generation failed");
  auto overlay = SketchPresentationOverlay::create(
      generated, *overlayGeneration, roleSets);
  require(overlay.has_value(), "complete product overlay failed");

  auto edit = SketchEditSessionHandle::create(1U);
  auto tool = SketchToolInstanceHandle::create(1U);
  auto provisionalGeneration = SketchProvisionalGeneration::create(1U);
  auto provisionalHandle = SketchProvisionalPrimitiveHandle::create(1U);
  require(edit && tool && provisionalGeneration && provisionalHandle,
          "complete product provisional identity failed");
  const SketchProvisionalStamp provisionalStamp{
      {sceneStamp, *edit, *tool},
      *provisionalGeneration,
      digest<SketchProvisionalDigest>(39U)};
  const std::array<PackedSketchProvisionalPrimitive, 1> provisionalValues{{
      {*provisionalHandle,
       {{{-0.06, -0.07}, {0.06, -0.07}}},
       2U,
       SketchPrimitiveKind::Line,
       SketchProvisionalClassification::Regular,
       0.0,
       0.0,
       0.0},
  }};
  auto provisional =
      SketchProvisionalGeometry::create(provisionalStamp, provisionalValues);
  require(provisional.has_value(), "complete product provisional failed");

  auto markerGeneration = SketchMarkerGeneration::create(1U);
  auto firstMarker = SketchMarkerHandle::create(1U);
  auto secondMarker = SketchMarkerHandle::create(2U);
  require(markerGeneration && firstMarker && secondMarker,
          "complete product marker identity failed");
  const SketchMarkerStamp markerStamp{
      {sceneStamp, std::nullopt, std::nullopt, std::nullopt},
      *markerGeneration,
      digest<SketchMarkerDigest>(39U)};
  const std::array<SketchMarkerAnchor, 2> anchors{
      SketchCanonicalMarkerAnchor{{-0.05, 0.07}},
      SketchCanonicalMarkerAnchor{{0.05, 0.07}},
  };
  const std::array<PackedSketchMarker, 2> markerValues{{
      {*firstMarker, id<SketchConstraintId>(3901U), 0U, 1U,
       SketchMarkerKind::HorizontalConstraint, 0.0},
      {*secondMarker, id<SketchConstraintId>(3902U), 1U, 1U,
       SketchMarkerKind::VerticalConstraint, 0.0},
  }};
  auto markers = SketchMarkerPacket::create(markerStamp, generated, {}, anchors,
                                            markerValues);
  require(markers.has_value(), "complete product markers failed");

  auto source = std::make_shared<const SketchSceneProducts>(
      SketchSceneProducts{productStamp(sceneStamp.target, 2U, 2U), generated,
                          *overlay, *provisional, *markers});
  auto prepared =
      prepareSketchProducts(source, item.requestedLod(), {}, basePacket);
  require(prepared && (*prepared)->overlayPointMesh() &&
              (*prepared)->provisional() && (*prepared)->markerMesh() &&
              (*prepared)->metrics().overlayPointMeshRetainedBytes > 0U &&
              (*prepared)->metrics().markerMeshRetainedBytes > 0U &&
              item.publishProducts(*prepared).has_value(),
          "complete product preparation failed");

  QImage completeFrame;
  for (std::size_t frame = 0U; frame < 96U; ++frame) {
    completeFrame = renderer.render();
    const auto current = item.presentedFrame();
    if (current && current->synchronized()->products() == *prepared)
      break;
  }
  const auto presented = item.presentedFrame();
  require(presented && presented->synchronized()->products() == *prepared &&
              presented->overlayPointChunks() &&
              presented->overlayPointChunks()->size() > 0U &&
              presented->provisionalChunks() &&
              presented->provisionalChunks()->size() > 0U &&
              presented->markerChunks() &&
              presented->markerChunks()->size() > 0U,
          "complete product packet did not reach one exact render pass");
  const SketchViewTransform &transform = presented->synchronized()->transform();
  const QPointF selectedPoint =
      transform.toItem(generated->points()[line->firstPoint]);
  const QPointF provisionalCenter = transform.toItem({0.0, -0.07});
  const QPointF horizontalCenter = transform.toItem({-0.05, 0.07});
  const QPointF verticalCenter = transform.toItem({0.05, 0.07});
  require(!hasGreenNear(baseFrame, selectedPoint, 4) &&
              hasGreenNear(completeFrame, selectedPoint, 4),
          "selected semantic point was not drawn in its overlay role");
  require(!hasMagentaNear(baseFrame, provisionalCenter, 3) &&
              hasMagentaNear(completeFrame, provisionalCenter, 3),
          "complete packet provisional geometry was not drawn");
  require(
      hasBlueNear(completeFrame, horizontalCenter + QPointF{5.0, 0.0}, 1) &&
          !hasBlueNear(completeFrame, horizontalCenter + QPointF{0.0, 5.0},
                       1) &&
          hasBlueNear(completeFrame, verticalCenter + QPointF{0.0, 5.0}, 1) &&
          !hasBlueNear(completeFrame, verticalCenter + QPointF{5.0, 0.0}, 1),
      "constraint marker symbols were absent or visually identical");

  auto removedSource = productPacket(generated, 3U, 3U);
  auto removed = PreparedSketchProducts::create(removedSource, *preparedBase);
  require(removed && item.publishProducts(*removed).has_value(),
          "complete product removal failed");
  QImage removedFrame;
  for (std::size_t frame = 0U; frame < 96U; ++frame) {
    removedFrame = renderer.render();
    const auto current = item.presentedFrame();
    if (current && current->synchronized()->products() == *removed)
      break;
  }
  require(item.presentedFrame() &&
              item.presentedFrame()->synchronized()->products() == *removed &&
              !item.presentedFrame()->overlayPointChunks() &&
              !item.presentedFrame()->provisionalChunks() &&
              !item.presentedFrame()->markerChunks() &&
              !hasGreenNear(removedFrame, selectedPoint, 4) &&
              !hasMagentaNear(removedFrame, provisionalCenter, 3) &&
              !hasBlueNear(removedFrame, horizontalCenter, 6) &&
              !hasBlueNear(removedFrame, verticalCenter, 6),
          "removed product layers remained resident or visible");
}

void verifyProgressiveDisplayedCoverage(OffscreenQuickRenderer &renderer) {
  constexpr std::size_t primitiveCount = 900U;
  const SceneStamp sceneStamp = stamp(34, 1, 34, 34, 34, 1);
  const auto generated = scene(primitiveCount, 91, sceneStamp);
  SketchSceneItem item{renderer.window().contentItem()};
  item.setPalette(palette(qRgb(236, 112, 52)));
  item.retarget(sceneStamp.target);

  SketchUploadOptions upload;
  upload.maximumChunkBytes = 64U * 1024U;
  upload.spatialTileLogicalPixels = 8.0;
  const SketchCamera2d initialCamera{2, {-0.13, -0.13}, 0.0005, 0.0};
  item.setSize({160.0, 120.0});
  require(item.publishCamera(initialCamera) == SketchCameraDecision::Accepted,
          "displayed-coverage fixture camera failed");
  auto prepared = prepareSketchScene(generated,
                                     SketchCurveLod::forMetresPerLogicalPixel(
                                         initialCamera.metresPerLogicalPixel),
                                     {}, {}, upload);
  require(
      prepared && (*prepared)->mesh()->chunks().size() > 700U &&
          item.publishProducts(preparedProductPacket(*prepared)).has_value(),
      "displayed-coverage fixture did not create scalable culling");

  QImage previous;
  for (std::size_t frame = 0; frame < 256U; ++frame) {
    previous = renderer.render();
    if (item.presentedScene())
      break;
  }
  require(item.presentedScene() &&
              item.presentedScene()->transform().camera() == initialCamera,
          "displayed-coverage fixture did not reach its initial frame");

  struct Transition {
    SketchCamera2d camera;
    QSizeF viewport;
    std::size_t targetPrimitive;
    bool publishCamera;
    bool targetInitiallyInvisible;
  };
  const std::array transitions{
      Transition{{3, {}, 0.002, 0.0}, {300.0, 220.0}, 896U, true, true},
      Transition{
          {4, {-0.13, -0.13}, 0.0005, 0.0}, {160.0, 120.0}, 96U, true, false},
      Transition{
          {5, {0.13, 0.13}, 0.0005, 0.0}, {160.0, 120.0}, 776U, true, true},
      Transition{
          {5, {0.13, 0.13}, 0.0005, 0.0}, {280.0, 200.0}, 688U, false, true},
      Transition{
          {6, {0.1, 0.156}, 0.0004, 0.0}, {160.0, 120.0}, 864U, true, false},
  };

  for (const Transition &transition : transitions) {
    item.setSize(transition.viewport);
    if (transition.publishCamera)
      require(item.publishCamera(transition.camera) ==
                  SketchCameraDecision::Accepted,
              "generated displayed-coverage camera was rejected");
    const PackedSketchPrimitive &primitive =
        generated->primitives()[transition.targetPrimitive];
    require(primitive.kind == SketchPrimitiveKind::Point,
            "displayed-coverage target must be a point primitive");
    const Point2d target = generated->points()[primitive.firstPoint];
    auto desiredTransform =
        SketchViewTransform::create(transition.camera, transition.viewport);
    require(desiredTransform.has_value(),
            "generated displayed-coverage transform was rejected");
    const QPointF targetItem = desiredTransform->toItem(target);
    require(QRectF{QPointF{}, transition.viewport}.contains(targetItem),
            "generated displayed-coverage target escaped the viewport");

    std::size_t incoherentFrames = 0U;
    bool verifiedResponsiveTransform = false;
    bool verifiedInvisibleTarget = !transition.targetInitiallyInvisible;
    QImage converged;
    for (std::size_t frame = 0; frame < 256U; ++frame) {
      const QImage current = renderer.render();
      const auto synchronized = item.presentedScene();
      if (synchronized) {
        require(synchronized->scene()->stamp() == sceneStamp &&
                    synchronized->transform().camera() == transition.camera &&
                    synchronized->transform().viewportLogical() ==
                        transition.viewport,
                "native renderer exposed a mismatched displayed transform");
        converged = current;
        break;
      }

      ++incoherentFrames;
      const auto evidence = item.pick(targetItem, 6.0);
      require(!evidence && evidence.error().code ==
                               "desktop.sketch.missing-presented-frame",
              "incomplete resident coverage remained pickable");
      if (!verifiedResponsiveTransform) {
        require(differingPixels(previous, current) > 20U,
                "culling blocked the responsive camera transform");
        verifiedResponsiveTransform = true;
      }
      if (transition.targetInitiallyInvisible &&
          !hasForegroundNear(current, targetItem))
        verifiedInvisibleTarget = true;
    }
    require(incoherentFrames > 0U,
            "camera transition published incomplete coverage");
    require(verifiedResponsiveTransform,
            "camera transition blocked the responsive transform");
    require(verifiedInvisibleTarget,
            "camera transition exposed an unresident target");
    if (converged.isNull())
      throw std::runtime_error(
          "camera transition did not converge within its frame bound: " +
          std::to_string(transition.camera.generation));
    const auto evidence = item.pick(targetItem, 6.0);
    require(evidence && evidence->scene == sceneStamp &&
                evidence->cameraGeneration == transition.camera.generation &&
                hasForegroundNear(converged, targetItem),
            "coherent resident coverage did not publish exact picking");
    previous = std::move(converged);
  }

  const auto oldFrame = item.presentedScene();
  require(oldFrame &&
              oldFrame->transform().viewportLogical() == QSizeF{160.0, 120.0},
          "different-scene fixture did not reset the old viewport");
  auto prospectiveTransform = SketchViewTransform::create(
      oldFrame->transform().camera(), QSizeF{280.0, 200.0});
  const PackedSketchPrimitive &outsidePrimitive = generated->primitives()[896U];
  require(prospectiveTransform &&
              outsidePrimitive.kind == SketchPrimitiveKind::Point,
          "grown-viewport target must be a point primitive");
  const QPointF outsideOldViewport = prospectiveTransform->toItem(
      generated->points()[outsidePrimitive.firstPoint]);
  require(outsideOldViewport.x() > 160.0 && outsideOldViewport.x() < 280.0 &&
              outsideOldViewport.y() >= 0.0 && outsideOldViewport.y() < 200.0,
          "grown-viewport target did not isolate the newly added area");

  const SceneStamp replacementStamp = stamp(34, 2, 34, 34, 34, 2);
  auto replacement =
      prepareSketchScene(scene(primitiveCount, 92, replacementStamp),
                         item.requestedLod(), {}, {}, upload);
  item.setSize({280.0, 200.0});
  require(
      replacement &&
          item.publishProducts(preparedProductPacket(*replacement)).has_value(),
      "different-scene resize fixture was rejected");
  const std::uint64_t slicesBefore = item.gpuUploadMetrics().slices;
  QImage staging;
  for (std::size_t frame = 0; frame < 256U; ++frame) {
    staging = renderer.render();
    if (item.gpuUploadMetrics().slices > slicesBefore)
      break;
  }
  const auto retained = item.presentedScene();
  require(item.gpuUploadMetrics().slices > slicesBefore && retained &&
              retained->scene()->stamp() == sceneStamp &&
              retained->transform().viewportLogical() == QSizeF{160.0, 120.0} &&
              !hasForegroundNear(staging, outsideOldViewport),
          "different-scene staging lost the old coherent viewport");
  const auto outsideEvidence = item.pick(outsideOldViewport, 6.0);
  const auto unrestrictedEvidence = retained->pickIndex()->pick(
      {retained->transform().toCanonical(outsideOldViewport), 0.008,
       SketchPickTargets::All});
  require(!outsideEvidence &&
              outsideEvidence.error().code ==
                  "desktop.sketch.pick-outside-presented-viewport" &&
              unrestrictedEvidence && unrestrictedEvidence->has_value(),
          "grown item area picked through the old resident viewport");
}

void verifyPickCoverageEdges(OffscreenQuickRenderer &renderer) {
  // Point primitives have base glyphs. Center and endpoint point targets need
  // exact displayed-marker evidence before they can be independent edge
  // acceptance oracles.
  const SceneStamp sceneStamp = stamp(35, 1, 35, 35, 35, 1);
  std::vector<Point2d> points{{0.082, -0.04}, {0.09, 0.0}, {0.11, 0.04}};
  std::vector<PackedSketchPrimitive> primitives;
  for (std::uint32_t index = 0; index < points.size(); ++index)
    primitives.push_back(
        {id<SketchEntityId>(350U + index),
         *SketchPrimitiveHandle::create(index + 1U), index, 0,
         SketchPrimitiveKind::Point,
         SketchPrimitiveFlags::Visible | SketchPrimitiveFlags::Selectable, 0.0,
         0.0, 0.0});
  auto snapshot = SketchSceneSnapshot::create(sceneStamp, styles(), points,
                                              std::move(primitives));
  require(snapshot.has_value(), "pick coverage edge scene was invalid");
  auto generated =
      std::make_shared<const SketchSceneSnapshot>(std::move(*snapshot));

  SketchSceneItem item{renderer.window().contentItem()};
  item.setSize({160.0, 120.0});
  item.retarget(sceneStamp.target);
  item.setPalette(palette(qRgb(228, 144, 52)));
  const SketchCamera2d initialCamera{2, {}, 0.001, 0.0};
  require(item.publishCamera(initialCamera) == SketchCameraDecision::Accepted,
          "pick coverage edge camera was rejected");
  SketchUploadOptions upload;
  upload.spatialTileLogicalPixels = 8.0;
  auto prepared = prepareSketchScene(
      generated, SketchCurveLod::forMetresPerLogicalPixel(0.001), {}, {},
      upload);
  require(
      prepared &&
          item.publishProducts(preparedProductPacket(*prepared)).has_value(),
      "pick coverage edge scene was rejected");
  QImage initial;
  for (std::size_t frame = 0; frame < 32U; ++frame) {
    initial = renderer.render();
    if (item.presentedScene())
      break;
  }
  const auto synchronized = item.presentedScene();
  require(synchronized != nullptr,
          "pick coverage scene did not reach a coherent frame");
  const auto resident =
      (*prepared)->mesh()->visibleChunks(synchronized->transform());
  const auto primitiveResident = [&](std::uint32_t handle) {
    const auto &index = (*prepared)->primitiveTessellationIndex();
    const auto *entry = index->find(*SketchPrimitiveHandle::create(handle));
    return entry && std::ranges::all_of(index->spans(entry->primitive),
                                        [&](const auto &span) {
                                          return std::ranges::binary_search(
                                              resident->chunks, span.chunk);
                                        });
  };
  require(resident &&
              synchronized->pickCoverage() == SketchPickCoveragePolicy{} &&
              primitiveResident(1U) && primitiveResident(2U) &&
              !primitiveResident(3U) &&
              item.gpuUploadMetrics().maximumPresentedChunks <
                  (*prepared)->mesh()->chunks().size(),
          "pick coverage evidence or culling bound was not synchronized");

  const QPointF visibleCenter = synchronized->transform().toItem(points[0]);
  const QPointF visibleQuery{visibleCenter.x() - 15.0, visibleCenter.y()};
  const auto maximumAccepted =
      item.pick(visibleQuery, 16.0, SketchPickTargets::Points);
  require(maximumAccepted && maximumAccepted->hit &&
              maximumAccepted->hit->entity == id<SketchEntityId>(350U),
          "maximum bounded tolerance rejected visible edge geometry");

  const QPointF invisibleCenter = synchronized->transform().toItem(points[1]);
  const QPointF invisibleQuery{invisibleCenter.x() - 15.0, invisibleCenter.y()};
  const auto rawInvisible = synchronized->pickIndex()->pick(
      {synchronized->transform().toCanonical(invisibleQuery), 0.016,
       SketchPickTargets::Points});
  const auto filteredInvisible =
      item.pick(invisibleQuery, 16.0, SketchPickTargets::Points);
  require(rawInvisible && *rawInvisible && filteredInvisible &&
              !filteredInvisible->hit &&
              !hasForegroundNear(initial, {159.0, invisibleCenter.y()}, 1),
          "clipped offscreen geometry remained an invisible hit");

  const QPointF nonresidentCenter = synchronized->transform().toItem(points[2]);
  const QPointF aboveBoundQuery{nonresidentCenter.x() - 31.0,
                                nonresidentCenter.y()};
  const auto rawNonresident = synchronized->pickIndex()->pick(
      {synchronized->transform().toCanonical(aboveBoundQuery), 0.032,
       SketchPickTargets::Points});
  const auto aboveBound =
      item.pick(aboveBoundQuery, 31.0, SketchPickTargets::Points);
  const auto negative = item.pick(visibleQuery, -1.0);
  const auto nonFinite =
      item.pick(visibleQuery, std::numeric_limits<double>::quiet_NaN());
  require(rawNonresident && *rawNonresident && !aboveBound && !negative &&
              !nonFinite &&
              aboveBound.error().code ==
                  "desktop.sketch.pick-tolerance-policy" &&
              negative.error().code == "desktop.sketch.pick-tolerance-policy" &&
              nonFinite.error().code == "desktop.sketch.pick-tolerance-policy",
          "out-of-policy pick tolerance reached nonresident geometry");

  const auto baseMesh = synchronized->mesh();
  const auto basePick = synchronized->pickIndex();
  require(item.publishPickCoverage({2, 0.0}) ==
              SketchPickCoverageDecision::Accepted,
          "pick coverage policy transition was rejected");
  item.setSize({200.0, 160.0});
  require(item.publishCamera({3, {0.01, 0.0}, 0.001, 0.0}) ==
              SketchCameraDecision::Accepted,
          "pick coverage edge camera transition was rejected");
  for (std::size_t frame = 0; frame < 64U; ++frame) {
    static_cast<void>(renderer.render());
    const auto current = item.presentedScene();
    if (current && current->pickCoverage().generation == 2U &&
        current->transform().camera().generation == 3U &&
        current->transform().viewportLogical() == QSizeF{200.0, 160.0})
      break;
  }
  const auto transitioned = item.presentedScene();
  require(
      transitioned &&
          transitioned->pickCoverage() == SketchPickCoveragePolicy{2, 0.0} &&
          transitioned->transform().camera().generation == 3U &&
          transitioned->transform().viewportLogical() == QSizeF{200.0, 160.0} &&
          transitioned->mesh() == baseMesh &&
          transitioned->pickIndex() == basePick,
      "pick policy, camera, or resize transition rebuilt base state");
}

void verifyNativeRender(OffscreenQuickRenderer &renderer) {
  const SceneStamp sceneStamp = stamp(10, 1, 10, 10, 10, 1);
  auto generated = scene(12, 71, sceneStamp);
  SketchSceneItem item{renderer.window().contentItem()};
  item.setSize(renderSize);
  item.retarget(sceneStamp.target);
  item.setPalette(palette(qRgb(224, 48, 64)));
  require(item.publishCamera({2, {}, 0.0005, 0.0}) ==
              SketchCameraDecision::Accepted,
          "native render initial camera failed");
  SketchUploadOptions initialUploadOptions;
  initialUploadOptions.maximumChunkBytes = 16U * 1024U;
  initialUploadOptions.spatialTileLogicalPixels = 128.0;
  auto prepared = prepareSketchScene(
      generated, SketchCurveLod::forMetresPerLogicalPixel(0.0005), {}, {},
      initialUploadOptions);
  require(
      prepared &&
          item.publishProducts(preparedProductPacket(*prepared)).has_value(),
      "native render scene publication failed");

  const QImage initial = renderer.render();
  const PixelSummary initialPixels = summarize(initial);
  std::cout << "initial pixels=" << initialPixels.pixels
            << " bounds=" << initialPixels.bounds.x() << ','
            << initialPixels.bounds.y() << ',' << initialPixels.bounds.width()
            << ',' << initialPixels.bounds.height()
            << " builds=" << item.geometryBuildCount()
            << " vertices=" << item.meshMetrics().vertices
            << " diagnostic=" << item.lastDiagnostic().code << '\n';
  require(initialPixels.pixels > 100U && initialPixels.bounds.isValid(),
          "baked sketch shaders rendered only the background");
  require(initialPixels.bounds.left() > 30 &&
              initialPixels.bounds.right() < renderSize.width() - 30 &&
              initialPixels.bounds.top() > 20 &&
              initialPixels.bounds.bottom() < renderSize.height() - 20 &&
              initialPixels.bounds.width() > 20 &&
              initialPixels.bounds.height() > 20,
          "rendered sketch geometry escaped its expected finite region");
  require(initialPixels.red > initialPixels.green * 2U &&
              item.geometryBuildCount() == 1U && item.meshMetrics().bytes > 0U,
          "initial material or geometry upload evidence is invalid");
  require(item.synchronizationMetrics().scalablePreparations == 0U,
          "native render synchronization performed scalable preparation");
  const SketchGpuUploadMetrics initialUpload = item.gpuUploadMetrics();
  require(initialUpload.slices == 1U && initialUpload.coherentSwaps == 1U &&
              initialUpload.rejectedPackets == 0U &&
              (*prepared)->mesh()->chunks().size() > 1U &&
              initialUpload.bytesUploaded == item.meshMetrics().bytes &&
              initialUpload.maximumSliceBytes <=
                  SketchGpuUploadPolicy::maximumBytesPerFrame &&
              initialUpload.maximumSliceChunks <=
                  SketchGpuUploadPolicy::maximumChunksPerFrame &&
              initialUpload.maximumPresentedChunks ==
                  (*prepared)->mesh()->chunks().size() &&
              initialUpload.maximumPresentedReferencedBytes ==
                  item.meshMetrics().bytes,
          "native render GPU upload accounting is invalid");

  require(item.publishCamera({3, {0.015, -0.004}, 0.0005, 0.43}) ==
              SketchCameraDecision::Accepted,
          "native render moved camera failed");
  const QImage moved = renderer.render();
  const PixelSummary movedPixels = summarize(moved);
  const double centroidMotion =
      std::hypot(movedPixels.centroid.x() - initialPixels.centroid.x(),
                 movedPixels.centroid.y() - initialPixels.centroid.y());
  require(movedPixels.pixels > 100U && centroidMotion > 10.0 &&
              differingPixels(initial, moved) > 200U &&
              item.geometryBuildCount() == 1U &&
              item.gpuUploadMetrics().slices == initialUpload.slices &&
              item.gpuUploadMetrics().chunksUploaded ==
                  initialUpload.chunksUploaded &&
              item.gpuUploadMetrics().bytesUploaded ==
                  initialUpload.bytesUploaded,
          "camera transform did not move retained native geometry");

  QImage cameraFrame = moved;
  PixelSummary cameraPixels = movedPixels;
  for (std::uint64_t generation = 4U; generation < 10U; ++generation) {
    const double amount = static_cast<double>(generation - 3U);
    require(item.publishCamera(
                {generation,
                 {0.015 + amount * 0.0002, -0.004 + amount * 0.0001},
                 0.0005,
                 0.43 + amount * 0.015}) == SketchCameraDecision::Accepted,
            "repeated native camera publication failed");
    const QImage next = renderer.render();
    const PixelSummary nextPixels = summarize(next);
    const SketchGpuUploadMetrics cameraUpload = item.gpuUploadMetrics();
    require(differingPixels(cameraFrame, next) > 20U &&
                nextPixels.pixels > 100U &&
                item.presentedScene()->transform().camera().generation ==
                    generation &&
                item.geometryBuildCount() == 1U &&
                cameraUpload.slices == initialUpload.slices &&
                cameraUpload.chunksUploaded == initialUpload.chunksUploaded &&
                cameraUpload.bytesUploaded == initialUpload.bytesUploaded &&
                cameraUpload.maximumVisibilityNodesPerFrame <=
                    SketchGpuUploadPolicy::maximumSpatialNodesPerFrame &&
                cameraUpload.maximumVisibleChunksSelectedPerFrame <=
                    SketchGpuUploadPolicy::maximumVisibleChunksPerFrame,
            "repeated camera updates rebuilt or lagged retained geometry");
    cameraFrame = next;
    cameraPixels = nextPixels;
  }

  item.setPalette(palette(qRgb(40, 214, 96)));
  const QImage recolored = renderer.render();
  const PixelSummary recoloredPixels = summarize(recolored);
  require(recoloredPixels.green > recoloredPixels.red * 2U &&
              recoloredPixels.bounds == cameraPixels.bounds &&
              item.geometryBuildCount() == 1U &&
              item.gpuUploadMetrics().slices == 1U &&
              item.gpuUploadMetrics().chunksUploaded ==
                  initialUpload.chunksUploaded &&
              item.gpuUploadMetrics().bytesUploaded ==
                  initialUpload.bytesUploaded &&
              differingPixels(cameraFrame, recolored) > 100U,
          "palette uniforms did not recolor retained native geometry");

  require(item.publishCamera({10, {}, 0.001, 0.5}) ==
              SketchCameraDecision::Accepted,
          "progressive reference camera failed");
  const QImage progressiveReference = renderer.render();
  require(item.geometryBuildCount() == 1U &&
              item.gpuUploadMetrics().slices == initialUpload.slices &&
              differingPixels(recolored, progressiveReference) > 100U,
          "progressive reference camera rebuilt retained geometry");

  auto progressive =
      prepareSketchScene(scene(11'000, 72, stamp(10, 2, 10, 10, 10, 2)),
                         item.requestedLod(), {}, {}, {}, *prepared);
  require(progressive &&
              (*progressive)->mesh()->metrics().bytes > 16U * 1024U * 1024U &&
              item.publishProducts(preparedProductPacket(*progressive)) ==
                  PreparedSketchSceneOffer{
                      PreparedSketchSceneDecision::Accepted, false},
          "progressive GPU upload fixture was invalid");
  for (std::uint64_t generation = 11U; generation <= 13U; ++generation) {
    const std::uint64_t slicesBefore = item.gpuUploadMetrics().slices;
    bool stagingStarted = false;
    for (std::size_t frame = 0; frame < 64U; ++frame) {
      static_cast<void>(renderer.render());
      if (item.gpuUploadMetrics().slices > slicesBefore) {
        stagingStarted = true;
        break;
      }
    }
    require(stagingStarted,
            "camera-churn fixture did not reach progressive staging");
    require(
        item.publishCamera(
            {generation,
             {0.016 + static_cast<double>(generation - 10U) * 0.0001, -0.003},
             0.001,
             0.5}) == SketchCameraDecision::Accepted,
        "camera-churn publication failed");
    const QImage canceled = renderer.render();
    const SketchGpuUploadMetrics churn = item.gpuUploadMetrics();
    require(churn.canceledStagings == generation - 10U &&
                churn.maximumRetiredLayers > 0U &&
                churn.maximumRetiredLayers <=
                    SketchGpuUploadPolicy::maximumRetiredLayers &&
                churn.maximumRetiredChunks <=
                    SketchGpuUploadPolicy::maximumChunksPerFrame &&
                churn.maximumNodeOperationsPerFrame <=
                    SketchGpuUploadPolicy::maximumChunksPerFrame &&
                item.presentedScene()->scene()->stamp() == sceneStamp &&
                differingPixels(progressiveReference, canceled) <= 4U,
            "repeated staging cancellation escaped bounded backpressure");
  }
  bool preservedOldFrame = false;
  QImage converged;
  for (std::size_t frame = 0; frame < 128U; ++frame) {
    const QImage staged = renderer.render();
    const auto synchronized = item.presentedScene();
    require(static_cast<bool>(synchronized),
            "progressive native frame lost picking evidence");
    const SketchGpuUploadMetrics progress = item.gpuUploadMetrics();
    require(progress.maximumSliceBytes <=
                    SketchGpuUploadPolicy::maximumBytesPerFrame &&
                progress.maximumSliceChunks <=
                    SketchGpuUploadPolicy::maximumChunksPerFrame &&
                progress.maximumNodeOperationsPerFrame <=
                    SketchGpuUploadPolicy::maximumChunksPerFrame &&
                progress.maximumVisibilityNodesPerFrame <=
                    SketchGpuUploadPolicy::maximumSpatialNodesPerFrame &&
                progress.maximumVisibleChunksSelectedPerFrame <=
                    SketchGpuUploadPolicy::maximumVisibleChunksPerFrame,
            "native upload slice exceeded its byte or node ceiling");
    if (synchronized->scene()->stamp() == sceneStamp) {
      preservedOldFrame = true;
      auto evidence = item.pick({150.0, 100.0}, 4.0);
      require(evidence &&
                  evidence->latestAcceptedScene == (*progressive)->stamp() &&
                  !evidence->matchesLatestAcceptedScene &&
                  differingPixels(progressiveReference, staged) <= 4U,
              "staged same-target content became visible or editable early");
      continue;
    }
    require(synchronized->scene()->stamp() == (*progressive)->stamp(),
            "progressive native upload converged to the wrong stamp");
    converged = staged;
    break;
  }
  const SketchGpuUploadMetrics convergedUpload = item.gpuUploadMetrics();
  require(
      preservedOldFrame && !converged.isNull() &&
          item.geometryBuildCount() == 2U &&
          convergedUpload.coherentSwaps == 2U &&
          convergedUpload.slices > initialUpload.slices &&
          convergedUpload.maximumStagingChunks > 0U &&
          convergedUpload.maximumStagingReferencedBytes >
              SketchGpuUploadPolicy::maximumBytesPerFrame &&
          convergedUpload.maximumRetiredChunks > 0U &&
          convergedUpload.maximumRetiredLayers > 0U &&
          convergedUpload.maximumRetiredLayers <=
              SketchGpuUploadPolicy::maximumRetiredLayers &&
          convergedUpload.maximumRetiredReferencedBytes > 0U &&
          differingPixels(progressiveReference, converged) > 100U,
      "progressive native upload did not preserve then coherently converge");

  SketchUploadOptions invalidUpload;
  invalidUpload.maximumChunkBytes = 4U * 1024U * 1024U;
  invalidUpload.spatialTileLogicalPixels = 1.0e9;
  auto invalid =
      prepareSketchScene(scene(15'000, 73, stamp(10, 3, 10, 10, 10, 3)),
                         item.requestedLod(), {}, {}, invalidUpload);
  require(
      invalid &&
          std::ranges::any_of((*invalid)->mesh()->chunks(),
                              [](const auto &chunk) {
                                return chunk->payloadBytes() >
                                       SketchGpuUploadPolicy::maximumChunkBytes;
                              }) &&
          item.publishProducts(preparedProductPacket(*invalid)) ==
              PreparedSketchSceneOffer{PreparedSketchSceneDecision::Accepted,
                                       false},
      "invalid upload chunk fixture was not constructed");
  const QImage afterRejectedPacket = renderer.render();
  const SketchGpuUploadMetrics rejectedMetrics = item.gpuUploadMetrics();
  require(item.lastDiagnostic().code ==
                  "desktop.sketch.gpu-upload-chunk-invariant" &&
              item.presentedScene()->scene()->stamp() ==
                  (*progressive)->stamp() &&
              rejectedMetrics.rejectedPackets == 1U &&
              rejectedMetrics.cpuReclaimsAccepted >= 1U &&
              rejectedMetrics.maximumCpuReclaimQueue <= 32U &&
              rejectedMetrics.cpuReclaimsOutstanding <=
                  rejectedMetrics.cpuReclaimsAccepted &&
              differingPixels(converged, afterRejectedPacket) <= 4U,
          "invalid upload packet corrupted the coherent presented root");

  constexpr double extreme = std::numeric_limits<double>::max() * 0.75;
  require(item.publishCamera({100, {extreme, -extreme}, 0.0005, 0.0}) ==
              SketchCameraDecision::Accepted,
          "extreme finite camera publication failed");
  const QImage afterRejectedProjection = renderer.render();
  const auto synchronized = item.presentedScene();
  require(
      item.lastDiagnostic().code == "desktop.sketch.unrepresentable-gpu-view" &&
          synchronized &&
          synchronized->transform().camera().generation == 13U &&
          item.geometryBuildCount() == 2U &&
          differingPixels(afterRejectedPacket, afterRejectedProjection) <= 4U,
      "rejected extreme projection corrupted the last valid frame");
}

} // namespace

int main(int argc, char *argv[]) {
  const auto backend = parseBackend(argc, argv);
  if (!backend) {
    std::cerr << "usage: sketch_scene_native_render --backend "
                 "opengl|vulkan|metal|d3d11|d3d12\n";
    return 2;
  }
  QQuickWindow::setGraphicsApi(backend->api);
  QGuiApplication application(argc, argv);

#if QT_CONFIG(vulkan)
  std::unique_ptr<QVulkanInstance> vulkan;
  if (backend->api == QSGRendererInterface::Vulkan) {
    vulkan = std::make_unique<QVulkanInstance>();
    if (!vulkan->create()) {
      std::cout << "SKIP backend=" << backend->name.toStdString()
                << " diagnostic=QVulkanInstance::create failed\n";
      return backendUnavailable;
    }
  }
#endif

  try {
    QString driver;
    {
      OffscreenQuickRenderer renderer;
#if QT_CONFIG(vulkan)
      if (vulkan)
        renderer.window().setVulkanInstance(vulkan.get());
#endif
      QString failure;
      if (!renderer.initialize(failure)) {
        std::cout << "SKIP backend=" << backend->name.toStdString()
                  << " diagnostic=" << failure.toStdString() << '\n';
        return backendUnavailable;
      }
      require(renderer.window().rendererInterface()->graphicsApi() ==
                  backend->api,
              "Qt initialized a different graphics backend than requested");
      verifyNativeRender(renderer);
      verifyProgressiveDisplayedCoverage(renderer);
      verifyPickCoverageEdges(renderer);
      verifyPatternPixelsMatchPicking(renderer);
      verifyProvisionalGeometry(renderer);
      verifyOverlaySpans(renderer);
      verifyCompleteProductRendering(renderer);
      verifyIndependentControlLifecycle(renderer);
      verifyDeviceLossLifecycle(renderer);
      driver = renderer.driver();
    }
    require(shutdownSketchSceneResources(std::chrono::seconds{5}),
            "application render-resource reclamation did not drain");
    std::cout << "verified native sketch scene backend="
              << backend->name.toStdString()
              << " driver=" << driver.toStdString() << '\n';
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "backend=" << backend->name.toStdString()
              << " failure=" << error.what() << '\n';
    return 1;
  }
}
