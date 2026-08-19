#include <QAccessible>
#include <QColorSpace>
#include <QCommandLineParser>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QMetaEnum>
#include <QPainter>
#include <QQmlApplicationEngine>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QScreen>
#include <QSet>
#include <QSysInfo>
#include <QTimer>
#include <QUuid>

#include <cmath>
#include <limits>

namespace {

constexpr auto kImageFile = "application-session.png";
constexpr auto kSemanticFile = "semantic-ui.json";
constexpr auto kCaptureFile = "capture.json";

QJsonArray rectJson(const QRect &rect) {
  return {rect.x(), rect.y(), rect.width(), rect.height()};
}

bool writePrivateJson(const QString &path, const QJsonObject &value) {
  QFile file(path);
  if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
    return false;
  if (file.write(QJsonDocument(value).toJson(QJsonDocument::Indented)) < 0)
    return false;
  file.close();
  return file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
}

QString sha256File(const QString &path) {
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly))
    return {};
  return QString::fromLatin1(
      QCryptographicHash::hash(file.readAll(), QCryptographicHash::Sha256)
          .toHex());
}

QString roleName(QAccessible::Role role) {
  const QMetaEnum roles = QMetaEnum::fromType<QAccessible::Role>();
  if (const char *name = roles.valueToKey(static_cast<int>(role)))
    return QString::fromLatin1(name);
  return QString::number(static_cast<int>(role));
}

bool requiresStableId(QAccessibleInterface *interface) {
  switch (interface->role()) {
  case QAccessible::MenuItem:
  case QAccessible::Link:
  case QAccessible::ListItem:
  case QAccessible::TreeItem:
  case QAccessible::PageTab:
  case QAccessible::EditableText:
  case QAccessible::PushButton:
  case QAccessible::CheckBox:
  case QAccessible::RadioButton:
  case QAccessible::ComboBox:
  case QAccessible::Slider:
  case QAccessible::SpinBox:
  case QAccessible::ButtonDropDown:
  case QAccessible::ButtonMenu:
  case QAccessible::ButtonDropGrid:
  case QAccessible::Switch:
    return true;
  default:
    return false;
  }
}

bool ownsInternalActions(QAccessible::Role role) {
  switch (role) {
  case QAccessible::ComboBox:
  case QAccessible::SpinBox:
  case QAccessible::EditableText:
    return true;
  default:
    return false;
  }
}

struct SemanticCollector {
  QVector<QJsonObject> nodes;
  QSet<QString> ids;
  QHash<QString, QAccessible::Role> roles;
  QStringList violations;
  qreal scale = 1.0;

  void visit(QAccessibleInterface *interface, const QString &parentId = {}) {
    if (!interface || !interface->isValid())
      return;

    QObject *object = interface->object();
    QString id = interface->text(QAccessible::Identifier);
    if (id.isEmpty() && object)
      id = object->property("semanticId").toString();

    auto *actions = interface->actionInterface();
    const QStringList actionNames =
        actions ? actions->actionNames() : QStringList{};
    const bool representedByComposite =
        !parentId.isEmpty() &&
        ownsInternalActions(roles.value(parentId, QAccessible::NoRole));
    if (requiresStableId(interface) && id.isEmpty() &&
        !representedByComposite) {
      const QString objectType =
          object ? object->metaObject()->className() : "<proxy>";
      violations.append(
          QStringLiteral(
              "actionable %1 '%2' (%3, parent '%4') has no Accessible.id")
              .arg(roleName(interface->role()),
                   interface->text(QAccessible::Name), objectType, parentId));
    }

    QString nextParent = parentId;
    if (!id.isEmpty()) {
      if (ids.contains(id)) {
        violations.append(
            QStringLiteral("duplicate Accessible.id '%1'").arg(id));
      } else {
        ids.insert(id);
        roles.insert(id, interface->role());
        nextParent = id;

        const QAccessible::State state = interface->state();
        const QRect logicalBounds = interface->rect();
        const QRect physicalBounds(std::lround(logicalBounds.x() * scale),
                                   std::lround(logicalBounds.y() * scale),
                                   std::lround(logicalBounds.width() * scale),
                                   std::lround(logicalBounds.height() * scale));

        QJsonArray actionArray;
        for (const QString &action : actionNames)
          actionArray.append(action);

        QJsonObject stateJson{
            {"enabled", !static_cast<bool>(state.disabled)},
            {"visible", !static_cast<bool>(state.invisible) &&
                            !static_cast<bool>(state.offscreen)},
            {"focused", static_cast<bool>(state.focused)},
            {"focusable", static_cast<bool>(state.focusable)},
            {"selected", static_cast<bool>(state.selected)},
            {"checked", static_cast<bool>(state.checked)},
            {"expanded", static_cast<bool>(state.expanded)},
            {"collapsed", static_cast<bool>(state.collapsed)},
            {"read_only", static_cast<bool>(state.readOnly)},
            {"password", static_cast<bool>(state.passwordEdit)},
        };

        QJsonObject node{
            {"id", id},
            {"parent_id",
             parentId.isEmpty() ? QJsonValue::Null : QJsonValue(parentId)},
            {"role", roleName(interface->role())},
            {"name", interface->text(QAccessible::Name)},
            {"description", interface->text(QAccessible::Description)},
            {"value", static_cast<bool>(state.passwordEdit)
                          ? QStringLiteral("<redacted>")
                          : interface->text(QAccessible::Value)},
            {"bounds_logical_desktop", rectJson(logicalBounds)},
            {"bounds_physical_desktop", rectJson(physicalBounds)},
            {"state", stateJson},
            {"actions", actionArray},
        };
        nodes.append(node);
      }
    }

    for (int index = 0; index < interface->childCount(); ++index)
      visit(interface->child(index), nextParent);
  }

  QJsonArray finalizedNodes() {
    QHash<QString, QJsonArray> children;
    for (const QJsonObject &node : std::as_const(nodes)) {
      const QString parent = node.value("parent_id").toString();
      if (!parent.isEmpty())
        children[parent].append(node.value("id"));
    }

    QJsonArray result;
    for (QJsonObject node : std::as_const(nodes)) {
      node.insert("child_ids", children.value(node.value("id").toString()));
      result.append(node);
    }
    return result;
  }
};

class ObservationRecorder final : public QObject {
public:
  ObservationRecorder(QString outputDirectory, int timeoutMs,
                      QObject *parent = nullptr)
      : QObject(parent), outputDirectory_(std::move(outputDirectory)) {
    timeout_.setSingleShot(true);
    timeout_.setInterval(timeoutMs);
    connect(&timeout_, &QTimer::timeout, this, [this] {
      fail(QStringLiteral("deadline expired before every registered surface "
                          "presented a frame"));
    });
  }

  void start() {
    QAccessible::setActive(true);
    if (!QDir().mkpath(outputDirectory_)) {
      fail(QStringLiteral("cannot create output directory"));
      return;
    }
    QFile::setPermissions(outputDirectory_, QFileDevice::ReadOwner |
                                                QFileDevice::WriteOwner |
                                                QFileDevice::ExeOwner);

    synchronizeSurfaces();
    if (frames_.isEmpty()) {
      fail(QStringLiteral("no visible registered Kearne surface"));
      return;
    }
    timeout_.start();
    for (QQuickWindow *window : frames_.keys())
      window->update();
  }

private:
  QList<QQuickWindow *> visibleSurfaces() const {
    QList<QQuickWindow *> surfaces;
    for (QWindow *window : QGuiApplication::topLevelWindows()) {
      auto *quickWindow = qobject_cast<QQuickWindow *>(window);
      if (quickWindow && quickWindow->isVisible() &&
          !quickWindow->property("kearneSurfaceId").toString().isEmpty()) {
        surfaces.append(quickWindow);
      }
    }
    return surfaces;
  }

  void synchronizeSurfaces() {
    const QList<QQuickWindow *> surfaces = visibleSurfaces();
    for (QQuickWindow *window : surfaces) {
      if (frames_.contains(window))
        continue;
      frames_.insert(window, 0);
      connect(
          window, &QQuickWindow::frameSwapped, this,
          [this, window] {
            if (finished_ || !frames_.contains(window))
              return;
            ++frames_[window];
            scheduleCaptureIfReady();
          },
          Qt::QueuedConnection);
      connect(window, &QObject::destroyed, this,
              [this, window] { frames_.remove(window); });
    }

    for (QQuickWindow *window : frames_.keys()) {
      if (!surfaces.contains(window))
        frames_.remove(window);
    }
  }

  void scheduleCaptureIfReady() {
    synchronizeSurfaces();
    for (int generation : std::as_const(frames_)) {
      if (generation == 0)
        return;
    }
    if (captureScheduled_)
      return;
    captureScheduled_ = true;
    QMetaObject::invokeMethod(
        this, [this] { capture(); }, Qt::QueuedConnection);
  }

  void capture() {
    captureScheduled_ = false;
    synchronizeSurfaces();
    for (int generation : std::as_const(frames_)) {
      if (generation == 0)
        return;
    }

    const QList<QQuickWindow *> surfaces = visibleSurfaces();
    if (surfaces.isEmpty()) {
      fail(
          QStringLiteral("all registered surfaces disappeared before capture"));
      return;
    }

    QRect logicalUnion;
    qreal commonScale = 0.0;
    struct Grab {
      QQuickWindow *window;
      QImage image;
      qreal scale;
    };
    QList<Grab> grabs;

    for (QQuickWindow *window : surfaces) {
      QImage image = window->grabWindow();
      if (image.isNull() || window->width() <= 0 || window->height() <= 0) {
        fail(QStringLiteral("surface '%1' returned an empty frame")
                 .arg(window->property("kearneSurfaceId").toString()));
        return;
      }
      const qreal scaleX = qreal(image.width()) / window->width();
      const qreal scaleY = qreal(image.height()) / window->height();
      if (std::abs(scaleX - scaleY) > 0.01) {
        fail(QStringLiteral(
            "surface has inconsistent horizontal and vertical scale"));
        return;
      }
      if (commonScale == 0.0)
        commonScale = scaleX;
      if (std::abs(commonScale - scaleX) > 0.01) {
        fail(QStringLiteral(
            "mixed-DPI composition is not proved by this prototype"));
        return;
      }
      logicalUnion = logicalUnion.united(window->geometry());
      grabs.append({window, image, scaleX});
    }

    QImage composite(std::lround(logicalUnion.width() * commonScale),
                     std::lround(logicalUnion.height() * commonScale),
                     QImage::Format_RGBA8888);
    composite.fill(QColor("#0b0e13"));

    QPainter painter(&composite);
    QJsonArray surfaceJson;
    for (const Grab &grab : std::as_const(grabs)) {
      const QPoint offset(
          std::lround((grab.window->x() - logicalUnion.x()) * commonScale),
          std::lround((grab.window->y() - logicalUnion.y()) * commonScale));
      painter.drawImage(offset, grab.image);

      surfaceJson.append(QJsonObject{
          {"id", grab.window->property("kearneSurfaceId").toString()},
          {"title", grab.window->title()},
          {"logical_desktop_bounds", rectJson(grab.window->geometry())},
          {"composite_pixel_bounds",
           rectJson(QRect(offset, grab.image.size()))},
          {"frame_generation", frames_.value(grab.window)},
          {"device_pixel_ratio", grab.scale},
      });
    }
    painter.end();

    QSet<QRgb> sampledColors;
    for (int y = 0; y < composite.height();
         y += qMax(1, composite.height() / 64)) {
      for (int x = 0; x < composite.width();
           x += qMax(1, composite.width() / 64))
        sampledColors.insert(composite.pixel(x, y));
    }
    if (sampledColors.size() < 8) {
      fail(QStringLiteral("composite frame appears blank or uniform"));
      return;
    }

    const QString imagePath = QDir(outputDirectory_).filePath(kImageFile);
    composite.setColorSpace(QColorSpace::SRgb);
    if (!composite.save(imagePath, "PNG") ||
        !QFile::setPermissions(imagePath, QFileDevice::ReadOwner |
                                              QFileDevice::WriteOwner)) {
      fail(QStringLiteral("cannot write private PNG artifact"));
      return;
    }

    const QString imageDigest = sha256File(imagePath);
    if (imageDigest.isEmpty()) {
      fail(QStringLiteral("cannot hash PNG artifact"));
      return;
    }

    SemanticCollector collector;
    collector.scale = commonScale;
    for (QQuickWindow *window : surfaces)
      collector.visit(QAccessible::queryAccessibleInterface(window));
    if (!collector.violations.isEmpty()) {
      fail(collector.violations.join("; "));
      return;
    }

    const QString observationId =
        QUuid::createUuid().toString(QUuid::WithoutBraces);
    const QString timestamp =
        QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
    int renderGeneration = std::numeric_limits<int>::max();
    for (QQuickWindow *window : surfaces)
      renderGeneration = qMin(renderGeneration, frames_.value(window));
    QJsonObject observationPoint{
        {"id", observationId},
        {"ui_generation", 1},
        {"render_generation", renderGeneration},
    };
    QJsonObject semantic{
        {"schema", "kearne.semantic-ui/v1"},     {"session_id", sessionId_},
        {"observation_point", observationPoint}, {"captured_at_utc", timestamp},
        {"nodes", collector.finalizedNodes()},
    };
    const QString semanticPath = QDir(outputDirectory_).filePath(kSemanticFile);
    if (!writePrivateJson(semanticPath, semantic)) {
      fail(QStringLiteral("cannot write semantic snapshot"));
      return;
    }

    QJsonObject capture{
        {"schema", "kearne.application-session-capture/v1"},
        {"session_id", sessionId_},
        {"observation_point", observationPoint},
        {"captured_at_utc", timestamp},
        {"capture_method",
         "QQuickWindow::grabWindow composed in desktop coordinates"},
        {"scope", "visible registered Kearne client surfaces"},
        {"platform", QGuiApplication::platformName()},
        {"qt_version", QT_VERSION_STR},
        {"build",
         QJsonObject{
             {"application_version", QCoreApplication::applicationVersion()},
             {"executable_sha256",
              sha256File(QCoreApplication::applicationFilePath())},
         }},
        {"environment",
         QJsonObject{
             {"cpu_architecture", QSysInfo::currentCpuArchitecture()},
             {"kernel_type", QSysInfo::kernelType()},
             {"kernel_version", QSysInfo::kernelVersion()},
             {"locale", QLocale().name()},
             {"quick_style", QQuickStyle::name()},
             {"theme", "kearne-dark-prototype"},
         }},
        {"image",
         QJsonObject{
             {"path", kImageFile},
             {"format", "PNG"},
             {"pixel_width", composite.width()},
             {"pixel_height", composite.height()},
             {"sha256", imageDigest},
             {"color_space", composite.colorSpace().description()},
             {"sampled_color_count", sampledColors.size()},
         }},
        {"logical_desktop_bounds", rectJson(logicalUnion)},
        {"device_pixel_ratio", commonScale},
        {"surfaces", surfaceJson},
        {"semantic_snapshot", kSemanticFile},
        {"limits",
         QJsonArray{
             "native decorations and shadows are outside QQuickWindow client "
             "capture",
             "OS-owned, secure, and unrelated application pixels are excluded",
             "mixed-DPI and overlapping-surface z-order require platform "
             "probes",
         }},
    };
    if (!writePrivateJson(QDir(outputDirectory_).filePath(kCaptureFile),
                          capture)) {
      fail(QStringLiteral("cannot write capture metadata"));
      return;
    }

    finished_ = true;
    timeout_.stop();
    const QJsonObject result{
        {"status", "captured"},
        {"image", imagePath},
        {"semantic_snapshot", semanticPath},
        {"metadata", QDir(outputDirectory_).filePath(kCaptureFile)},
    };
    fprintf(stdout, "%s\n",
            QJsonDocument(result).toJson(QJsonDocument::Compact).constData());
    fflush(stdout);
    QCoreApplication::exit(0);
  }

  void fail(const QString &message) {
    if (finished_)
      return;
    finished_ = true;
    timeout_.stop();
    const QJsonObject result{{"status", "error"}, {"message", message}};
    fprintf(stderr, "%s\n",
            QJsonDocument(result).toJson(QJsonDocument::Compact).constData());
    fflush(stderr);
    QCoreApplication::exit(2);
  }

  QString outputDirectory_;
  QHash<QQuickWindow *, int> frames_;
  QTimer timeout_;
  const QString sessionId_ = QUuid::createUuid().toString(QUuid::WithoutBraces);
  bool captureScheduled_ = false;
  bool finished_ = false;
};

} // namespace

int main(int argc, char *argv[]) {
  QGuiApplication application(argc, argv);
  QCoreApplication::setApplicationName("kearne-observation-spike");
  QCoreApplication::setApplicationVersion("0.1");
  QQuickStyle::setStyle("Basic");

  QCommandLineParser parser;
  parser.setApplicationDescription(
      "Capture a frame-correlated Kearne Qt session.");
  parser.addHelpOption();
  parser.addVersionOption();
  const QCommandLineOption outputOption(
      {"o", "output"}, "Private artifact directory.", "directory");
  const QCommandLineOption timeoutOption(
      "timeout-ms", "Frame deadline in milliseconds.", "milliseconds", "10000");
  parser.addOptions({outputOption, timeoutOption});
  parser.process(application);

  bool timeoutOk = false;
  const int timeoutMs = parser.value(timeoutOption).toInt(&timeoutOk);
  if (!parser.isSet(outputOption) || !timeoutOk || timeoutMs < 1) {
    fprintf(stderr, "--output and a positive --timeout-ms are required\n");
    return 2;
  }

  QQmlApplicationEngine engine;
  engine.loadFromModule("Kearne.ObservationSpike", "Main");
  if (engine.rootObjects().isEmpty())
    return 2;

  ObservationRecorder recorder(QDir(parser.value(outputOption)).absolutePath(),
                               timeoutMs);
  QTimer::singleShot(0, &recorder, [&recorder] { recorder.start(); });
  return application.exec();
}
