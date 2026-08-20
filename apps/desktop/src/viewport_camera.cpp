#include "viewport_camera.hpp"

#include <QByteArray>
#include <Qt>

#include <algorithm>
#include <cmath>
#include <string_view>
#include <type_traits>

namespace kearne::ui {
namespace {

constexpr qreal minimumDistance = 36.0;
constexpr qreal maximumDistance = 1600.0;
constexpr qreal maximumPan = 1'000'000.0;

qreal wrappedDegrees(qreal degrees) {
  degrees = std::fmod(degrees, 360.0);
  return degrees <= -180.0 ? degrees + 360.0
         : degrees > 180.0 ? degrees - 360.0
                           : degrees;
}

bool finiteCamera(qreal yaw, qreal pitch, qreal roll, qreal panX, qreal panY,
                  qreal distance) {
  return std::isfinite(yaw) && std::isfinite(pitch) && std::isfinite(roll) &&
         std::isfinite(panX) && std::isfinite(panY) && std::isfinite(distance);
}

QString text(std::string_view value) {
  return QString::fromLatin1(value.data(),
                             static_cast<qsizetype>(value.size()));
}

PointerButtonMask pointerButtons(int buttons) {
  auto result = PointerButtonMask::None;
  if ((buttons & static_cast<int>(Qt::LeftButton)) != 0)
    result = result | PointerButtonMask::Left;
  if ((buttons & static_cast<int>(Qt::MiddleButton)) != 0)
    result = result | PointerButtonMask::Middle;
  if ((buttons & static_cast<int>(Qt::RightButton)) != 0)
    result = result | PointerButtonMask::Right;
  return result;
}

PointerModifierMask pointerModifiers(int modifiers) {
  auto result = PointerModifierMask::None;
  if ((modifiers & static_cast<int>(Qt::ShiftModifier)) != 0)
    result = result | PointerModifierMask::Shift;
  if ((modifiers & static_cast<int>(Qt::ControlModifier)) != 0)
    result = result | PointerModifierMask::Control;
  if ((modifiers & static_cast<int>(Qt::AltModifier)) != 0)
    result = result | PointerModifierMask::Alt;
  if ((modifiers & static_cast<int>(Qt::MetaModifier)) != 0)
    result = result | PointerModifierMask::Meta;
  return result;
}

QString standardViewId(StandardView view) {
  switch (view) {
  case StandardView::Front:
    return QStringLiteral("front");
  case StandardView::Back:
    return QStringLiteral("back");
  case StandardView::Left:
    return QStringLiteral("left");
  case StandardView::Right:
    return QStringLiteral("right");
  case StandardView::Top:
    return QStringLiteral("top");
  case StandardView::Bottom:
    return QStringLiteral("bottom");
  case StandardView::Isometric:
    return QStringLiteral("isometric");
  }
  return {};
}

} // namespace

ViewportCamera::ViewportCamera(QString navigationProfile, bool zoomReversed,
                               QObject *parent)
    : QObject(parent), zoomReversed_(zoomReversed) {
  setNavigationProfile(navigationProfile);
}

QString ViewportCamera::navigationProfile() const {
  return text(navigationProfileId(navigationProfile_));
}
bool ViewportCamera::zoomReversed() const { return zoomReversed_; }
qreal ViewportCamera::yaw() const { return yaw_; }
qreal ViewportCamera::pitch() const { return pitch_; }
qreal ViewportCamera::roll() const { return roll_; }
qreal ViewportCamera::panX() const { return panX_; }
qreal ViewportCamera::panY() const { return panY_; }
qreal ViewportCamera::distance() const { return distance_; }
QString ViewportCamera::viewName() const { return viewName_; }

QString ViewportCamera::state() const {
  return QStringLiteral("%1:%2:%3,%4,%5,%6,%7,%8")
      .arg(navigationProfile(), viewName_)
      .arg(yaw_, 0, 'f', 1)
      .arg(pitch_, 0, 'f', 1)
      .arg(roll_, 0, 'f', 1)
      .arg(panX_, 0, 'f', 1)
      .arg(panY_, 0, 'f', 1)
      .arg(distance_, 0, 'f', 1);
}

void ViewportCamera::setNavigationProfile(const QString &profile) {
  const QByteArray bytes = profile.toLatin1();
  const auto parsed = navigationProfileFromId(std::string_view{
      bytes.constData(), static_cast<std::size_t>(bytes.size())});
  if (!parsed || navigationProfile_ == *parsed)
    return;
  navigationProfile_ = *parsed;
  emit navigationProfileChanged();
  emit cameraChanged();
}

void ViewportCamera::setZoomReversed(bool reversed) {
  if (zoomReversed_ == reversed)
    return;
  zoomReversed_ = reversed;
  emit zoomReversedChanged();
  emit cameraChanged();
}

bool ViewportCamera::applyPointerDrag(int buttons, int modifiers, qreal dx,
                                      qreal dy) {
  const auto action =
      mapPointerDrag(navigationProfile_,
                     PointerDragInput{pointerButtons(buttons),
                                      pointerModifiers(modifiers), dx, dy});
  return action && applyNavigation(*action);
}

void ViewportCamera::applyWheel(qreal angleDeltaY) {
  static_cast<void>(applyNavigation(
      ZoomNavigation{(zoomReversed_ ? -1.0 : 1.0) * angleDeltaY / 120.0}));
}

void ViewportCamera::applySpaceMotion(qreal tx, qreal ty, qreal tz, qreal rx,
                                      qreal ry, qreal rz, qreal periodMs) {
  if (!finiteCamera(tx, ty, tz, rx, ry, rz) || !std::isfinite(periodMs))
    return;
  const qreal timeScale = std::clamp(periodMs / 16.0, 0.1, 4.0);
  const qreal nextDistance = distance_ * std::exp(-tz * 0.08 * timeScale);
  changeCamera(yaw_ + ry * 2.4 * timeScale, pitch_ - rx * 2.4 * timeScale,
               roll_ + rz * 2.0 * timeScale, panX_ + tx * 14.0 * timeScale,
               panY_ - ty * 14.0 * timeScale, nextDistance,
               QStringLiteral("custom"));
}

void ViewportCamera::orbit(qreal dx, qreal dy) {
  changeCamera(yaw_ + dx * 0.35, pitch_ + dy * 0.30, roll_, panX_, panY_,
               distance_, QStringLiteral("custom"));
}

void ViewportCamera::pan(qreal dx, qreal dy) {
  changeCamera(yaw_, pitch_, roll_, panX_ + dx, panY_ + dy, distance_,
               QStringLiteral("custom"));
}

void ViewportCamera::zoom(qreal amount) {
  if (!std::isfinite(amount))
    return;
  changeCamera(yaw_, pitch_, roll_, panX_, panY_,
               distance_ * std::exp(-amount * 0.18), viewName_);
}

void ViewportCamera::fit() {
  changeCamera(yaw_, pitch_, roll_, 0.0, 0.0, 220.0, viewName_);
}

bool ViewportCamera::setView(const QString &view) {
  qreal yaw = 0.0;
  qreal pitch = 0.0;
  if (view == QStringLiteral("front")) {
  } else if (view == QStringLiteral("back")) {
    yaw = 180.0;
  } else if (view == QStringLiteral("left")) {
    yaw = 90.0;
  } else if (view == QStringLiteral("right")) {
    yaw = -90.0;
  } else if (view == QStringLiteral("top")) {
    pitch = -90.0;
  } else if (view == QStringLiteral("bottom")) {
    pitch = 90.0;
  } else if (view == QStringLiteral("isometric")) {
    yaw = 45.0;
    pitch = -30.0;
  } else {
    return false;
  }
  changeCamera(yaw, pitch, 0.0, 0.0, 0.0, distance_, view);
  return true;
}

bool ViewportCamera::applyNavigation(const NavigationAction &action) {
  if (!validNavigationAction(action))
    return false;
  return std::visit(
      [this]<typename Value>(const Value &value) {
        using Type = std::decay_t<Value>;
        if constexpr (std::is_same_v<Type, OrbitNavigation>) {
          orbit(value.deltaXLogicalPixels, value.deltaYLogicalPixels);
        } else if constexpr (std::is_same_v<Type, PanNavigation>) {
          pan(value.deltaXLogicalPixels, value.deltaYLogicalPixels);
        } else if constexpr (std::is_same_v<Type, ZoomNavigation>) {
          zoom(value.amount);
        } else if constexpr (std::is_same_v<Type, RollNavigation>) {
          changeCamera(yaw_, pitch_, roll_ + value.amount, panX_, panY_,
                       distance_, QStringLiteral("custom"));
        } else if constexpr (std::is_same_v<Type, SpaceMotionNavigation>) {
          applySpaceMotion(value.translationX, value.translationY,
                           value.translationZ, value.rotationX, value.rotationY,
                           value.rotationZ);
        } else if constexpr (std::is_same_v<Type, FitNavigation>) {
          fit();
        } else if constexpr (std::is_same_v<Type, StandardViewNavigation>) {
          return setView(standardViewId(value.view));
        }
        return true;
      },
      action);
}

void ViewportCamera::changeCamera(qreal yaw, qreal pitch, qreal roll,
                                  qreal panX, qreal panY, qreal distance,
                                  const QString &viewName) {
  if (!finiteCamera(yaw, pitch, roll, panX, panY, distance))
    return;
  yaw = wrappedDegrees(yaw);
  pitch = std::clamp(pitch, -90.0, 90.0);
  roll = wrappedDegrees(roll);
  panX = std::clamp(panX, -maximumPan, maximumPan);
  panY = std::clamp(panY, -maximumPan, maximumPan);
  distance = std::clamp(distance, minimumDistance, maximumDistance);
  if (qFuzzyCompare(yaw_ + 1.0, yaw + 1.0) &&
      qFuzzyCompare(pitch_ + 1.0, pitch + 1.0) &&
      qFuzzyCompare(roll_ + 1.0, roll + 1.0) &&
      qFuzzyCompare(panX_ + 1.0, panX + 1.0) &&
      qFuzzyCompare(panY_ + 1.0, panY + 1.0) &&
      qFuzzyCompare(distance_ + 1.0, distance + 1.0) && viewName_ == viewName)
    return;
  yaw_ = yaw;
  pitch_ = pitch;
  roll_ = roll;
  panX_ = panX;
  panY_ = panY;
  distance_ = distance;
  viewName_ = viewName;
  emit cameraChanged();
}

} // namespace kearne::ui
