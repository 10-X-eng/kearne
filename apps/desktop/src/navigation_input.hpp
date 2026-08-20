#pragma once

#include <QMetaObject>
#include <QObject>
#include <QPointer>

#include <cstdint>
#include <optional>
#include <string_view>
#include <variant>

namespace kearne::ui {

enum class NavigationProfile : std::uint8_t {
  Fusion = 1,
  SolidWorks = 2,
  Onshape = 3,
};

[[nodiscard]] std::optional<NavigationProfile>
navigationProfileFromId(std::string_view id) noexcept;
[[nodiscard]] std::string_view
navigationProfileId(NavigationProfile profile) noexcept;

enum class PointerButtonMask : std::uint8_t {
  None = 0,
  Left = 1U << 0U,
  Middle = 1U << 1U,
  Right = 1U << 2U,
};

[[nodiscard]] constexpr PointerButtonMask
operator|(PointerButtonMask first, PointerButtonMask second) noexcept {
  return static_cast<PointerButtonMask>(static_cast<std::uint8_t>(first) |
                                        static_cast<std::uint8_t>(second));
}

enum class PointerModifierMask : std::uint8_t {
  None = 0,
  Shift = 1U << 0U,
  Control = 1U << 1U,
  Alt = 1U << 2U,
  Meta = 1U << 3U,
};

[[nodiscard]] constexpr PointerModifierMask
operator|(PointerModifierMask first, PointerModifierMask second) noexcept {
  return static_cast<PointerModifierMask>(static_cast<std::uint8_t>(first) |
                                          static_cast<std::uint8_t>(second));
}

struct PointerDragInput {
  PointerButtonMask buttons = PointerButtonMask::None;
  PointerModifierMask modifiers = PointerModifierMask::None;
  double deltaXLogicalPixels = 0.0;
  double deltaYLogicalPixels = 0.0;
  bool operator==(const PointerDragInput &) const = default;
};

struct OrbitNavigation {
  double deltaXLogicalPixels = 0.0;
  double deltaYLogicalPixels = 0.0;
  bool operator==(const OrbitNavigation &) const = default;
};

struct PanNavigation {
  double deltaXLogicalPixels = 0.0;
  double deltaYLogicalPixels = 0.0;
  bool operator==(const PanNavigation &) const = default;
};

struct ZoomNavigation {
  double amount = 0.0;
  bool operator==(const ZoomNavigation &) const = default;
};

struct RollNavigation {
  double amount = 0.0;
  bool operator==(const RollNavigation &) const = default;
};

// Device-normalized 6-DoF motion for one bounded input drain. Translation and
// rotation components are dimensionless signed increments. Device adapters
// apply hardware range and event-period normalization before publication;
// targets apply camera-specific sensitivity exactly once.
struct SpaceMotionNavigation {
  double translationX = 0.0;
  double translationY = 0.0;
  double translationZ = 0.0;
  double rotationX = 0.0;
  double rotationY = 0.0;
  double rotationZ = 0.0;
  bool operator==(const SpaceMotionNavigation &) const = default;
};

struct FitNavigation {
  bool operator==(const FitNavigation &) const = default;
};

enum class StandardView : std::uint8_t {
  Front = 1,
  Back = 2,
  Left = 3,
  Right = 4,
  Top = 5,
  Bottom = 6,
  Isometric = 7,
};

struct StandardViewNavigation {
  StandardView view = StandardView::Isometric;
  bool operator==(const StandardViewNavigation &) const = default;
};

using NavigationAction =
    std::variant<OrbitNavigation, PanNavigation, ZoomNavigation, RollNavigation,
                 SpaceMotionNavigation, FitNavigation, StandardViewNavigation>;

[[nodiscard]] bool
validNavigationAction(const NavigationAction &action) noexcept;

// Pointer profile resolution is presentation policy only. Targets decide how
// logical deltas affect a 3D camera or an SI-valued sketch camera.
[[nodiscard]] std::optional<NavigationAction>
mapPointerDrag(NavigationProfile profile,
               const PointerDragInput &input) noexcept;

class NavigationTarget {
public:
  virtual ~NavigationTarget() = default;
  [[nodiscard]] virtual bool
  applyNavigation(const NavigationAction &action) = 0;
};

class NavigationRouteClaim;

// Routes synchronous UI-thread navigation to one active viewport. Claims are
// generation-scoped: releasing an older claim cannot disturb a newer target.
// QObject lifetimes prevent dispatch through destroyed targets without adding
// ownership or allocation to the motion hot path.
class NavigationTargetRouter final : public QObject, public NavigationTarget {
public:
  explicit NavigationTargetRouter(QObject &fallback, QObject *parent = nullptr);

  [[nodiscard]] NavigationRouteClaim claim(QObject &target);
  [[nodiscard]] bool applyNavigation(const NavigationAction &action) override;

private:
  [[nodiscard]] bool release(std::uint64_t claim) noexcept;
  [[nodiscard]] bool owns(std::uint64_t claim) const noexcept;
  void clearActive() noexcept;

  NavigationTarget *fallback_ = nullptr;
  QPointer<QObject> fallbackLifetime_;
  NavigationTarget *active_ = nullptr;
  QPointer<QObject> activeLifetime_;
  QMetaObject::Connection activeDestroyed_;
  std::uint64_t activeClaim_ = 0;
  std::uint64_t claimClock_ = 0;

  friend class NavigationRouteClaim;
};

class NavigationRouteClaim final {
public:
  NavigationRouteClaim() = default;
  NavigationRouteClaim(const NavigationRouteClaim &) = delete;
  NavigationRouteClaim &operator=(const NavigationRouteClaim &) = delete;
  NavigationRouteClaim(NavigationRouteClaim &&other) noexcept;
  NavigationRouteClaim &operator=(NavigationRouteClaim &&other) noexcept;
  ~NavigationRouteClaim();

  [[nodiscard]] bool active() const noexcept;
  [[nodiscard]] bool release() noexcept;

private:
  NavigationRouteClaim(NavigationTargetRouter &router, std::uint64_t claim);

  QPointer<NavigationTargetRouter> router_;
  std::uint64_t claim_ = 0;

  friend class NavigationTargetRouter;
};

} // namespace kearne::ui
