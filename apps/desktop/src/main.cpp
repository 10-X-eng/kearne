#include "ui_session.hpp"

#include <QBuffer>
#include <QCommandLineParser>
#include <QColorSpace>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSaveFile>
#include <QSet>
#include <QStandardPaths>
#include <QTimer>
#include <QUuid>
#include <QWindow>

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <stdexcept>

namespace {

QByteArray encodedPng(const QImage &image) {
  QByteArray bytes;
  QBuffer buffer(&bytes);
  if (!buffer.open(QIODevice::WriteOnly) || !image.save(&buffer, "PNG"))
    throw std::runtime_error("could not encode application capture");
  return bytes;
}

QJsonObject imageStatistics(const QImage &image) {
  QSet<QRgb> colors;
  int minimumAlpha = 255;
  int maximumAlpha = 0;
  int minimumLuminance = 255;
  int maximumLuminance = 0;
  const int xStep = std::max(1, image.width() / 128);
  const int yStep = std::max(1, image.height() / 128);
  for (int y = 0; y < image.height(); y += yStep) {
    for (int x = 0; x < image.width(); x += xStep) {
      const QRgb pixel = image.pixel(x, y);
      colors.insert(pixel);
      minimumAlpha = std::min(minimumAlpha, qAlpha(pixel));
      maximumAlpha = std::max(maximumAlpha, qAlpha(pixel));
      const int luminance = qGray(pixel);
      minimumLuminance = std::min(minimumLuminance, luminance);
      maximumLuminance = std::max(maximumLuminance, luminance);
    }
  }
  return QJsonObject{
      {QStringLiteral("sampled_unique_colors"), colors.size()},
      {QStringLiteral("sampled_alpha_min"), minimumAlpha},
      {QStringLiteral("sampled_alpha_max"), maximumAlpha},
      {QStringLiteral("sampled_luminance_min"), minimumLuminance},
      {QStringLiteral("sampled_luminance_max"), maximumLuminance},
  };
}

void writeFile(const QString &path, const QByteArray &bytes) {
  QSaveFile file(path);
  if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() ||
      !file.commit()) {
    throw std::runtime_error("could not write " + path.toStdString());
  }
  QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
}

QJsonArray logicalBoundsFor(QQuickItem &item) {
  const QPointF scene = item.mapToScene(QPointF{});
  return QJsonArray{scene.x(), scene.y(), item.width(), item.height()};
}

QJsonArray screenBoundsFor(QQuickItem &item, QQuickWindow &window) {
  const QPointF scene = item.mapToScene(QPointF{});
  const QPoint windowPosition = window.position();
  return QJsonArray{scene.x() + windowPosition.x(),
                    scene.y() + windowPosition.y(), item.width(), item.height()};
}

QJsonArray physicalBoundsFor(QQuickItem &item, QQuickWindow &window) {
  const qreal ratio = window.devicePixelRatio();
  const QPointF scene = item.mapToScene(QPointF{});
  return QJsonArray{scene.x() * ratio, scene.y() * ratio,
                    item.width() * ratio, item.height() * ratio};
}

bool isEffectivelyVisible(QQuickItem &item, QQuickWindow &window) {
  if (!item.isVisible() || item.opacity() <= 0.0 || item.width() <= 0.0 ||
      item.height() <= 0.0)
    return false;
  QRectF visible = item.mapRectToScene(QRectF{0.0, 0.0, item.width(), item.height()});
  visible = visible.intersected(QRectF{0.0, 0.0, static_cast<qreal>(window.width()),
                                      static_cast<qreal>(window.height())});
  for (QQuickItem *parent = item.parentItem(); parent; parent = parent->parentItem()) {
    if (!parent->isVisible() || parent->opacity() <= 0.0)
      return false;
    if (parent->clip()) {
      visible = visible.intersected(
          parent->mapRectToScene(QRectF{0.0, 0.0, parent->width(), parent->height()}));
    }
  }
  return !visible.isEmpty();
}

QList<QObject *> traversalChildren(QObject &object) {
  QList<QObject *> children = object.children();
  if (auto *item = qobject_cast<QQuickItem *>(&object)) {
    for (QQuickItem *visualChild : item->childItems()) {
      if (!children.contains(visualChild))
        children.push_back(visualChild);
    }
  }
  return children;
}

void collectSemanticNodes(QObject &object, QQuickWindow &window, QJsonArray &nodes,
                          QSet<QObject *> &visited,
                          const QString &semanticParent = {}) {
  if (visited.contains(&object))
    return;
  visited.insert(&object);
  const QString id = object.property("semanticId").toString();
  QString childParent = semanticParent;
  if (!id.isEmpty()) {
    bool enabled = object.property("enabled").isValid()
                       ? object.property("enabled").toBool()
                       : true;
    bool visible = object.property("visible").isValid()
                       ? object.property("visible").toBool()
                       : true;
    bool focused = object.property("activeFocus").isValid()
                       ? object.property("activeFocus").toBool()
                       : false;
    if (auto *item = qobject_cast<QQuickItem *>(&object)) {
      enabled = item->isEnabled();
      visible = isEffectivelyVisible(*item, window);
      focused = item->hasActiveFocus();
    }
    QJsonObject node{{QStringLiteral("id"), id},
                     {QStringLiteral("name"),
                      object.property("semanticName").toString()},
                     {QStringLiteral("role"),
                      object.property("semanticRole").toString()},
                     {QStringLiteral("parent_id"), semanticParent},
                     {QStringLiteral("enabled"), enabled},
                     {QStringLiteral("visible"), visible},
                     {QStringLiteral("focused"), focused},
                     {QStringLiteral("actions"),
                      QJsonArray::fromStringList(
                          object.property("semanticActions").toStringList())}};
    if (auto *item = qobject_cast<QQuickItem *>(&object)) {
      node.insert(QStringLiteral("logical_bounds"), logicalBoundsFor(*item));
      node.insert(QStringLiteral("physical_bounds"),
                  physicalBoundsFor(*item, window));
      node.insert(QStringLiteral("screen_bounds"),
                  screenBoundsFor(*item, window));
    } else if (auto *surface = qobject_cast<QWindow *>(&object)) {
      const qreal ratio = surface->devicePixelRatio();
      node.insert(QStringLiteral("logical_bounds"),
                  QJsonArray{0, 0, surface->width(), surface->height()});
      node.insert(QStringLiteral("physical_bounds"),
                  QJsonArray{0, 0, surface->width() * ratio,
                             surface->height() * ratio});
      node.insert(QStringLiteral("screen_bounds"),
                  QJsonArray{surface->x(), surface->y(), surface->width(),
                             surface->height()});
    } else if (object.property("width").isValid() &&
               object.property("height").isValid()) {
      const qreal x = object.property("x").toReal();
      const qreal y = object.property("y").toReal();
      const qreal width = object.property("width").toReal();
      const qreal height = object.property("height").toReal();
      const qreal ratio = window.devicePixelRatio();
      node.insert(QStringLiteral("logical_bounds"), QJsonArray{x, y, width, height});
      node.insert(QStringLiteral("physical_bounds"),
                  QJsonArray{x * ratio, y * ratio, width * ratio,
                             height * ratio});
      node.insert(QStringLiteral("screen_bounds"),
                  QJsonArray{x + window.x(), y + window.y(), width, height});
    }
    const QVariant value = object.property("semanticValue");
    if (value.isValid())
      node.insert(QStringLiteral("value"), QJsonValue::fromVariant(value));
    nodes.push_back(node);
    childParent = id;
  }

  for (QObject *child : traversalChildren(object))
    collectSemanticNodes(*child, window, nodes, visited, childParent);
}

QObject *findSemanticObject(QObject &object, const QString &semanticId,
                            QSet<QObject *> &visited) {
  if (visited.contains(&object))
    return nullptr;
  visited.insert(&object);
  if (object.property("semanticId").toString() == semanticId)
    return &object;
  for (QObject *child : traversalChildren(object)) {
    if (QObject *match = findSemanticObject(*child, semanticId, visited))
      return match;
  }
  return nullptr;
}

QObject *findSemanticObject(QObject &object, const QString &semanticId) {
  QSet<QObject *> visited;
  return findSemanticObject(object, semanticId, visited);
}

class CaptureController final : public QObject {
public:
  CaptureController(QQuickWindow &window, kearne::ui::UiSession &session,
                    QString outputDirectory, QStringList actions, QObject *parent)
      : QObject(parent), window_(window), session_(session),
        outputDirectory_(std::move(outputDirectory)),
        pendingActions_(std::move(actions)) {
    connect(&window_, &QQuickWindow::frameSwapped, this, [this] {
      ++presentedFrames_;
      if (++settledFrames_ < 2) {
        window_.update();
        return;
      }
      if (!pendingActions_.isEmpty() && !actionScheduled_) {
        actionScheduled_ = true;
        QTimer::singleShot(0, this, [this] { invokeNextAction(); });
        return;
      }
      if (!captureScheduled_) {
        captureScheduled_ = true;
        QTimer::singleShot(0, this, [this] { capture(); });
      }
    });
    QTimer::singleShot(10'000, this, [this] {
      if (!captureScheduled_) {
        std::cerr << "capture deadline expired before two presented frames\n";
        QCoreApplication::exit(3);
      }
    });
    window_.update();
  }

private:
  void invokeNextAction() {
    try {
      const QString semanticId = pendingActions_.takeFirst();
      QObject *target = findSemanticObject(window_, semanticId);
      if (!target)
        throw std::runtime_error("semantic action target not found: " +
                                 semanticId.toStdString());
      if (!target->property("enabled").toBool())
        throw std::runtime_error("semantic action target is disabled: " +
                                 semanticId.toStdString());
      if (auto *item = qobject_cast<QQuickItem *>(target);
          item && !isEffectivelyVisible(*item, window_))
        throw std::runtime_error("semantic action target is not visible: " +
                                 semanticId.toStdString());
      if (!target->property("semanticActions").toStringList().contains(
              QStringLiteral("invoke")))
        throw std::runtime_error("semantic target does not support invoke: " +
                                 semanticId.toStdString());

      const qulonglong generationBefore = session_.generation();
      if (!QMetaObject::invokeMethod(target, "click", Qt::DirectConnection))
        throw std::runtime_error("semantic target has no public click action: " +
                                 semanticId.toStdString());
      actionReceipts_.push_back(
          QJsonObject{{QStringLiteral("semantic_id"), semanticId},
                      {QStringLiteral("action"), QStringLiteral("invoke")},
                      {QStringLiteral("generation_before"),
                       static_cast<qint64>(generationBefore)},
                      {QStringLiteral("generation_after"),
                       static_cast<qint64>(session_.generation())}});
      actionScheduled_ = false;
      settledFrames_ = 0;
      window_.update();
    } catch (const std::exception &error) {
      std::cerr << error.what() << '\n';
      QCoreApplication::exit(4);
    }
  }

  void capture() {
    try {
      QDir directory;
      if (!directory.mkpath(outputDirectory_))
        throw std::runtime_error("could not create capture directory");
      QFile::setPermissions(outputDirectory_, QFileDevice::ReadOwner |
                                                  QFileDevice::WriteOwner |
                                                  QFileDevice::ExeOwner);

      QImage image = window_.grabWindow();
      if (image.colorSpace().isValid())
        image = image.convertedToColorSpace(QColorSpace(QColorSpace::SRgb));
      else
        image.setColorSpace(QColorSpace(QColorSpace::SRgb));
      if (image.isNull() || image.width() < 1 || image.height() < 1)
        throw std::runtime_error("application capture was empty");
      const QByteArray png = encodedPng(image);
      const QJsonObject statistics = imageStatistics(image);
      const QString imagePath =
          QDir(outputDirectory_).filePath(QStringLiteral("application-session.png"));
      writeFile(imagePath, png);

      QJsonArray nodes;
      QSet<QObject *> visited;
      collectSemanticNodes(window_, window_, nodes, visited);
      const QJsonObject semantic{
          {QStringLiteral("schema"), QStringLiteral("kearne.semantic-ui/v1")},
          {QStringLiteral("surface_id"), session_.activeSurfaceId()},
          {QStringLiteral("workspace_id"), session_.activeWorkspaceId()},
          {QStringLiteral("ui_generation"),
           static_cast<qint64>(session_.generation())},
          {QStringLiteral("actions"), actionReceipts_},
          {QStringLiteral("nodes"), nodes},
      };
      const QString semanticPath =
          QDir(outputDirectory_).filePath(QStringLiteral("semantic-ui.json"));
      writeFile(semanticPath, QJsonDocument(semantic).toJson(QJsonDocument::Indented));

      const QByteArray digest =
          QCryptographicHash::hash(png, QCryptographicHash::Sha256).toHex();
      const QJsonObject metadata{
          {QStringLiteral("schema"),
           QStringLiteral("kearne.application-session-capture/v1")},
          {QStringLiteral("application_version"), QStringLiteral(KEARNE_VERSION)},
          {QStringLiteral("build_type"), QStringLiteral(KEARNE_BUILD_TYPE)},
          {QStringLiteral("session_id"), sessionId_},
          {QStringLiteral("qt_version"), QString::fromLatin1(qVersion())},
          {QStringLiteral("platform"), QGuiApplication::platformName()},
          {QStringLiteral("locale"), QLocale().name()},
          {QStringLiteral("captured_at"),
           QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
          {QStringLiteral("capture_method"),
           QStringLiteral("QQuickWindow::grabWindow")},
          {QStringLiteral("target"), QStringLiteral("ApplicationSession")},
          {QStringLiteral("scope"),
           QStringLiteral("visible registered Kearne client surfaces")},
          {QStringLiteral("surface_count"), 1},
          {QStringLiteral("ui_generation"),
           static_cast<qint64>(session_.generation())},
          {QStringLiteral("presented_frames"), presentedFrames_},
          {QStringLiteral("actions"), actionReceipts_},
          {QStringLiteral("image"),
           QJsonObject{{QStringLiteral("path"), imagePath},
                       {QStringLiteral("pixel_width"), image.width()},
                       {QStringLiteral("pixel_height"), image.height()},
                       {QStringLiteral("logical_width"), window_.width()},
                       {QStringLiteral("logical_height"), window_.height()},
                       {QStringLiteral("device_pixel_ratio"),
                        window_.devicePixelRatio()},
                       {QStringLiteral("format"), QStringLiteral("PNG")},
                       {QStringLiteral("color_space"), QStringLiteral("sRGB")},
                       {QStringLiteral("sha256"), QString::fromLatin1(digest)},
                       {QStringLiteral("statistics"), statistics}}},
          {QStringLiteral("semantic_snapshot"), semanticPath},
      };
      const QString metadataPath =
          QDir(outputDirectory_).filePath(QStringLiteral("capture.json"));
      writeFile(metadataPath, QJsonDocument(metadata).toJson(QJsonDocument::Indented));
      const QByteArray output =
          QJsonDocument(metadata).toJson(QJsonDocument::Compact);
      std::cout.write(output.constData(), output.size());
      std::cout.put('\n');
      QCoreApplication::exit(0);
    } catch (const std::exception &error) {
      std::cerr << error.what() << '\n';
      QCoreApplication::exit(2);
    }
  }

  QQuickWindow &window_;
  kearne::ui::UiSession &session_;
  QString outputDirectory_;
  QStringList pendingActions_;
  QJsonArray actionReceipts_;
  QString sessionId_ = QUuid::createUuid().toString(QUuid::WithoutBraces);
  int presentedFrames_ = 0;
  int settledFrames_ = 0;
  bool actionScheduled_ = false;
  bool captureScheduled_ = false;
};

} // namespace

int main(int argc, char *argv[]) {
  QGuiApplication::setOrganizationName(QStringLiteral("Kearne"));
  QGuiApplication::setApplicationName(QStringLiteral("Kearne"));
  QGuiApplication::setApplicationVersion(QStringLiteral(KEARNE_VERSION));
  if (!qEnvironmentVariableIsSet("QT_QUICK_CONTROLS_STYLE"))
    qputenv("QT_QUICK_CONTROLS_STYLE", "Basic");
  QGuiApplication application(argc, argv);

  QCommandLineParser parser;
  parser.setApplicationDescription(QStringLiteral("Kearne mechanical CAD"));
  parser.addHelpOption();
  parser.addVersionOption();
  QCommandLineOption captureOption(QStringLiteral("capture-dir"),
                                   QStringLiteral("Capture the visible Kearne session and exit."),
                                   QStringLiteral("directory"));
  QCommandLineOption workspaceOption(QStringLiteral("workspace"),
                                     QStringLiteral("Select the initial workspace."),
                                     QStringLiteral("id"), QStringLiteral("model"));
  QCommandLineOption surfaceOption(QStringLiteral("surface"),
                                   QStringLiteral("Select the initial application surface."),
                                   QStringLiteral("id"), QStringLiteral("editor"));
  QCommandLineOption stateOption(QStringLiteral("ui-state"),
                                 QStringLiteral("Select deterministic contract state."),
                                 QStringLiteral("id"), QStringLiteral("unavailable"));
  QCommandLineOption inspectorOption(
      QStringLiteral("inspector-page"),
      QStringLiteral("Select the initial inspector page."), QStringLiteral("id"),
      QStringLiteral("properties"));
  QCommandLineOption settingsCategoryOption(
      QStringLiteral("settings-category"),
      QStringLiteral("Select the initial settings category."), QStringLiteral("id"),
      QStringLiteral("appearance"));
  QCommandLineOption actionOption(
      QStringLiteral("ui-action"),
      QStringLiteral("Invoke a visible control by semantic ID before capture."),
      QStringLiteral("semantic-id"));
  QCommandLineOption widthOption(QStringLiteral("width"), QStringLiteral("Window width."),
                                 QStringLiteral("pixels"), QStringLiteral("1440"));
  QCommandLineOption heightOption(QStringLiteral("height"), QStringLiteral("Window height."),
                                  QStringLiteral("pixels"), QStringLiteral("900"));
  parser.addOptions(
      {captureOption, workspaceOption, surfaceOption, stateOption, inspectorOption,
       settingsCategoryOption, actionOption, widthOption, heightOption});
  parser.process(application);
  if (parser.isSet(actionOption) && !parser.isSet(captureOption)) {
    std::cerr << "--ui-action requires --capture-dir\n";
    return EXIT_FAILURE;
  }

  kearne::ui::UiSession session(kearne::ui::makeDevelopmentFrontendPort());
  session.navigateTo(parser.value(surfaceOption));
  session.selectWorkspace(parser.value(workspaceOption));
  session.selectInspectorPage(parser.value(inspectorOption));
  session.selectSettingsCategory(parser.value(settingsCategoryOption));
  session.requestCommand(QStringLiteral("development.state.") +
                         parser.value(stateOption));

  QQmlApplicationEngine engine;
  engine.rootContext()->setContextProperty(QStringLiteral("uiSession"), &session);
  engine.loadFromModule(QStringLiteral("Kearne.UI"), QStringLiteral("Main"));
  if (engine.rootObjects().isEmpty())
    return EXIT_FAILURE;

  auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().constFirst());
  if (!window) {
    std::cerr << "root QML object is not a window\n";
    return EXIT_FAILURE;
  }
  bool widthOk = false;
  bool heightOk = false;
  const int width = parser.value(widthOption).toInt(&widthOk);
  const int height = parser.value(heightOption).toInt(&heightOk);
  if (!widthOk || !heightOk || width < 800 || height < 600) {
    std::cerr << "capture dimensions must be at least 800x600\n";
    return EXIT_FAILURE;
  }
  window->resize(width, height);

  std::unique_ptr<CaptureController> capture;
  if (parser.isSet(captureOption)) {
    capture = std::make_unique<CaptureController>(
        *window, session, parser.value(captureOption), parser.values(actionOption),
        &application);
  }
  return application.exec();
}
