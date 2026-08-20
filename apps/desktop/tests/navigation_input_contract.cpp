#include "navigation_input.hpp"

#include <kearne/testkit/property.hpp>

#include <QObject>

#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>

namespace {

using kearne::ui::NavigationAction;
using kearne::ui::NavigationProfile;
using kearne::ui::OrbitNavigation;
using kearne::ui::PanNavigation;
using kearne::ui::PointerButtonMask;
using kearne::ui::PointerDragInput;
using kearne::ui::PointerModifierMask;
using kearne::ui::SpaceMotionNavigation;

static_assert(std::is_trivially_copyable_v<SpaceMotionNavigation>);
static_assert(sizeof(SpaceMotionNavigation) == 6U * sizeof(double));

enum class ExpectedAction : std::uint8_t { None, Orbit, Pan };

class RecordingTarget final : public QObject,
                              public kearne::ui::NavigationTarget {
public:
  [[nodiscard]] bool
  applyNavigation(const kearne::ui::NavigationAction &action) override {
    if (!kearne::ui::validNavigationAction(action))
      return false;
    last = action;
    ++calls;
    return accepted;
  }

  kearne::ui::NavigationAction last = kearne::ui::FitNavigation{};
  std::uint64_t calls = 0;
  bool accepted = true;
};

constexpr std::array profiles{NavigationProfile::Fusion,
                              NavigationProfile::SolidWorks,
                              NavigationProfile::Onshape};
constexpr std::array profileIds{std::string_view{"fusion"},
                                std::string_view{"solidworks"},
                                std::string_view{"onshape"}};

void require(bool condition, const char *message) {
  if (!condition)
    throw std::runtime_error(message);
}

bool contains(std::uint8_t value, std::uint8_t flag) {
  return (value & flag) != 0U;
}

ExpectedAction referenceMapping(NavigationProfile profile, std::uint8_t buttons,
                                std::uint8_t modifiers) {
  constexpr auto middle = static_cast<std::uint8_t>(PointerButtonMask::Middle);
  constexpr auto right = static_cast<std::uint8_t>(PointerButtonMask::Right);
  constexpr auto shift = static_cast<std::uint8_t>(PointerModifierMask::Shift);
  constexpr auto control =
      static_cast<std::uint8_t>(PointerModifierMask::Control);
  switch (profile) {
  case NavigationProfile::Fusion:
    if (contains(buttons, middle))
      return contains(modifiers, shift) ? ExpectedAction::Orbit
                                        : ExpectedAction::Pan;
    break;
  case NavigationProfile::SolidWorks:
    if (contains(buttons, middle))
      return contains(modifiers, control) ? ExpectedAction::Pan
                                          : ExpectedAction::Orbit;
    break;
  case NavigationProfile::Onshape:
    if ((contains(buttons, right) && contains(modifiers, control)) ||
        contains(buttons, middle))
      return ExpectedAction::Pan;
    if (contains(buttons, right))
      return ExpectedAction::Orbit;
    break;
  }
  return ExpectedAction::None;
}

void requireMapping(const std::optional<NavigationAction> &actual,
                    ExpectedAction expected, double deltaX, double deltaY) {
  if (expected == ExpectedAction::None) {
    require(!actual, "unmapped drag produced an action");
    return;
  }
  require(actual.has_value(), "mapped drag produced no action");
  if (expected == ExpectedAction::Orbit) {
    const auto *orbit = std::get_if<OrbitNavigation>(&*actual);
    require(orbit && orbit->deltaXLogicalPixels == deltaX &&
                orbit->deltaYLogicalPixels == deltaY,
            "orbit mapping changed its action or deltas");
    return;
  }
  const auto *pan = std::get_if<PanNavigation>(&*actual);
  require(pan && pan->deltaXLogicalPixels == deltaX &&
              pan->deltaYLogicalPixels == deltaY,
          "pan mapping changed its action or deltas");
}

void verifyProfileIds() {
  for (std::size_t index = 0; index < profiles.size(); ++index) {
    require(kearne::ui::navigationProfileId(profiles[index]) ==
                profileIds[index],
            "profile ID changed");
    require(kearne::ui::navigationProfileFromId(profileIds[index]) ==
                profiles[index],
            "profile ID did not round-trip");
  }
  require(!kearne::ui::navigationProfileFromId({}),
          "empty profile ID was accepted");
  require(!kearne::ui::navigationProfileFromId("Fusion"),
          "case-variant profile ID was accepted");
  require(!kearne::ui::navigationProfileFromId("unknown"),
          "unknown profile ID was accepted");
  require(kearne::ui::navigationProfileId(static_cast<NavigationProfile>(0xffU))
              .empty(),
          "invalid profile produced an ID");
}

void verifyExhaustiveMappings() {
  constexpr double deltaX = 17.25;
  constexpr double deltaY = -9.5;
  for (const auto profile : profiles) {
    for (std::uint8_t buttons = 0; buttons < 8U; ++buttons) {
      for (std::uint8_t modifiers = 0; modifiers < 16U; ++modifiers) {
        const auto actual = kearne::ui::mapPointerDrag(
            profile,
            PointerDragInput{static_cast<PointerButtonMask>(buttons),
                             static_cast<PointerModifierMask>(modifiers),
                             deltaX, deltaY});
        requireMapping(actual, referenceMapping(profile, buttons, modifiers),
                       deltaX, deltaY);
      }
    }
  }
}

void verifyInvalidInputs() {
  const auto valid = PointerDragInput{PointerButtonMask::Middle,
                                      PointerModifierMask::None, 1.0, -1.0};
  require(
      !kearne::ui::mapPointerDrag(static_cast<NavigationProfile>(0xffU), valid),
      "invalid profile was mapped");
  require(!kearne::ui::mapPointerDrag(
              NavigationProfile::Fusion,
              PointerDragInput{static_cast<PointerButtonMask>(0x80U),
                               PointerModifierMask::None, 1.0, -1.0}),
          "unknown pointer-button bit was accepted");
  require(
      !kearne::ui::mapPointerDrag(
          NavigationProfile::Fusion,
          PointerDragInput{PointerButtonMask::Middle,
                           static_cast<PointerModifierMask>(0x80U), 1.0, -1.0}),
      "unknown modifier bit was accepted");

  const double infinity = std::numeric_limits<double>::infinity();
  const double nan = std::numeric_limits<double>::quiet_NaN();
  for (const auto value : {infinity, -infinity, nan}) {
    require(!kearne::ui::mapPointerDrag(
                NavigationProfile::Fusion,
                PointerDragInput{PointerButtonMask::Middle,
                                 PointerModifierMask::None, value, 0.0}),
            "non-finite horizontal delta was accepted");
    require(!kearne::ui::mapPointerDrag(
                NavigationProfile::Fusion,
                PointerDragInput{PointerButtonMask::Middle,
                                 PointerModifierMask::None, 0.0, value}),
            "non-finite vertical delta was accepted");
  }
}

void verifyActionValidation() {
  using namespace kearne::ui;
  require(validNavigationAction(OrbitNavigation{1.0, -1.0}),
          "finite orbit action was rejected");
  require(validNavigationAction(PanNavigation{1.0, -1.0}),
          "finite pan action was rejected");
  require(validNavigationAction(ZoomNavigation{1.0}),
          "finite zoom action was rejected");
  require(validNavigationAction(RollNavigation{1.0}),
          "finite roll action was rejected");
  require(validNavigationAction(
              SpaceMotionNavigation{1.0, -2.0, 3.0, -4.0, 5.0, -6.0}),
          "finite SpaceMouse action was rejected");
  require(validNavigationAction(FitNavigation{}), "fit action was rejected");
  for (std::uint8_t view = static_cast<std::uint8_t>(StandardView::Front);
       view <= static_cast<std::uint8_t>(StandardView::Isometric); ++view) {
    require(validNavigationAction(
                StandardViewNavigation{static_cast<StandardView>(view)}),
            "declared standard view was rejected");
  }

  const double nan = std::numeric_limits<double>::quiet_NaN();
  require(!validNavigationAction(OrbitNavigation{nan, 0.0}),
          "non-finite orbit action was accepted");
  require(!validNavigationAction(PanNavigation{0.0, nan}),
          "non-finite pan action was accepted");
  require(!validNavigationAction(ZoomNavigation{nan}),
          "non-finite zoom action was accepted");
  require(!validNavigationAction(RollNavigation{nan}),
          "non-finite roll action was accepted");
  for (std::size_t axis = 0; axis < 6U; ++axis) {
    std::array<double, 6> values{};
    values[axis] = nan;
    require(!validNavigationAction(SpaceMotionNavigation{values[0], values[1],
                                                         values[2], values[3],
                                                         values[4], values[5]}),
            "non-finite SpaceMouse axis was accepted");
  }
  require(!validNavigationAction(
              StandardViewNavigation{static_cast<StandardView>(0xffU)}),
          "invalid standard view was accepted");
}

void verifyRouteLifecycle() {
  using namespace kearne::ui;
  RecordingTarget fallback;
  RecordingTarget first;
  RecordingTarget second;
  NavigationTargetRouter router(fallback);
  const NavigationAction pan = PanNavigation{3.0, -2.0};

  require(router.applyNavigation(pan) && fallback.calls == 1U,
          "router did not use its fallback without a claim");
  auto firstClaim = router.claim(first);
  require(firstClaim.active() && router.applyNavigation(pan) &&
              first.calls == 1U && fallback.calls == 1U,
          "active route did not isolate the fallback");
  first.accepted = false;
  require(!router.applyNavigation(pan) && first.calls == 2U &&
              fallback.calls == 1U,
          "rejected active navigation leaked to the fallback");
  first.accepted = true;

  auto secondClaim = router.claim(second);
  require(!firstClaim.active() && secondClaim.active() &&
              !firstClaim.release() && router.applyNavigation(pan) &&
              second.calls == 1U,
          "stale route release disturbed the current target");
  NavigationRouteClaim moved = std::move(secondClaim);
  require(!secondClaim.active() && moved.active() && moved.release() &&
              router.applyNavigation(pan) && fallback.calls == 2U,
          "route claim move or release lost fallback routing");

  NavigationRouteClaim destroyedClaim;
  {
    auto transient = std::make_unique<RecordingTarget>();
    destroyedClaim = router.claim(*transient);
    require(destroyedClaim.active(), "transient target claim failed");
    transient.reset();
  }
  require(!destroyedClaim.active() && !destroyedClaim.release() &&
              router.applyNavigation(pan) && fallback.calls == 3U,
          "destroyed active target did not restore the fallback");

  auto doomedFallback = std::make_unique<RecordingTarget>();
  NavigationTargetRouter routerWithoutFallback(*doomedFallback);
  doomedFallback.reset();
  require(!routerWithoutFallback.applyNavigation(pan),
          "destroyed fallback received navigation");

  QObject notATarget;
  auto validBeforeRejection = router.claim(first);
  auto rejected = router.claim(notATarget);
  NavigationTargetRouter nested(fallback);
  auto rejectedRouter = router.claim(nested);
  const std::uint64_t firstBeforeRejectedClaim = first.calls;
  require(validBeforeRejection.active() && !rejected.active() &&
              !rejectedRouter.active() && router.applyNavigation(pan) &&
              first.calls == firstBeforeRejectedClaim + 1U &&
              fallback.calls == 3U,
          "invalid claim replaced a valid navigation route");
  static_cast<void>(validBeforeRejection.release());
  NavigationTargetRouter invalidFallback(notATarget);
  require(!invalidFallback.applyNavigation(pan),
          "non-navigation QObject became a fallback route");
  NavigationTargetRouter routerFallback(static_cast<QObject &>(router));
  require(!routerFallback.applyNavigation(pan),
          "router-to-router fallback admitted a navigation cycle");
  const std::uint64_t callsBeforeInvalid = first.calls;
  const double nan = std::numeric_limits<double>::quiet_NaN();
  auto validForInvalidAction = router.claim(first);
  require(validForInvalidAction.active() &&
              !router.applyNavigation(ZoomNavigation{nan}) &&
              first.calls == callsBeforeInvalid,
          "invalid navigation reached an active target");

  NavigationRouteClaim orphanedClaim;
  {
    auto ephemeralRouter = std::make_unique<NavigationTargetRouter>(fallback);
    orphanedClaim = ephemeralRouter->claim(first);
    require(orphanedClaim.active(), "ephemeral router claim failed");
  }
  require(!orphanedClaim.active() && !orphanedClaim.release(),
          "claim retained a destroyed router");
}

void verifyGeneratedRoutes() {
  const auto property = kearne::testkit::propertyProfile();
  kearne::testkit::checkProperty(
      "navigation target routes", property,
      [](kearne::testkit::Random &random, std::uint64_t) {
        using namespace kearne::ui;
        RecordingTarget fallback;
        RecordingTarget first;
        RecordingTarget second;
        NavigationTargetRouter router(fallback);
        NavigationRouteClaim firstClaim;
        NavigationRouteClaim secondClaim;
        RecordingTarget *expected = &fallback;

        for (std::size_t step = 0; step < 128U; ++step) {
          switch (random.next() % 5U) {
          case 0U:
            firstClaim = router.claim(first);
            expected = &first;
            break;
          case 1U:
            secondClaim = router.claim(second);
            expected = &second;
            break;
          case 2U: {
            const bool active = firstClaim.active();
            require(firstClaim.release() == active,
                    "first generated claim release disagreed with ownership");
            if (active)
              expected = &fallback;
            break;
          }
          case 3U: {
            const bool active = secondClaim.active();
            require(secondClaim.release() == active,
                    "second generated claim release disagreed with ownership");
            if (active)
              expected = &fallback;
            break;
          }
          default: {
            const SpaceMotionNavigation motion{
                random.between(-4.0, 4.0), random.between(-4.0, 4.0),
                random.between(-4.0, 4.0), random.between(-4.0, 4.0),
                random.between(-4.0, 4.0), random.between(-4.0, 4.0)};
            const std::uint64_t before = expected->calls;
            require(router.applyNavigation(motion) &&
                        expected->calls == before + 1U &&
                        std::get<SpaceMotionNavigation>(expected->last) ==
                            motion,
                    "generated motion reached the wrong navigation target");
            break;
          }
          }
        }
      });
}

void verifyGeneratedMappings() {
  const auto property = kearne::testkit::propertyProfile();
  kearne::testkit::checkProperty(
      "navigation pointer mapping", property,
      [](kearne::testkit::Random &random, std::uint64_t) {
        const auto profile = profiles[random.next() % profiles.size()];
        const auto rawButtons = static_cast<std::uint8_t>(random.next());
        const auto rawModifiers = static_cast<std::uint8_t>(random.next());
        const double deltaX = random.between(-1'000'000.0, 1'000'000.0);
        const double deltaY = random.between(-1'000'000.0, 1'000'000.0);
        const auto actual = kearne::ui::mapPointerDrag(
            profile,
            PointerDragInput{static_cast<PointerButtonMask>(rawButtons),
                             static_cast<PointerModifierMask>(rawModifiers),
                             deltaX, deltaY});
        if ((rawButtons & 0xf8U) != 0U || (rawModifiers & 0xf0U) != 0U) {
          require(!actual, "unknown generated input bit was accepted");
          return;
        }
        requireMapping(actual,
                       referenceMapping(profile, rawButtons, rawModifiers),
                       deltaX, deltaY);
      });
}

} // namespace

int main() {
  try {
    verifyProfileIds();
    verifyExhaustiveMappings();
    verifyInvalidInputs();
    verifyActionValidation();
    verifyGeneratedMappings();
    verifyRouteLifecycle();
    verifyGeneratedRoutes();
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
