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
#include <QSurfaceFormat>
#if QT_CONFIG(vulkan)
#include <QVulkanInstance>
#endif
#include <rhi/qrhi.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using namespace kearne;
using namespace kearne::render;
using namespace kearne::ui;
using namespace kearne::ui::test;

constexpr int backendUnavailable = 77;
constexpr QSize renderSize{480, 360};
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

  ~OffscreenQuickRenderer() {
    window_.setRenderTarget({});
    if (initialized_)
      control_.invalidate();
  }

  QQuickWindow &window() { return window_; }

  bool initialize(QString &failure) {
    if (!control_.initialize()) {
      failure = QStringLiteral("QQuickRenderControl initialization failed");
      return false;
    }
    initialized_ = true;
    rhi_ = control_.rhi();
    if (!rhi_) {
      failure = QStringLiteral("scene graph has no QRhi");
      return false;
    }
    texture_.reset(rhi_->newTexture(QRhiTexture::RGBA8, renderSize, 1,
                                    QRhiTexture::RenderTarget |
                                        QRhiTexture::UsedAsTransferSource));
    depthStencil_.reset(
        rhi_->newRenderBuffer(QRhiRenderBuffer::DepthStencil, renderSize, 1));
    if (!texture_ || !depthStencil_ || !texture_->create() ||
        !depthStencil_->create()) {
      failure = QStringLiteral("offscreen attachments could not be created");
      return false;
    }
    QRhiTextureRenderTargetDescription description{
        QRhiColorAttachment{texture_.get()}};
    description.setDepthStencilBuffer(depthStencil_.get());
    target_.reset(rhi_->newTextureRenderTarget(description));
    if (!target_) {
      failure = QStringLiteral("offscreen target allocation failed");
      return false;
    }
    pass_.reset(target_->newCompatibleRenderPassDescriptor());
    target_->setRenderPassDescriptor(pass_.get());
    if (!pass_ || !target_->create()) {
      failure = QStringLiteral("offscreen target creation failed");
      return false;
    }
    QQuickRenderTarget quickTarget =
        QQuickRenderTarget::fromRhiRenderTarget(target_.get());
    quickTarget.setDevicePixelRatio(1.0);
    window_.setRenderTarget(quickTarget);
    return true;
  }

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
    control_.commandBuffer()->resourceUpdate(updates);
    control_.endFrame();
    require(readback.pixelSize == renderSize && !readback.data.isEmpty(),
            "offscreen readback failed");
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
  QQuickRenderControl control_;
  QQuickWindow window_;
  QRhi *rhi_ = nullptr;
  std::unique_ptr<QRhiTexture> texture_;
  std::unique_ptr<QRhiRenderBuffer> depthStencil_;
  std::unique_ptr<QRhiTextureRenderTarget> target_;
  std::unique_ptr<QRhiRenderPassDescriptor> pass_;
  bool initialized_ = false;
};

bool foreground(QRgb pixel) {
  return std::max({std::abs(qRed(pixel) - background.red()),
                   std::abs(qGreen(pixel) - background.green()),
                   std::abs(qBlue(pixel) - background.blue())}) > 12;
}

bool colorNear(const QImage &image, QPointF point, QColor expected,
               int radius = 5) {
  const QImage pixels = image.convertToFormat(QImage::Format_ARGB32);
  for (int y = std::max(0, qRound(point.y()) - radius);
       y <= std::min(pixels.height() - 1, qRound(point.y()) + radius); ++y) {
    const auto *row = reinterpret_cast<const QRgb *>(pixels.constScanLine(y));
    for (int x = std::max(0, qRound(point.x()) - radius);
         x <= std::min(pixels.width() - 1, qRound(point.x()) + radius); ++x) {
      const QColor actual = QColor::fromRgba(row[x]);
      if (std::abs(actual.red() - expected.red()) < 36 &&
          std::abs(actual.green() - expected.green()) < 36 &&
          std::abs(actual.blue() - expected.blue()) < 36)
        return true;
    }
  }
  return false;
}

std::size_t foregroundPixels(const QImage &image) {
  const QImage pixels = image.convertToFormat(QImage::Format_ARGB32);
  std::size_t count = 0U;
  for (int y = 0; y < pixels.height(); ++y) {
    const auto *row = reinterpret_cast<const QRgb *>(pixels.constScanLine(y));
    for (int x = 0; x < pixels.width(); ++x)
      count += foreground(row[x]) ? 1U : 0U;
  }
  return count;
}

std::size_t foregroundPixelsInsideEllipse(const QImage &image, QPointF center,
                                          double radiusX, double radiusY,
                                          double maximumNormalizedSquared) {
  const QImage pixels = image.convertToFormat(QImage::Format_ARGB32);
  std::size_t count = 0U;
  for (int y = std::max(0, qFloor(center.y() - radiusY));
       y <= std::min(pixels.height() - 1, qCeil(center.y() + radiusY)); ++y) {
    const auto *row = reinterpret_cast<const QRgb *>(pixels.constScanLine(y));
    for (int x = std::max(0, qFloor(center.x() - radiusX));
         x <= std::min(pixels.width() - 1, qCeil(center.x() + radiusX)); ++x) {
      const double normalizedX = (x - center.x()) / radiusX;
      const double normalizedY = (y - center.y()) / radiusY;
      if (normalizedX * normalizedX + normalizedY * normalizedY <=
              maximumNormalizedSquared &&
          foreground(row[x]))
        ++count;
    }
  }
  return count;
}

std::shared_ptr<const SketchSceneSnapshot> nativeScene() {
  const SceneStamp sceneStamp = stamp(70, 1, 70, 70, 70, 1);
  auto line = SketchPrimitiveHandle::create(1U);
  auto circle = SketchPrimitiveHandle::create(2U);
  auto spline = SketchPrimitiveHandle::create(3U);
  auto linearSpline = SketchPrimitiveHandle::create(4U);
  require(line && circle && spline && linearSpline,
          "native render handles failed");
  SketchPrimitiveBatch batch;
  batch.points = {{-0.06, 0.0}, {0.06, 0.0}, {0.0, 0.035}};
  batch.primitives = {
      {id<SketchEntityId>(701U), *line, 0U, 0U, SketchPrimitiveKind::Line,
       SketchPrimitiveFlags::Visible | SketchPrimitiveFlags::Selectable},
      {id<SketchEntityId>(702U), *circle, 2U, 0U, SketchPrimitiveKind::Circle,
       SketchPrimitiveFlags::Visible | SketchPrimitiveFlags::Selectable, 0.018},
      {id<SketchEntityId>(703U), *spline, 3U, 0U, SketchPrimitiveKind::BSpline,
       SketchPrimitiveFlags::Visible | SketchPrimitiveFlags::Selectable},
      {id<SketchEntityId>(704U), *linearSpline, 3U, 4U,
       SketchPrimitiveKind::BSpline,
       SketchPrimitiveFlags::Visible | SketchPrimitiveFlags::Selectable}};
  batch.primitives[2].spline = 0U;
  batch.primitives[3].spline = 1U;
  batch.splineControlPointCoordinates = {0.04,  -0.06, 0.04, -0.02, 0.0,
                                         -0.02, 0.02,  0.06, 0.06,  0.06};
  batch.splineKnots = {0.0, 0.0, 0.0, 1.0, 1.0, 1.0, 0.0, 0.0, 1.0, 1.0};
  batch.splineWeights = {1.0, std::numbers::sqrt2 / 2.0, 1.0, 0.5, 2.0};
  batch.splines = {{0U, 3U, 0U, 0U, 2U, false}, {3U, 2U, 6U, 3U, 1U, false}};
  auto created =
      SketchSceneSnapshot::create(sceneStamp, styles(), std::move(batch));
  require(created.has_value(), "native render scene failed");
  return std::make_shared<const SketchSceneSnapshot>(std::move(*created));
}

std::shared_ptr<const SketchPresentationOverlay>
selectedLine(const std::shared_ptr<const SketchSceneSnapshot> &base) {
  constexpr std::array roles{
      SketchOverlayRole::Hovered, SketchOverlayRole::Selected,
      SketchOverlayRole::Preview, SketchOverlayRole::Diagnostic};
  std::array<SketchOverlayRoleSetPtr, 4> sets;
  for (std::size_t index = 0U; index < sets.size(); ++index) {
    std::vector<SketchOverlayScope> scopes;
    if (roles[index] == SketchOverlayRole::Selected)
      scopes.push_back({base->primitives().front().entity, std::nullopt});
    auto created = SketchOverlayRoleSet::create(base, roles[index], scopes);
    require(created.has_value(), "selected overlay role failed");
    sets[index] = std::move(*created);
  }
  auto generation = SketchPresentationGeneration::create(1U);
  require(generation.has_value(), "selected overlay generation failed");
  auto created = SketchPresentationOverlay::create(base, *generation, sets);
  require(created.has_value(), "selected overlay failed");
  return std::move(*created);
}

std::shared_ptr<const SketchProvisionalGeometry>
provisionalCircle(const std::shared_ptr<const SketchSceneSnapshot> &base) {
  auto edit = SketchEditSessionHandle::create(1U);
  auto tool = SketchToolInstanceHandle::create(1U);
  auto generation = SketchProvisionalGeneration::create(1U);
  auto circleHandle = SketchProvisionalPrimitiveHandle::create(1U);
  auto splineHandle = SketchProvisionalPrimitiveHandle::create(2U);
  auto ellipseHandle = SketchProvisionalPrimitiveHandle::create(3U);
  auto ellipticalArcHandle = SketchProvisionalPrimitiveHandle::create(4U);
  require(edit && tool && generation && circleHandle && splineHandle &&
              ellipseHandle && ellipticalArcHandle,
          "provisional identity fixture failed");
  SketchProvisionalBatch batch;
  batch.primitives = {{*circleHandle,
                       {{{-0.035, -0.035}, {}}},
                       1U,
                       SketchPrimitiveKind::Circle,
                       SketchProvisionalClassification::Regular,
                       0.012},
                      {*splineHandle,
                       {},
                       0U,
                       SketchPrimitiveKind::BSpline,
                       SketchProvisionalClassification::Regular},
                      {*ellipseHandle,
                       {{{0.08, 0.035}, {}}},
                       1U,
                      SketchPrimitiveKind::Ellipse,
                      SketchProvisionalClassification::Regular,
                       0.03},
                      {*ellipticalArcHandle,
                       {{{-0.08, 0.02}, {}}},
                       1U,
                       SketchPrimitiveKind::EllipticalArc,
                       SketchProvisionalClassification::Regular,
                       0.028}};
  batch.primitives[1].spline = 0U;
  batch.primitives[2].secondaryRadius = 0.018;
  batch.primitives[3].startAngleRadians =
      40.0 * std::numbers::pi / 180.0;
  batch.primitives[3].sweepAngleRadians =
      230.0 * std::numbers::pi / 180.0;
  batch.primitives[3].secondaryRadius = 0.017;
  batch.splineControlPointCoordinates = {-0.03, 0.06, -0.03, 0.08, -0.05, 0.08};
  batch.splineKnots = {0.0, 0.0, 0.0, 1.0, 1.0, 1.0};
  batch.splineWeights = {1.0, std::numbers::sqrt2 / 2.0, 1.0};
  batch.splines = {{0U, 3U, 0U, 0U, 2U, false}};
  const SketchProvisionalStamp draftStamp{
      {base->stamp(), *edit, *tool},
      *generation,
      digest<SketchProvisionalDigest>(711U)};
  auto created =
      SketchProvisionalGeometry::create(draftStamp, std::move(batch));
  require(created.has_value(), "provisional circle fixture failed");
  return std::move(*created);
}

std::shared_ptr<const SketchMarkerPacket>
degreeLabel(const std::shared_ptr<const SketchSceneSnapshot> &base) {
  auto generation = SketchMarkerGeneration::create(1U);
  auto handle = SketchMarkerHandle::create(1U);
  require(generation && handle, "native label identity fixture failed");
  const std::array<SketchMarkerAnchor, 1> anchors{
      SketchCanonicalMarkerAnchor{{0.0, -0.07}}};
  const std::array<PackedSketchMarker, 1> markers{{
      {*handle, std::nullopt, 0U, 1U, SketchMarkerKind::SplineDegreeLabel,
       3.0},
  }};
  auto created = SketchMarkerPacket::create(
      {{base->stamp(), std::nullopt, std::nullopt, std::nullopt}, *generation,
       digest<SketchMarkerDigest>(713U)},
      base, nullptr, anchors, markers);
  require(created.has_value(), "native label marker fixture failed");
  return std::move(*created);
}

std::shared_ptr<const PreparedSketchProducts>
products(const std::shared_ptr<const SketchSceneSnapshot> &base) {
  auto source = std::make_shared<const SketchSceneProducts>(
      SketchSceneProducts{productStamp(base->stamp().target, 1U, 712U),
                          base,
                          selectedLine(base),
                          provisionalCircle(base),
                          degreeLabel(base)});
  auto prepared = prepareSketchProducts(source);
  require(prepared.has_value() && (*prepared)->base()->packet() &&
              (*prepared)->provisional() &&
              (*prepared)->provisional()->packet() &&
              (*prepared)->markers() && (*prepared)->markerPacket(),
          "native vector products failed");
  return std::move(*prepared);
}

QImage renderUntilPresented(OffscreenQuickRenderer &renderer,
                            SketchSceneItem &item,
                            const SketchProductStamp &expected) {
  QImage image;
  for (std::size_t frame = 0U; frame < 60U; ++frame) {
    image = renderer.render();
    const auto presented = item.presentedFrame();
    if (presented && presented->synchronized()->products()->stamp() == expected)
      return image;
  }
  const Diagnostic failure = item.lastDiagnostic();
  throw std::runtime_error("native vector frame did not become presentable: " +
                           failure.code + ": " + failure.summary);
}

void verifyNativeVectorRender(OffscreenQuickRenderer &renderer) {
  const auto source = nativeScene();
  const auto prepared = products(source);
  SketchSceneItem item{renderer.window().contentItem()};
  item.setSize(renderSize);
  item.retarget(source->stamp().target);
  const QColor regular{45, 125, 220};
  const QColor selected{0, 190, 180};
  const QColor preview{25, 205, 110};
  item.setPalette({regular.rgba(), qRgb(120, 130, 145), selected.rgba(),
                   preview.rgba(), qRgb(220, 70, 80), qRgb(245, 158, 11)});
  require(item.publishCamera({2U, {}, 0.0005, 0.0}) ==
              SketchCameraDecision::Accepted,
          "native render camera failed");
  auto offer = item.publishProducts(prepared);
  require(offer && offer->decision == PreparedSketchSceneDecision::Accepted,
          "native vector products were rejected");
  const QImage image = renderUntilPresented(renderer, item, prepared->stamp());
  const QString capturePath =
      qEnvironmentVariable("KEARNE_NATIVE_RENDER_CAPTURE");
  if (!capturePath.isEmpty())
    require(image.save(capturePath),
            "native vector capture could not be saved");
  require(foregroundPixels(image) > 300U,
          "native vector renderer produced no useful coverage");

  auto transform =
      SketchViewTransform::create({2U, {}, 0.0005, 0.0}, renderSize);
  require(transform.has_value(), "native render transform failed");
  require(colorNear(image, transform->toItem({0.0, 0.0}), selected),
          "selected vector did not retain its selected color");
  require(colorNear(image, transform->toItem({0.018, 0.035}), regular),
          "native circle did not render at its analytic radius");
  require(colorNear(image,
                    transform->toItem({0.04 / std::numbers::sqrt2,
                                       -0.06 + 0.04 / std::numbers::sqrt2}),
                    regular),
          "native rational B-spline did not render at its analytic position");
  require(colorNear(image, transform->toItem({0.04, 0.06}), regular),
          "native rational degree-one B-spline did not render as a segment");
  const QPointF labelAnchor = transform->toItem({0.0, -0.07});
  require(colorNear(image, labelAnchor + QPointF{14.0, -16.5}, regular, 3),
          "native vector text label did not render at its screen offset");
  require(colorNear(image, transform->toItem({-0.023, -0.035}), preview),
          "provisional native circle did not render during interaction");
  require(colorNear(image,
                    transform->toItem({-0.05 + 0.02 / std::numbers::sqrt2,
                                       0.06 + 0.02 / std::numbers::sqrt2}),
                    preview),
          "provisional native B-spline did not render during interaction");
  const QPointF ellipseCenter = transform->toItem({0.08, 0.035});
  require(colorNear(image, transform->toItem({0.11, 0.035}), preview),
          "provisional native ellipse did not render at its analytic radius");
  require(foregroundPixelsInsideEllipse(image, ellipseCenter, 60.0, 36.0,
                                        0.72) == 0U,
          "native ellipse leaked coverage into its interior");
  const QPointF ellipticalArcCenter = transform->toItem({-0.08, 0.02});
  const double arcParameter = 130.0 * std::numbers::pi / 180.0;
  require(colorNear(image,
                    transform->toItem(
                        {-0.08 + 0.028 * std::cos(arcParameter),
                         0.02 + 0.017 * std::sin(arcParameter)}),
                    preview),
          "provisional native elliptical arc did not render analytically");
  require(foregroundPixelsInsideEllipse(image, ellipticalArcCenter, 56.0,
                                        34.0, 0.72) == 0U,
          "native elliptical arc leaked coverage into its interior");

  auto picked =
      item.pick(transform->toItem({0.02, 0.0}), 5.0, SketchPickTargets::Curves);
  require(picked && picked->hit &&
              picked->hit->primitive == source->primitives().front().handle,
          "displayed native line could not be selected analytically");
  auto splinePicked =
      item.pick(transform->toItem({0.04 / std::numbers::sqrt2,
                                   -0.06 + 0.04 / std::numbers::sqrt2}),
                5.0, SketchPickTargets::Curves);
  require(splinePicked && splinePicked->hit &&
              splinePicked->hit->primitive == source->primitives()[2].handle,
          "displayed native B-spline could not be selected analytically");
  require(item.vectorPacketMetrics().records == 4U,
          "renderer reported the wrong native record count");
}

} // namespace

int main(int argc, char *argv[]) {
  const auto backend = parseBackend(argc, argv);
  if (!backend) {
    std::cerr << "usage: sketch-scene-native-render --backend <name>\n";
    return 2;
  }
  QQuickWindow::setGraphicsApi(backend->api);
  QSurfaceFormat graphicsFormat;
  graphicsFormat.setVersion(4, 3);
  graphicsFormat.setProfile(QSurfaceFormat::CoreProfile);
  QSurfaceFormat::setDefaultFormat(graphicsFormat);
  try {
    QGuiApplication application(argc, argv);
#if QT_CONFIG(vulkan)
    QVulkanInstance vulkan;
    if (backend->api == QSGRendererInterface::Vulkan && !vulkan.create())
      return backendUnavailable;
#endif
    OffscreenQuickRenderer renderer;
#if QT_CONFIG(vulkan)
    if (backend->api == QSGRendererInterface::Vulkan)
      renderer.window().setVulkanInstance(&vulkan);
#endif
    QString failure;
    if (!renderer.initialize(failure)) {
      std::cerr << failure.toStdString() << '\n';
      return backendUnavailable;
    }
    verifyNativeVectorRender(renderer);
    std::cout << "native vector rendering verified on "
              << renderer.driver().toStdString() << '\n';
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
