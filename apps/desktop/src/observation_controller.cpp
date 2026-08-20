#include "observation_controller.hpp"

#include "theme_manager.hpp"
#include "ui_session.hpp"

#include <QAccessible>
#include <QBuffer>
#include <QColorSpace>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLocale>
#include <QMetaEnum>
#include <QMouseEvent>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSaveFile>
#include <QSet>
#include <QTimer>
#include <QUuid>
#include <QWindow>

#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <utility>

namespace kearne::ui {
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
      !file.commit())
    throw std::runtime_error("could not write " + path.toStdString());
  QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
}

QJsonArray logicalBoundsFor(QQuickItem &item) {
  const QPointF scene = item.mapToScene(QPointF{});
  return {scene.x(), scene.y(), item.width(), item.height()};
}

QJsonArray screenBoundsFor(QQuickItem &item, QQuickWindow &window) {
  const QPointF scene = item.mapToScene(QPointF{});
  const QPoint position = window.position();
  return {scene.x() + position.x(), scene.y() + position.y(), item.width(),
          item.height()};
}

QJsonArray physicalBoundsFor(QQuickItem &item, QQuickWindow &window) {
  const qreal ratio = window.devicePixelRatio();
  const QPointF scene = item.mapToScene(QPointF{});
  return {scene.x() * ratio, scene.y() * ratio, item.width() * ratio,
          item.height() * ratio};
}

Qt::MouseButton pointerButton(const QJsonObject &input) {
  const QString name = input.value(QStringLiteral("button")).toString();
  if (name == QStringLiteral("left"))
    return Qt::LeftButton;
  if (name == QStringLiteral("middle"))
    return Qt::MiddleButton;
  if (name == QStringLiteral("right"))
    return Qt::RightButton;
  throw std::runtime_error(
      "pointer input requires left, middle, or right button");
}

Qt::KeyboardModifiers pointerModifiers(const QJsonObject &input) {
  Qt::KeyboardModifiers result = Qt::NoModifier;
  for (const QJsonValue value :
       input.value(QStringLiteral("modifiers")).toArray()) {
    const QString name = value.toString();
    if (name == QStringLiteral("shift"))
      result |= Qt::ShiftModifier;
    else if (name == QStringLiteral("control"))
      result |= Qt::ControlModifier;
    else if (name == QStringLiteral("alt"))
      result |= Qt::AltModifier;
    else
      throw std::runtime_error(
          "pointer input contains an unsupported modifier");
  }
  return result;
}

QPointF pointerPosition(QQuickItem &item, const QJsonObject &input,
                        const QString &field) {
  const QJsonArray position = input.value(field).toArray();
  if (position.size() != 2 || !position[0].isDouble() ||
      !position[1].isDouble())
    throw std::runtime_error(
        "pointer position requires two normalized numbers");
  const qreal x = position[0].toDouble();
  const qreal y = position[1].toDouble();
  if (x < 0.0 || x > 1.0 || y < 0.0 || y > 1.0)
    throw std::runtime_error("pointer position is outside the target");
  return item.mapToScene(QPointF{x * item.width(), y * item.height()});
}

void sendMouseEvent(QQuickWindow &window, QEvent::Type type,
                    const QPointF &position, Qt::MouseButton button,
                    Qt::MouseButtons buttons, Qt::KeyboardModifiers modifiers) {
  const QPointF global = window.mapToGlobal(position.toPoint());
  QMouseEvent event(type, position, global, button, buttons, modifiers);
  QCoreApplication::sendEvent(&window, &event);
}

QString accessibleRoleName(QAccessible::Role role) {
  const QMetaEnum roles = QMetaEnum::fromType<QAccessible::Role>();
  const char *name = roles.valueToKey(role);
  return name ? QString::fromLatin1(name)
              : QString::number(static_cast<int>(role));
}

QJsonObject accessibilityFor(QObject &object) {
  QJsonObject result{{QStringLiteral("present"), false}};
  QAccessibleInterface *interface =
      QAccessible::queryAccessibleInterface(&object);
  if (!interface || !interface->isValid())
    return result;

  const QAccessible::State state = interface->state();
  QStringList actions;
  if (QAccessibleActionInterface *action = interface->actionInterface())
    actions = action->actionNames();
  result = {
      {QStringLiteral("present"), true},
      {QStringLiteral("identifier"), interface->text(QAccessible::Identifier)},
      {QStringLiteral("name"), interface->text(QAccessible::Name)},
      {QStringLiteral("description"),
       interface->text(QAccessible::Description)},
      {QStringLiteral("role"), accessibleRoleName(interface->role())},
      {QStringLiteral("actions"), QJsonArray::fromStringList(actions)},
      {QStringLiteral("state"),
       QJsonObject{
           {QStringLiteral("disabled"), state.disabled != 0},
           {QStringLiteral("selected"), state.selected != 0},
           {QStringLiteral("focusable"), state.focusable != 0},
           {QStringLiteral("focused"), state.focused != 0},
           {QStringLiteral("pressed"), state.pressed != 0},
           {QStringLiteral("checkable"), state.checkable != 0},
           {QStringLiteral("checked"), state.checked != 0},
           {QStringLiteral("read_only"), state.readOnly != 0},
           {QStringLiteral("invisible"), state.invisible != 0},
           {QStringLiteral("offscreen"), state.offscreen != 0},
           {QStringLiteral("selectable"), state.selectable != 0},
           {QStringLiteral("editable"), state.editable != 0},
           {QStringLiteral("has_popup"), state.hasPopup != 0},
       }},
  };
  return result;
}

bool isEffectivelyVisible(QQuickItem &item, QQuickWindow &window) {
  if (!item.isVisible() || item.opacity() <= 0.0 || item.width() <= 0.0 ||
      item.height() <= 0.0)
    return false;
  QRectF visible =
      item.mapRectToScene(QRectF{0.0, 0.0, item.width(), item.height()});
  visible =
      visible.intersected(QRectF{0.0, 0.0, static_cast<qreal>(window.width()),
                                 static_cast<qreal>(window.height())});
  for (QQuickItem *parent = item.parentItem(); parent;
       parent = parent->parentItem()) {
    if (!parent->isVisible() || parent->opacity() <= 0.0)
      return false;
    if (parent->clip()) {
      visible = visible.intersected(parent->mapRectToScene(
          QRectF{0.0, 0.0, parent->width(), parent->height()}));
    }
  }
  return !visible.isEmpty();
}

QList<QObject *> traversalChildren(QObject &object) {
  QList<QObject *> children = object.children();
  if (auto *item = qobject_cast<QQuickItem *>(&object)) {
    for (QQuickItem *child : item->childItems()) {
      if (!children.contains(child))
        children.push_back(child);
    }
  }
  return children;
}

void collectSemanticNodes(QObject &object, QQuickWindow &window,
                          QJsonArray &nodes, QSet<QObject *> &visited,
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
    QJsonObject node{
        {QStringLiteral("id"), id},
        {QStringLiteral("name"), object.property("semanticName").toString()},
        {QStringLiteral("role"), object.property("semanticRole").toString()},
        {QStringLiteral("parent_id"), semanticParent},
        {QStringLiteral("enabled"), enabled},
        {QStringLiteral("visible"), visible},
        {QStringLiteral("focused"), focused},
        {QStringLiteral("actions"),
         QJsonArray::fromStringList(
             object.property("semanticActions").toStringList())},
        {QStringLiteral("action_handler"),
         object.metaObject()->indexOfMethod(
             "performSemanticAction(QVariant,QVariant)") >= 0},
        {QStringLiteral("accessibility"), accessibilityFor(object)},
    };
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
      node.insert(QStringLiteral("logical_bounds"),
                  QJsonArray{x, y, width, height});
      node.insert(
          QStringLiteral("physical_bounds"),
          QJsonArray{x * ratio, y * ratio, width * ratio, height * ratio});
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

QByteArray semanticFrameFingerprint(QQuickWindow &window) {
  QJsonArray nodes;
  QSet<QObject *> visited;
  collectSemanticNodes(window, window, nodes, visited);
  return QCryptographicHash::hash(
      QJsonDocument(nodes).toJson(QJsonDocument::Compact),
      QCryptographicHash::Sha256);
}

bool hasActiveSemanticTransition(QObject &object, QSet<QObject *> &visited) {
  if (visited.contains(&object))
    return false;
  visited.insert(&object);
  if (object.property("semanticRole").toString() == QStringLiteral("drawer") &&
      object.property("visible").toBool()) {
    const QVariant position = object.property("position");
    if (position.isValid() && position.toReal() < 0.999)
      return true;
  }
  for (QObject *child : traversalChildren(object)) {
    if (hasActiveSemanticTransition(*child, visited))
      return true;
  }
  return false;
}

bool hasActiveSemanticTransition(QObject &object) {
  QSet<QObject *> visited;
  return hasActiveSemanticTransition(object, visited);
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

QJsonArray jsonArray(const QList<QJsonObject> &objects) {
  QJsonArray result;
  for (const QJsonObject &object : objects)
    result.push_back(object);
  return result;
}

} // namespace

QList<QJsonObject>
parseSemanticOperations(const QStringList &actions,
                        const QStringList &encodedOperations) {
  QList<QJsonObject> result;
  result.reserve(actions.size() + encodedOperations.size());
  for (const QString &semanticId : actions) {
    result.push_back({{QStringLiteral("action"), QStringLiteral("invoke")},
                      {QStringLiteral("semantic_id"), semanticId}});
  }
  for (const QString &encoded : encodedOperations) {
    QJsonParseError error;
    const QJsonDocument document =
        QJsonDocument::fromJson(encoded.toUtf8(), &error);
    if (error.error != QJsonParseError::NoError || !document.isObject())
      throw std::runtime_error("--ui-operation must contain one JSON object");
    const QJsonObject operation = document.object();
    for (auto field = operation.constBegin(); field != operation.constEnd();
         ++field) {
      if (field.key() != QStringLiteral("action") &&
          field.key() != QStringLiteral("semantic_id") &&
          field.key() != QStringLiteral("value"))
        throw std::runtime_error("--ui-operation contains an unknown field");
    }
    if (!operation.value(QStringLiteral("action")).isString() ||
        operation.value(QStringLiteral("action")).toString().isEmpty() ||
        !operation.value(QStringLiteral("semantic_id")).isString() ||
        operation.value(QStringLiteral("semantic_id")).toString().isEmpty())
      throw std::runtime_error(
          "--ui-operation requires action and semantic_id");
    result.push_back(operation);
  }
  return result;
}

ObservationController::ObservationController(
    QQuickWindow &window, UiSession &session, ThemeManager &themes,
    QString outputDirectory, QList<QJsonObject> operations,
    std::function<bool()> presentationCurrent, QObject *parent)
    : QObject(parent), window_(window), session_(session), themes_(themes),
      outputDirectory_(std::move(outputDirectory)),
      pendingOperations_(std::move(operations)),
      sessionId_(QUuid::createUuid().toString(QUuid::WithoutBraces)),
      presentationCurrent_(std::move(presentationCurrent)) {
  connect(&window_, &QQuickWindow::frameSwapped, this, [this] {
    ++presentedFrames_;
    if (hasActiveSemanticTransition(window_) ||
        session_.commandDraftState() == QStringLiteral("pending") ||
        (presentationCurrent_ && !presentationCurrent_())) {
      settledFrames_ = 0;
      window_.update();
      return;
    }
    const QByteArray fingerprint = semanticFrameFingerprint(window_);
    if (fingerprint == lastFrameFingerprint_)
      ++settledFrames_;
    else {
      lastFrameFingerprint_ = fingerprint;
      settledFrames_ = 0;
    }
    if (settledFrames_ < 2) {
      window_.update();
      return;
    }
    if (!pendingOperations_.isEmpty() || actionScheduled_) {
      if (!pendingOperations_.isEmpty() && !actionScheduled_) {
        actionScheduled_ = true;
        QTimer::singleShot(0, this, [this] { performNextOperation(); });
      }
      return;
    }
    if (!captureScheduled_) {
      captureScheduled_ = true;
      QTimer::singleShot(0, this, [this] { capture(); });
    }
  });
  QTimer::singleShot(30'000, this, [this] {
    if (!captureScheduled_) {
      std::cerr << "capture deadline expired before two presented frames\n";
      QCoreApplication::exit(3);
    }
  });
  window_.update();
}

void ObservationController::performNextOperation() {
  try {
    const QJsonObject operation = pendingOperations_.takeFirst();
    const QString semanticId =
        operation.value(QStringLiteral("semantic_id")).toString();
    const QString action = operation.value(QStringLiteral("action")).toString();
    QObject *target = findSemanticObject(window_, semanticId);
    if (!target)
      throw std::runtime_error("semantic action target not found: " +
                               semanticId.toStdString());
    const QVariant enabled = target->property("enabled");
    if (enabled.isValid() && !enabled.toBool())
      throw std::runtime_error("semantic action target is disabled: " +
                               semanticId.toStdString());
    if (auto *item = qobject_cast<QQuickItem *>(target);
        item && !isEffectivelyVisible(*item, window_))
      throw std::runtime_error("semantic operation target is not visible: " +
                               semanticId.toStdString());
    if (!target->property("semanticActions").toStringList().contains(action)) {
      throw std::runtime_error(
          (QStringLiteral("semantic target does not support ") + action +
           QStringLiteral(": ") + semanticId)
              .toStdString());
    }

    const qulonglong generationBefore = session_.generation();
    QVariant accepted;
    const QVariant actionArgument = action;
    const QVariant valueArgument =
        operation.value(QStringLiteral("value")).toVariant();
    if ((action == QStringLiteral("pointerClick") ||
         action == QStringLiteral("pointerDrag"))) {
      auto *item = qobject_cast<QQuickItem *>(target);
      const QJsonObject input =
          operation.value(QStringLiteral("value")).toObject();
      if (!item || input.isEmpty())
        throw std::runtime_error(
            "pointer input requires an item and object value");
      const Qt::MouseButton button = pointerButton(input);
      const Qt::KeyboardModifiers modifiers = pointerModifiers(input);
      const QString startField = action == QStringLiteral("pointerClick")
                                     ? QStringLiteral("position")
                                     : QStringLiteral("from");
      const QPointF start = pointerPosition(*item, input, startField);
      const QPointF finish =
          action == QStringLiteral("pointerClick")
              ? start
              : pointerPosition(*item, input, QStringLiteral("to"));
      sendMouseEvent(window_, QEvent::MouseButtonPress, start, button, button,
                     modifiers);
      if (action == QStringLiteral("pointerDrag")) {
        for (int step = 1; step <= 4; ++step) {
          const qreal progress = static_cast<qreal>(step) / 4.0;
          sendMouseEvent(window_, QEvent::MouseMove,
                         start + (finish - start) * progress, Qt::NoButton,
                         button, modifiers);
        }
      }
      sendMouseEvent(window_, QEvent::MouseButtonRelease, finish, button,
                     Qt::NoButton, modifiers);
      accepted = true;
    } else if (!QMetaObject::invokeMethod(target, "performSemanticAction",
                                          Qt::DirectConnection,
                                          Q_RETURN_ARG(QVariant, accepted),
                                          Q_ARG(QVariant, actionArgument),
                                          Q_ARG(QVariant, valueArgument)) ||
               !accepted.toBool()) {
      throw std::runtime_error((QStringLiteral("semantic control rejected ") +
                                action + QStringLiteral(": ") + semanticId)
                                   .toStdString());
    }
    QJsonObject receipt{
        {QStringLiteral("semantic_id"), semanticId},
        {QStringLiteral("action"), action},
        {QStringLiteral("generation_before"),
         static_cast<qint64>(generationBefore)},
        {QStringLiteral("generation_after"),
         static_cast<qint64>(session_.generation())},
    };
    if (operation.contains(QStringLiteral("value")))
      receipt.insert(QStringLiteral("value"),
                     operation.value(QStringLiteral("value")));
    actionReceipts_.push_back(receipt);
    actionScheduled_ = false;
    settledFrames_ = 0;
    lastFrameFingerprint_.clear();
    window_.update();
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    QCoreApplication::exit(4);
  }
}

void ObservationController::capture() {
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
    const QString imagePath =
        QDir(outputDirectory_)
            .filePath(QStringLiteral("application-session.png"));
    writeFile(imagePath, png);

    QJsonArray nodes;
    QSet<QObject *> visited;
    collectSemanticNodes(window_, window_, nodes, visited);
    const QJsonArray receipts = jsonArray(actionReceipts_);
    const QJsonObject semantic{
        {QStringLiteral("schema"), QStringLiteral("kearne.semantic-ui/v1")},
        {QStringLiteral("surface_id"), session_.activeSurfaceId()},
        {QStringLiteral("workspace_id"), session_.activeWorkspaceId()},
        {QStringLiteral("project_revision"), session_.projectRevision()},
        {QStringLiteral("theme_id"), themes_.activeThemeId()},
        {QStringLiteral("ui_generation"),
         static_cast<qint64>(session_.generation())},
        {QStringLiteral("actions"), receipts},
        {QStringLiteral("nodes"), nodes},
    };
    const QString semanticPath =
        QDir(outputDirectory_).filePath(QStringLiteral("semantic-ui.json"));
    writeFile(semanticPath,
              QJsonDocument(semantic).toJson(QJsonDocument::Indented));

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
        {QStringLiteral("theme_id"), themes_.activeThemeId()},
        {QStringLiteral("theme_selection"), themes_.selectionId()},
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
        {QStringLiteral("project_revision"), session_.projectRevision()},
        {QStringLiteral("presented_frames"), presentedFrames_},
        {QStringLiteral("actions"), receipts},
        {QStringLiteral("image"),
         QJsonObject{
             {QStringLiteral("path"), imagePath},
             {QStringLiteral("pixel_width"), image.width()},
             {QStringLiteral("pixel_height"), image.height()},
             {QStringLiteral("logical_width"), window_.width()},
             {QStringLiteral("logical_height"), window_.height()},
             {QStringLiteral("device_pixel_ratio"), window_.devicePixelRatio()},
             {QStringLiteral("format"), QStringLiteral("PNG")},
             {QStringLiteral("color_space"), QStringLiteral("sRGB")},
             {QStringLiteral("sha256"), QString::fromLatin1(digest)},
             {QStringLiteral("statistics"), imageStatistics(image)},
         }},
        {QStringLiteral("semantic_snapshot"), semanticPath},
    };
    const QString metadataPath =
        QDir(outputDirectory_).filePath(QStringLiteral("capture.json"));
    writeFile(metadataPath,
              QJsonDocument(metadata).toJson(QJsonDocument::Indented));
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

} // namespace kearne::ui
