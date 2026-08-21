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
#include <QFileInfo>
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
#include <chrono>
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

struct ObservationController::PendingPointerMotion final {
  QPointF start;
  QPointF finish;
  Qt::MouseButton button = Qt::NoButton;
  Qt::KeyboardModifiers modifiers = Qt::NoModifier;
  std::optional<std::chrono::steady_clock::time_point> moveDispatched;
  QJsonArray movePresentations;
  int nextMove = 1;
  bool capturePreview = false;
  bool previewCaptured = false;
  bool releaseAtEnd = false;
  static constexpr int moveCount = 20;
};

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
  connect(&window_, &QQuickWindow::frameSwapped, this,
          [this] { framePresented(); });
  QTimer::singleShot(30'000, this, [this] {
    if (!captureScheduled_) {
      std::cerr << "capture deadline expired before two presented frames\n";
      QCoreApplication::exit(3);
    }
  });
  window_.update();
}

ObservationController::~ObservationController() = default;

void ObservationController::framePresented() {
  ++presentedFrames_;
  if (pointerMotion_ && pointerMotion_->moveDispatched) {
    pointerMotion_->movePresentations.push_back(QJsonObject{
        {QStringLiteral("move"), pointerMotion_->nextMove - 1},
        {QStringLiteral("frame"), presentedFrames_},
        {QStringLiteral("latency_ms"),
         std::chrono::duration<double, std::milli>(
             std::chrono::steady_clock::now() - *pointerMotion_->moveDispatched)
             .count()},
        {QStringLiteral("preview_visible"),
         session_.sketchGesturePreviewVisible()},
        {QStringLiteral("hovered_entity"),
         session_.sketchHoveredEntityId()},
        {QStringLiteral("hovered_point"), session_.sketchHoveredPointKey()}});
    pointerMotion_->moveDispatched.reset();
    if (pointerMotion_->capturePreview && !pointerMotion_->previewCaptured &&
        pointerMotion_->nextMove > PendingPointerMotion::moveCount) {
      capturePreviewImage();
      pointerMotion_->previewCaptured = true;
    }
  }
  recordPresentedState();
  if (pointerPreviewCapturePending_) {
    capturePreviewImage();
    pointerPreviewCapturePending_ = false;
  }

  if (pointerMotion_) {
    settledFrames_ = 0;
    if (!pointerStepScheduled_) {
      pointerStepScheduled_ = true;
      QTimer::singleShot(0, this, [this] { continuePointerMotion(); });
    }
    return;
  }
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
  finishActiveReceipt();
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
}

void ObservationController::capturePreviewImage() {
  try {
    if (!activeReceipt_)
      throw std::runtime_error("pointer preview has no active receipt");
    if (!QDir{}.mkpath(outputDirectory_))
      throw std::runtime_error("could not create preview capture directory");
    QImage preview = window_.grabWindow();
    if (preview.isNull())
      throw std::runtime_error("pointer preview capture was empty");
    const QString path = QDir(outputDirectory_)
                             .filePath(QStringLiteral("pointer-preview-%1.png")
                                           .arg(*activeReceipt_ + 1));
    writeFile(path, encodedPng(preview));
    QJsonArray nodes;
    QSet<QObject *> visited;
    collectSemanticNodes(window_, window_, nodes, visited);
    const QString semanticPath =
        QDir(outputDirectory_)
            .filePath(QStringLiteral("pointer-preview-%1.semantic.json")
                          .arg(*activeReceipt_ + 1));
    writeFile(semanticPath,
              QJsonDocument(QJsonObject{
                                {QStringLiteral("schema"),
                                 QStringLiteral("kearne.pointer-preview/v1")},
                                {QStringLiteral("frame"), presentedFrames_},
                                {QStringLiteral("ui_generation"),
                                 static_cast<qint64>(session_.generation())},
                                {QStringLiteral("command_state"),
                                 session_.commandDraftState()},
                                {QStringLiteral("nodes"), nodes},
                            })
                  .toJson(QJsonDocument::Indented));
    actionReceipts_[*activeReceipt_].insert(QStringLiteral("preview_image"),
                                            QFileInfo(path).fileName());
    actionReceipts_[*activeReceipt_].insert(QStringLiteral("preview_semantic"),
                                            QFileInfo(semanticPath).fileName());
    actionReceipts_[*activeReceipt_].insert(QStringLiteral("preview_frame"),
                                            presentedFrames_);
    actionReceipts_[*activeReceipt_].insert(
        QStringLiteral("preview_generation"),
        static_cast<qint64>(session_.generation()));
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    QCoreApplication::exit(4);
  }
}

void ObservationController::recordPresentedState() {
  if (!activeReceipt_ || !actionStarted_)
    return;
  QJsonObject &receipt = actionReceipts_[*activeReceipt_];
  QJsonArray frames = receipt.value(QStringLiteral("presented")).toArray();
  const double elapsed = std::chrono::duration<double, std::milli>(
                             std::chrono::steady_clock::now() - *actionStarted_)
                             .count();
  const QJsonObject frame{
      {QStringLiteral("frame"), presentedFrames_},
      {QStringLiteral("elapsed_ms"), elapsed},
      {QStringLiteral("ui_generation"),
       static_cast<qint64>(session_.generation())},
      {QStringLiteral("command_state"), session_.commandDraftState()},
      {QStringLiteral("project_revision"), session_.projectRevision()},
      {QStringLiteral("native_scene_current"),
       !presentationCurrent_ || presentationCurrent_()},
      {QStringLiteral("sketch_preview_visible"),
       session_.sketchGesturePreviewVisible()},
  };
  const bool changed =
      frames.isEmpty() ||
      frames.last().toObject().value(QStringLiteral("ui_generation")) !=
          frame.value(QStringLiteral("ui_generation")) ||
      frames.last().toObject().value(QStringLiteral("command_state")) !=
          frame.value(QStringLiteral("command_state")) ||
      frames.last().toObject().value(QStringLiteral("project_revision")) !=
          frame.value(QStringLiteral("project_revision")) ||
      frames.last().toObject().value(QStringLiteral("native_scene_current")) !=
          frame.value(QStringLiteral("native_scene_current")) ||
      frames.last().toObject().value(
          QStringLiteral("sketch_preview_visible")) !=
          frame.value(QStringLiteral("sketch_preview_visible"));
  constexpr qsizetype maximumTraceFrames = 64;
  if (changed && frames.size() < maximumTraceFrames)
    frames.push_back(frame);
  else if (changed) {
    frames[maximumTraceFrames - 1] = frame;
    receipt.insert(QStringLiteral("presented_trace_truncated"), true);
  }
  receipt.insert(QStringLiteral("presented"), frames);
}

void ObservationController::finishActiveReceipt() {
  if (!activeReceipt_ || !actionStarted_)
    return;
  QJsonObject &receipt = actionReceipts_[*activeReceipt_];
  const QString settledRevision = session_.projectRevision();
  const QJsonArray frames =
      receipt.value(QStringLiteral("presented")).toArray();
  const auto currentScene = std::find_if(
      frames.cbegin(), frames.cend(),
      [&settledRevision](const QJsonValue &value) {
        const QJsonObject frame = value.toObject();
        return frame.value(QStringLiteral("native_scene_current")).toBool() &&
               frame.value(QStringLiteral("project_revision")).toString() ==
                   settledRevision;
      });
  if (currentScene != frames.cend()) {
    const QJsonObject frame = currentScene->toObject();
    const double currentMilliseconds =
        frame.value(QStringLiteral("elapsed_ms")).toDouble();
    receipt.insert(QStringLiteral("current_scene_frame"),
                   frame.value(QStringLiteral("frame")));
    receipt.insert(QStringLiteral("current_scene_ms"), currentMilliseconds);
    if (receipt.contains(QStringLiteral("input_complete_ms"))) {
      receipt.insert(
          QStringLiteral("current_scene_after_input_ms"),
          std::max(0.0, currentMilliseconds -
                            receipt.value(QStringLiteral("input_complete_ms"))
                                .toDouble()));
    }
  }
  receipt.insert(QStringLiteral("settled_frame"), presentedFrames_);
  receipt.insert(QStringLiteral("settled_ms"),
                 std::chrono::duration<double, std::milli>(
                     std::chrono::steady_clock::now() - *actionStarted_)
                     .count());
  receipt.insert(QStringLiteral("settled_generation"),
                 static_cast<qint64>(session_.generation()));
  receipt.insert(QStringLiteral("settled_state"), session_.commandDraftState());
  receipt.insert(QStringLiteral("settled_project_revision"), settledRevision);
  receipt.insert(QStringLiteral("settled_native_scene_current"),
                 !presentationCurrent_ || presentationCurrent_());
  if (inputCompleted_) {
    receipt.insert(QStringLiteral("settled_after_input_ms"),
                   std::chrono::duration<double, std::milli>(
                       std::chrono::steady_clock::now() - *inputCompleted_)
                       .count());
  }
  activeReceipt_.reset();
  actionStarted_.reset();
  inputCompleted_.reset();
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
    const auto dispatchedAt = std::chrono::steady_clock::now();
    QVariant accepted;
    const QVariant actionArgument = action;
    const QVariant valueArgument =
        operation.value(QStringLiteral("value")).toVariant();
    if (action == QStringLiteral("pointerClick") ||
        action == QStringLiteral("pointerDrag") ||
        action == QStringLiteral("pointerHover")) {
      auto *item = qobject_cast<QQuickItem *>(target);
      const QJsonObject input =
          operation.value(QStringLiteral("value")).toObject();
      if (!item || input.isEmpty())
        throw std::runtime_error(
            "pointer input requires an item and object value");
      const Qt::KeyboardModifiers modifiers = pointerModifiers(input);
      const bool hoverPath = action == QStringLiteral("pointerHover") &&
                             input.contains(QStringLiteral("from")) &&
                             input.contains(QStringLiteral("to"));
      const QString startField =
          action == QStringLiteral("pointerDrag") || hoverPath
              ? QStringLiteral("from")
              : QStringLiteral("position");
      const QPointF start = pointerPosition(*item, input, startField);
      if (hoverPath) {
        pointerMotion_ =
            std::make_unique<PendingPointerMotion>(PendingPointerMotion{
                .start = start,
                .finish = pointerPosition(*item, input, QStringLiteral("to")),
                .button = Qt::NoButton,
                .modifiers = modifiers,
                .moveDispatched = std::nullopt,
                .movePresentations = {},
                .nextMove = 1,
                .capturePreview = input.value(QStringLiteral("capture_preview"))
                                      .toBool(false),
                .previewCaptured = false,
                .releaseAtEnd = false,
            });
      } else if (action == QStringLiteral("pointerHover")) {
        sendMouseEvent(window_, QEvent::MouseMove, start, Qt::NoButton,
                       Qt::NoButton, modifiers);
      } else {
        const Qt::MouseButton button = pointerButton(input);
        const QPointF finish =
            action == QStringLiteral("pointerClick")
                ? start
                : pointerPosition(*item, input, QStringLiteral("to"));
        if (action == QStringLiteral("pointerDrag")) {
          sendMouseEvent(window_, QEvent::MouseButtonPress, start, button,
                         button, modifiers);
          pointerMotion_ =
              std::make_unique<PendingPointerMotion>(PendingPointerMotion{
                  .start = start,
                  .finish = finish,
                  .button = button,
                  .modifiers = modifiers,
                  .moveDispatched = std::nullopt,
                  .movePresentations = {},
                  .nextMove = 1,
                  .capturePreview =
                      input.value(QStringLiteral("capture_preview"))
                          .toBool(false),
                  .previewCaptured = false,
                  .releaseAtEnd = true,
              });
        } else {
          sendMouseEvent(window_, QEvent::MouseButtonPress, start, button,
                         button, modifiers);
          sendMouseEvent(window_, QEvent::MouseButtonRelease, finish, button,
                         Qt::NoButton, modifiers);
        }
      }
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
    const double dispatchMilliseconds =
        std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - dispatchedAt)
            .count();
    QJsonObject receipt{
        {QStringLiteral("semantic_id"), semanticId},
        {QStringLiteral("action"), action},
        {QStringLiteral("generation_before"),
         static_cast<qint64>(generationBefore)},
        {QStringLiteral("generation_after"),
         static_cast<qint64>(session_.generation())},
        {QStringLiteral("dispatch_ms"), dispatchMilliseconds},
        {QStringLiteral("state_after_dispatch"), session_.commandDraftState()},
        {QStringLiteral("project_revision_after_dispatch"),
         session_.projectRevision()},
        {QStringLiteral("presented_frame_before"), presentedFrames_},
        {QStringLiteral("presented"), QJsonArray{}},
    };
    if (operation.contains(QStringLiteral("value")))
      receipt.insert(QStringLiteral("value"),
                     operation.value(QStringLiteral("value")));
    activeReceipt_ = actionReceipts_.size();
    actionStarted_ = dispatchedAt;
    actionReceipts_.push_back(receipt);
    pointerPreviewCapturePending_ =
        action == QStringLiteral("pointerHover") && !pointerMotion_ &&
        operation.value(QStringLiteral("value"))
            .toObject()
            .value(QStringLiteral("capture_preview"))
            .toBool(false);
    if (!pointerMotion_)
      actionScheduled_ = false;
    settledFrames_ = 0;
    lastFrameFingerprint_.clear();
    window_.update();
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    QCoreApplication::exit(4);
  }
}

void ObservationController::continuePointerMotion() {
  pointerStepScheduled_ = false;
  if (!pointerMotion_)
    return;
  if (pointerMotion_->nextMove <= PendingPointerMotion::moveCount) {
    const qreal progress = static_cast<qreal>(pointerMotion_->nextMove) /
                           static_cast<qreal>(PendingPointerMotion::moveCount);
    pointerMotion_->moveDispatched = std::chrono::steady_clock::now();
    sendMouseEvent(
        window_, QEvent::MouseMove,
        pointerMotion_->start +
            (pointerMotion_->finish - pointerMotion_->start) * progress,
        Qt::NoButton, pointerMotion_->button, pointerMotion_->modifiers);
    ++pointerMotion_->nextMove;
    window_.update();
    return;
  }
  const bool releaseAtEnd = pointerMotion_->releaseAtEnd;
  if (releaseAtEnd) {
    sendMouseEvent(window_, QEvent::MouseButtonRelease, pointerMotion_->finish,
                   pointerMotion_->button, Qt::NoButton,
                   pointerMotion_->modifiers);
    inputCompleted_ = std::chrono::steady_clock::now();
  }
  const QJsonArray movePresentations = pointerMotion_->movePresentations;
  pointerMotion_.reset();
  actionScheduled_ = false;
  if (activeReceipt_) {
    QJsonObject &receipt = actionReceipts_[*activeReceipt_];
    receipt.insert(QStringLiteral("generation_after"),
                   static_cast<qint64>(session_.generation()));
    if (releaseAtEnd)
      receipt.insert(QStringLiteral("state_after_input"),
                     session_.commandDraftState());
    receipt.insert(QStringLiteral("pointer_move_presentations"),
                   movePresentations.size());
    receipt.insert(QStringLiteral("pointer_move_frames"), movePresentations);
    if (inputCompleted_)
      receipt.insert(QStringLiteral("input_complete_ms"),
                     std::chrono::duration<double, std::milli>(
                         *inputCompleted_ - *actionStarted_)
                         .count());
  }
  window_.update();
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
    QJsonObject semantic{
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
    if (auto *sourceEditor = qobject_cast<QQuickItem *>(findSemanticObject(
            window_, QStringLiteral("inspector.source.editor")));
        sourceEditor && isEffectivelyVisible(*sourceEditor, window_)) {
      const QVariantMap selectedFunction = session_.selectedFunction();
      semantic.insert(
          QStringLiteral("source_snapshot"),
          QJsonObject{
              {QStringLiteral("path"),
               selectedFunction.value(QStringLiteral("sourcePath")).toString()},
              {QStringLiteral("revision"),
               selectedFunction.value(QStringLiteral("revision")).toString()},
              {QStringLiteral("source"), session_.modelSource()},
          });
    }
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
