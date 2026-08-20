#include "engineering_grid_item.hpp"

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
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string_view>

namespace {

using namespace kearne::ui;

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

  ~OffscreenQuickRenderer() {
    window_.setRenderTarget({});
    if (initialized_)
      control_.invalidate();
    renderPass_.reset();
    renderTarget_.reset();
    depthStencil_.reset();
    texture_.reset();
  }

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
  QQuickRenderControl control_;
  QQuickWindow window_;
  QRhi *rhi_ = nullptr;
  std::unique_ptr<QRhiTexture> texture_;
  std::unique_ptr<QRhiRenderBuffer> depthStencil_;
  std::unique_ptr<QRhiTextureRenderTarget> renderTarget_;
  std::unique_ptr<QRhiRenderPassDescriptor> renderPass_;
  bool initialized_ = false;
};

bool differsFromBackground(QRgb pixel) {
  constexpr int tolerance = 10;
  return std::max({std::abs(qRed(pixel) - background.red()),
                   std::abs(qGreen(pixel) - background.green()),
                   std::abs(qBlue(pixel) - background.blue())}) > tolerance;
}

std::size_t visiblePixels(const QImage &source) {
  const QImage image = source.convertToFormat(QImage::Format_ARGB32);
  std::size_t count = 0;
  for (int y = 0; y < image.height(); ++y) {
    const auto *row = reinterpret_cast<const QRgb *>(image.constScanLine(y));
    for (int x = 0; x < image.width(); ++x)
      count += differsFromBackground(row[x]) ? 1U : 0U;
  }
  return count;
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
      const QRgb leftPixel = leftRow[x];
      const QRgb rightPixel = rightRow[x];
      if (std::max({std::abs(qRed(leftPixel) - qRed(rightPixel)),
                    std::abs(qGreen(leftPixel) - qGreen(rightPixel)),
                    std::abs(qBlue(leftPixel) - qBlue(rightPixel))}) > 4)
        ++count;
    }
  }
  return count;
}

void verifyNativeGrid(OffscreenQuickRenderer &renderer) {
  EngineeringGridItem item{renderer.window().contentItem()};
  item.setSize(renderSize);
  item.setPixelsPerMetre(20'000.0);
  item.setMinorSpacingMetres(0.001);
  item.setMinorColor(QColor{40, 96, 160, 255});
  item.setMajorColor(QColor{88, 156, 224, 255});
  item.setAxisXColor(QColor{240, 72, 72, 255});
  item.setAxisYColor(QColor{72, 224, 112, 255});

  const QImage initial = renderer.render();
  const EngineeringGridMetrics initialMetrics = item.gridMetrics();
  const std::uint64_t initialBuilds = item.geometryBuildCount();
  require(visiblePixels(initial) > 2'000U && initialBuilds == 1U &&
              initialMetrics.status == EngineeringGridBuildStatus::Built &&
              initialMetrics.axisLines == 2U,
          "native grid did not render its canonical retained geometry");

  const QImage unchanged = renderer.render();
  require(item.geometryBuildCount() == initialBuilds &&
              differingPixels(initial, unchanged) <= 4U,
          "unchanged frame rebuilt or changed retained grid geometry");

  item.setMinorColor(QColor{144, 72, 208, 255});
  item.setMajorColor(QColor{224, 144, 64, 255});
  const QImage recolored = renderer.render();
  require(item.geometryBuildCount() == initialBuilds &&
              differingPixels(unchanged, recolored) > 1'000U,
          "palette-only update rebuilt geometry or failed to update material");

  item.setViewCenterMetres({0.0037, -0.0021});
  item.setRotationRadians(0.37);
  const QImage transformed = renderer.render();
  const EngineeringGridMetrics transformedMetrics = item.gridMetrics();
  require(item.geometryBuildCount() == initialBuilds + 1U &&
              transformedMetrics.status == EngineeringGridBuildStatus::Built &&
              differingPixels(recolored, transformed) > 2'000U,
          "pan/rotation did not rebuild and transform the native grid");

  item.setMinorSpacingMetres(0.0);
  const QImage invalid = renderer.render();
  require(item.geometryBuildCount() == initialBuilds + 2U &&
              item.gridMetrics().status ==
                  EngineeringGridBuildStatus::InvalidProjection &&
              visiblePixels(invalid) == 0U,
          "invalid projection retained stale native grid geometry");

  item.setMinorSpacingMetres(1.0e-6);
  for (int index = 0; index < 64; ++index) {
    item.setPixelsPerMetre(std::pow(1.18, static_cast<double>(index)) * 50.0);
    const QImage frame = renderer.render();
    const EngineeringGridMetrics metrics = item.gridMetrics();
    require(visiblePixels(frame) > 0U &&
                metrics.status == EngineeringGridBuildStatus::Built &&
                metrics.xFamilyLines <=
                    EngineeringGridPolicy::maximumLinesPerFamily &&
                metrics.yFamilyLines <=
                    EngineeringGridPolicy::maximumLinesPerFamily,
            "zoom sequence escaped the native grid work bound");
  }
  require(item.geometryBuildCount() == initialBuilds + 66U,
          "zoom sequence did not produce exactly one rebuild per frame");
}

} // namespace

int main(int argc, char *argv[]) {
  const auto backend = parseBackend(argc, argv);
  if (!backend) {
    std::cerr << "usage: engineering_grid_native_render --backend "
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
    verifyNativeGrid(renderer);
    std::cout << "verified native engineering grid backend="
              << backend->name.toStdString()
              << " driver=" << renderer.driver().toStdString() << '\n';
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "backend=" << backend->name.toStdString()
              << " failure=" << error.what() << '\n';
    return 1;
  }
}
