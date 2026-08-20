#include "navigation_input.hpp"

#include <QThread>

#include <cmath>
#include <limits>
#include <type_traits>
#include <utility>

namespace kearne::ui {
namespace {

constexpr std::uint8_t knownButtons =
    static_cast<std::uint8_t>(PointerButtonMask::Left) |
    static_cast<std::uint8_t>(PointerButtonMask::Middle) |
    static_cast<std::uint8_t>(PointerButtonMask::Right);
constexpr std::uint8_t knownModifiers =
    static_cast<std::uint8_t>(PointerModifierMask::Shift) |
    static_cast<std::uint8_t>(PointerModifierMask::Control) |
    static_cast<std::uint8_t>(PointerModifierMask::Alt) |
    static_cast<std::uint8_t>(PointerModifierMask::Meta);

template <typename Mask> constexpr bool contains(Mask value, Mask flag) {
  return (static_cast<std::uint8_t>(value) & static_cast<std::uint8_t>(flag)) !=
         0U;
}

constexpr bool valid(StandardView view) {
  return view >= StandardView::Front && view <= StandardView::Isometric;
}

} // namespace

std::optional<NavigationProfile>
navigationProfileFromId(std::string_view id) noexcept {
  if (id == "fusion")
    return NavigationProfile::Fusion;
  if (id == "solidworks")
    return NavigationProfile::SolidWorks;
  if (id == "onshape")
    return NavigationProfile::Onshape;
  return std::nullopt;
}

std::string_view navigationProfileId(NavigationProfile profile) noexcept {
  switch (profile) {
  case NavigationProfile::Fusion:
    return "fusion";
  case NavigationProfile::SolidWorks:
    return "solidworks";
  case NavigationProfile::Onshape:
    return "onshape";
  }
  return {};
}

bool validNavigationAction(const NavigationAction &action) noexcept {
  return std::visit(
      []<typename Value>(const Value &value) {
        using Type = std::decay_t<Value>;
        if constexpr (std::is_same_v<Type, OrbitNavigation> ||
                      std::is_same_v<Type, PanNavigation>) {
          return std::isfinite(value.deltaXLogicalPixels) &&
                 std::isfinite(value.deltaYLogicalPixels);
        } else if constexpr (std::is_same_v<Type, ZoomNavigation> ||
                             std::is_same_v<Type, RollNavigation>) {
          return std::isfinite(value.amount);
        } else if constexpr (std::is_same_v<Type, SpaceMotionNavigation>) {
          return std::isfinite(value.translationX) &&
                 std::isfinite(value.translationY) &&
                 std::isfinite(value.translationZ) &&
                 std::isfinite(value.rotationX) &&
                 std::isfinite(value.rotationY) &&
                 std::isfinite(value.rotationZ);
        } else if constexpr (std::is_same_v<Type, StandardViewNavigation>) {
          return valid(value.view);
        } else {
          return true;
        }
      },
      action);
}

std::optional<NavigationAction>
mapPointerDrag(NavigationProfile profile,
               const PointerDragInput &input) noexcept {
  const auto buttons = static_cast<std::uint8_t>(input.buttons);
  const auto modifiers = static_cast<std::uint8_t>(input.modifiers);
  if ((buttons & ~knownButtons) != 0U || (modifiers & ~knownModifiers) != 0U ||
      !std::isfinite(input.deltaXLogicalPixels) ||
      !std::isfinite(input.deltaYLogicalPixels))
    return std::nullopt;

  const bool middle = contains(input.buttons, PointerButtonMask::Middle);
  const bool right = contains(input.buttons, PointerButtonMask::Right);
  const bool shift = contains(input.modifiers, PointerModifierMask::Shift);
  const bool control = contains(input.modifiers, PointerModifierMask::Control);
  const OrbitNavigation orbit{input.deltaXLogicalPixels,
                              input.deltaYLogicalPixels};
  const PanNavigation pan{input.deltaXLogicalPixels, input.deltaYLogicalPixels};

  switch (profile) {
  case NavigationProfile::Fusion:
    if (middle && shift)
      return NavigationAction{orbit};
    if (middle)
      return NavigationAction{pan};
    break;
  case NavigationProfile::SolidWorks:
    if (middle && control)
      return NavigationAction{pan};
    if (middle)
      return NavigationAction{orbit};
    break;
  case NavigationProfile::Onshape:
    if ((right && control) || middle)
      return NavigationAction{pan};
    if (right)
      return NavigationAction{orbit};
    break;
  }
  return std::nullopt;
}

NavigationTargetRouter::NavigationTargetRouter(QObject &fallback,
                                               QObject *parent)
    : QObject(parent),
      fallback_(dynamic_cast<NavigationTargetRouter *>(&fallback)
                    ? nullptr
                    : dynamic_cast<NavigationTarget *>(&fallback)),
      fallbackLifetime_(fallback_ && fallback.thread() == thread() ? &fallback
                                                                   : nullptr) {
  if (!fallbackLifetime_)
    fallback_ = nullptr;
}

NavigationRouteClaim NavigationTargetRouter::claim(QObject &target) {
  auto *navigation = dynamic_cast<NavigationTarget *>(&target);
  if (!navigation || dynamic_cast<NavigationTargetRouter *>(&target) ||
      QThread::currentThread() != thread() || target.thread() != thread() ||
      claimClock_ == std::numeric_limits<std::uint64_t>::max())
    return {};

  clearActive();
  active_ = navigation;
  activeLifetime_ = &target;
  activeClaim_ = ++claimClock_;
  const std::uint64_t claim = activeClaim_;
  activeDestroyed_ = QObject::connect(
      &target, &QObject::destroyed, this,
      [this, claim] {
        if (activeClaim_ == claim)
          clearActive();
      },
      Qt::DirectConnection);
  return NavigationRouteClaim{*this, claim};
}

bool NavigationTargetRouter::applyNavigation(const NavigationAction &action) {
  if (QThread::currentThread() != thread() || !validNavigationAction(action))
    return false;
  if (active_ && activeLifetime_)
    return active_->applyNavigation(action);
  if (active_)
    clearActive();
  return fallback_ && fallbackLifetime_ ? fallback_->applyNavigation(action)
                                        : false;
}

bool NavigationTargetRouter::release(std::uint64_t claim) noexcept {
  if (QThread::currentThread() != thread() || claim == 0U ||
      activeClaim_ != claim)
    return false;
  clearActive();
  return true;
}

bool NavigationTargetRouter::owns(std::uint64_t claim) const noexcept {
  return QThread::currentThread() == thread() && claim != 0U &&
         activeClaim_ == claim && active_ && activeLifetime_;
}

void NavigationTargetRouter::clearActive() noexcept {
  if (activeDestroyed_)
    QObject::disconnect(activeDestroyed_);
  activeDestroyed_ = {};
  active_ = nullptr;
  activeLifetime_.clear();
  activeClaim_ = 0U;
}

NavigationRouteClaim::NavigationRouteClaim(NavigationTargetRouter &router,
                                           std::uint64_t claim)
    : router_(&router), claim_(claim) {}

NavigationRouteClaim::NavigationRouteClaim(
    NavigationRouteClaim &&other) noexcept
    : router_(std::exchange(other.router_, QPointer<NavigationTargetRouter>{})),
      claim_(std::exchange(other.claim_, 0U)) {}

NavigationRouteClaim &
NavigationRouteClaim::operator=(NavigationRouteClaim &&other) noexcept {
  if (this == &other)
    return *this;
  static_cast<void>(release());
  router_ = std::exchange(other.router_, QPointer<NavigationTargetRouter>{});
  claim_ = std::exchange(other.claim_, 0U);
  return *this;
}

NavigationRouteClaim::~NavigationRouteClaim() { static_cast<void>(release()); }

bool NavigationRouteClaim::active() const noexcept {
  return router_ && router_->owns(claim_);
}

bool NavigationRouteClaim::release() noexcept {
  NavigationTargetRouter *router = router_.data();
  router_.clear();
  const std::uint64_t claim = std::exchange(claim_, 0U);
  return router && router->release(claim);
}

} // namespace kearne::ui
