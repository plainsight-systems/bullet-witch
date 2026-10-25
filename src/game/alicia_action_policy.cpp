// Original Alicia action receivers, independent of resource and clock owners.
// Source formulas preserve family/nibble selection and submission order.
// Both implementation corpora: I.1/E.4 expose and validate inputs; MEM.9
// keeps all plans fixed-size. See the header and alicia-action-policy.md.
#include "alicia_action_policy.hpp"

#include <bit>
#include <cmath>
#include <stdexcept>

#include "bw/math/matrix.hpp"
#include "formats/reader.hpp"

namespace bw::game {
ActionMotionPlan ordinary_shooter_exit(ActionMotionPlan entry) noexcept {
  entry.clear_upper = true;
  return entry;
}
namespace {
constexpr std::uint32_t kSlotCount = 1108;
constexpr std::uint32_t kNibbleMask = 0xf;
constexpr std::uint32_t kFamilyStride = 0x10;
constexpr std::uint32_t kCrouchFamily = 0x90;
constexpr std::uint32_t kFirstUpperFamily = 0x130;
constexpr std::uint32_t kFirstReloadFamily = 0x150;
constexpr std::uint32_t kUpperFamilyEnd = 0x2c0;
constexpr std::uint32_t kFirstDischargeFamily = 0x1d0;
constexpr std::uint32_t kRaiseFamily = 0x250;
constexpr std::uint32_t kLowerFamily = 0x270;
constexpr std::uint32_t kAirRecoil = 0x2c1;
constexpr std::array<std::uint32_t, 6> kRecoilFamilies{0xd0,  0xe0,  0xf0,
                                                       0x100, 0x110, 0x120};
constexpr std::uint32_t kRaisedBack = 8;
constexpr std::uint32_t kRaisedForward = 9;
constexpr std::uint32_t kRaisedLeft = 10;
constexpr std::uint32_t kRaisedRight = 11;
constexpr std::uint32_t kRaisedIdle = 12;
constexpr std::uint32_t kRaisedTurnLeft = 13;
constexpr std::uint32_t kRaisedTurnRight = 14;
constexpr std::uint32_t kWeaponLower = 5;
constexpr std::uint32_t kWeaponDischarge = 6;
constexpr float kDashThreshold = .9F;
constexpr float kRateSanityLimit = 1000;
constexpr float kIdleDelaySeconds = 60;
constexpr std::uint32_t kStaySlot = 0x2ff;
constexpr double kCameraBiasRadians = .2617993950843811;
constexpr double kSideBoundaryRadians = .7853981852531433;

void validate_slot(std::uint32_t slot) {
  if (slot >= kSlotCount) {
    throw std::invalid_argument("Unknown native motion slot");
  }
}
void validate_stance(AliciaStance stance) {
  if (stance != AliciaStance::standing && stance != AliciaStance::crouched) {
    throw std::invalid_argument("Unknown Alicia stance");
  }
}
void validate_action(AliciaStance stance, std::uint32_t slot) {
  validate_stance(stance);
  validate_slot(slot);
}
void validate_weapon(std::uint32_t weapon_class) {
  if (weapon_class >= 4) throw std::invalid_argument("Unknown weapon class");
}
math::Vector3<float> horizontal_unit(const math::Vector3<float>& value) {
  if (!math::all_finite(value) || value[1] != 0) {
    throw std::invalid_argument("Expected finite horizontal direction");
  }
  const auto length = math::length(value);
  if (!(length > 0) || !std::isfinite(length)) {
    throw std::invalid_argument("Expected nonzero horizontal direction");
  }
  return math::divide(value, length);
}
ActionMotionRequest request(ActionMotionBank bank, std::uint32_t slot,
                            bool fresh = false) {
  ActionMotionRequest result;
  result.bank = bank;
  result.slot = slot;
  result.channel = bank == ActionMotionBank::upper_body ? 1 : 0;
  result.fresh = fresh;
  return result;
}
std::uint32_t stance_offset(AliciaStance stance) {
  return stance == AliciaStance::crouched ? kFamilyStride : 0;
}
}  // namespace

WeaponPresentationSettings WeaponPresentationSettings::parse(
    const formats::CustomizationDefinition& tree, unsigned weapon_class) {
  validate_weapon(weapon_class);
  constexpr std::array<std::string_view, 4> names{"wa0", "wa1", "wa2", "wa3"};
  const std::array path{std::string_view("witch"), std::string_view("weapon"),
                        names.at(weapon_class), std::string_view("00_spec")};
  const resources::detail::Reader reader{tree.lookup(path)};
  constexpr std::uint64_t kCadence = 0x90, kMovementScale = 0xb0;
  const WeaponPresentationSettings result{
      std::bit_cast<float>(reader.u32(kCadence)),
      std::bit_cast<float>(reader.u32(kMovementScale))};
  if (!std::isfinite(result.movement_scale) || result.movement_scale < 0) {
    throw std::invalid_argument("Invalid authored weapon movement scale");
  }
  if (!std::isfinite(result.rate_per_second) || result.rate_per_second <= 0 ||
      !std::isfinite(1.0F / result.rate_per_second)) {
    throw std::invalid_argument("Invalid authored weapon cadence");
  }
  return result;
}
GroundMotionSelection select_ground_motion(const GroundMotionInput& input) {
  validate_stance(input.stance);
  if (!std::isfinite(input.magnitude) || input.magnitude < 0 ||
      input.magnitude > 1 || !std::isfinite(input.signed_turn_rate) ||
      !std::isfinite(input.weapon_movement_scale) ||
      input.weapon_movement_scale < 0 ||
      !std::isfinite(input.upper_sampled_seconds) ||
      input.upper_sampled_seconds < 0) {
    throw std::invalid_argument("Invalid grounded motion input");
  }
  if (input.upper_slot) validate_slot(*input.upper_slot);
  const auto camera = horizontal_unit(input.camera_forward);
  auto magnitude = input.magnitude;
  if (input.weapon_raised) magnitude *= input.weapon_movement_scale;
  if (!std::isfinite(magnitude)) {
    throw std::invalid_argument("Ground motion scale overflow");
  }
  const bool crouched = input.stance == AliciaStance::crouched;
  const auto family = crouched                     ? kCrouchFamily
                      : magnitude > kDashThreshold ? kFamilyStride
                                                   : 0;
  auto rate = !crouched && magnitude <= kDashThreshold
                  ? magnitude / kDashThreshold
                  : magnitude;
  if (rate > kRateSanityLimit) rate = 1;
  const bool moving = rate > 0;
  std::uint32_t direction = moving ? 0 : 1;
  if (moving) {
    const auto movement = horizontal_unit(input.movement_world);
    if (input.weapon_raised) {
      // Source's biased camera basis selects shooting strafes. Shared math
      // owns the transform/dot/cross operations, not this action policy.
      const auto cosine = static_cast<float>(std::cos(kCameraBiasRadians));
      const auto sine = static_cast<float>(std::sin(kCameraBiasRadians));
      const math::Matrix4<float> rotation{{{cosine, 0, -sine, 0},
                                           {0, 1, 0, 0},
                                           {sine, 0, cosine, 0},
                                           {0, 0, 0, 1}}};
      const auto reference = math::transform_direction<float>(camera, rotation);
      const auto alignment = math::dot(movement, reference);
      const auto threshold = static_cast<float>(std::cos(kSideBoundaryRadians));
      if (alignment > threshold) {
        direction = kRaisedForward;
      } else if (alignment < -threshold) {
        direction = kRaisedBack;
      } else {
        direction = math::cross(reference, movement)[1] > 0 ? kRaisedRight
                                                            : kRaisedLeft;
      }
    }
  } else {
    rate = 1;
    if (input.weapon_raised) {
      direction = kRaisedIdle;
      if (input.signed_turn_rate != 0) {
        direction =
            input.signed_turn_rate < 0 ? kRaisedTurnLeft : kRaisedTurnRight;
        rate = std::abs(input.signed_turn_rate);
      }
    }
  }
  GroundMotionSelection result;
  result.locomotion_slot = family | direction;
  result.motion = request(ActionMotionBank::body, result.locomotion_slot);
  result.motion.rate = rate;
  result.camera_facing = input.weapon_raised;
  const bool idle = direction == 1 || direction == kRaisedIdle;
  result.motion.start =
      idle ? ActionMotionStart::seconds : ActionMotionStart::preserve_phase;
  // Idle body follows the upper action's authored full-body variant at that
  // owner's current phase; moving keeps independent locomotion underneath.
  if (idle && input.upper_slot) {
    result.motion.slot = (*input.upper_slot & ~kNibbleMask) | direction;
    result.motion.start_seconds = input.upper_sampled_seconds;
  }
  return result;
}

GroundIdleSelection GroundIdlePolicy::advance(
    const GroundMotionInput& input,
    std::optional<std::uint32_t> current_body_slot, bool current_body_completed,
    float delta_seconds) {
  if (!std::isfinite(delta_seconds) || delta_seconds < 0 ||
      !std::isfinite(idle_seconds_ + delta_seconds)) {
    throw std::invalid_argument("Invalid idle timer delta");
  }
  if (current_body_slot) validate_slot(*current_body_slot);
  GroundIdleSelection result{select_ground_motion(input)};
  if (input.stance != AliciaStance::standing) {
    reset();
    return result;
  }
  // The source tests the old retained slot even when this update clears or
  // arms it. Preserve that phase separately from this tick's chosen motion.
  result.refresh_direction = !stay_pending_;
  const auto slot = result.ground.locomotion_slot;
  const auto direction = slot & kNibbleMask;
  const bool idle =
      slot < kUpperFamilyEnd && (direction == 1 || direction == kRaisedIdle);
  if (idle && input.upper_slot) {
    reset();
  } else if (stay_pending_) {
    if (idle && (current_body_slot != kStaySlot || !current_body_completed)) {
      result.ground.motion = request(ActionMotionBank::body, kStaySlot);
      result.special_idle = true;
    } else {
      reset();
    }
  } else if (direction == 1) {
    // Crossing 60 seconds records the next selection, while the current
    // ordinary idle remains submitted for this update (source6667e0).
    idle_seconds_ += delta_seconds;
    if (idle_seconds_ > kIdleDelaySeconds) {
      stay_pending_ = true;
      idle_seconds_ = 0;
    }
  } else {
    idle_seconds_ = 0;
  }
  return result;
}

void GroundIdlePolicy::reset() noexcept {
  idle_seconds_ = 0;
  stay_pending_ = false;
}

ActionMotionPlan change_weapon_raised(AliciaStance stance,
                                      std::uint32_t body_slot, bool was_raised,
                                      bool raised) {
  validate_action(stance, body_slot);
  ActionMotionPlan plan;
  if (was_raised == raised) return plan;
  const auto family =
      (raised ? kRaiseFamily : kLowerFamily) + stance_offset(stance);
  plan.requests[0] =
      request(ActionMotionBank::upper_body, family | (body_slot & kNibbleMask));
  plan.requests[1] =
      request(ActionMotionBank::weapon, raised ? 2 : kWeaponLower, true);
  plan.count = 2;
  plan.clear_upper = raised;
  plan.weapon_rest_slot = raised ? 2 : 1;
  return plan;
}

ActionMotionPlan accepted_reload(const AcceptedWeaponAction& action) {
  validate_action(action.stance, action.body_slot);
  validate_weapon(action.weapon_class);
  ActionMotionPlan plan;
  const bool raised_variant =
      action.body_slot < kUpperFamilyEnd && (action.body_slot & 8) != 0;
  plan.requests[0] =
      request(ActionMotionBank::weapon, raised_variant ? 4 : 3, true);
  plan.requests[1] =
      request(ActionMotionBank::upper_body,
              (kFirstReloadFamily + action.weapon_class * 2 * kFamilyStride +
               stance_offset(action.stance)) |
                  (action.body_slot & kNibbleMask));
  plan.count = 2;
  return plan;
}

ActionMotionPlan accepted_shot(const AcceptedWeaponAction& action) {
  validate_action(action.stance, action.body_slot);
  validate_weapon(action.weapon_class);
  ActionMotionPlan plan;
  const auto direction = action.body_slot & kNibbleMask;
  plan.requests[0] =
      request(ActionMotionBank::upper_body,
              (kFirstDischargeFamily + action.weapon_class * 2 * kFamilyStride +
               stance_offset(action.stance)) |
                  direction,
              true);
  plan.requests[1] = request(ActionMotionBank::weapon, kWeaponDischarge, true);
  auto recoil = kAirRecoil;
  if (action.grounded) {
    const auto body_family = action.body_slot & ~kNibbleMask;
    const std::size_t movement = body_family == 0               ? 0
                                 : body_family == kFamilyStride ? 1
                                                                : 2;
    const std::size_t class_offset = action.weapon_class == 2 ? 3 : 0;
    recoil = kRecoilFamilies.at(movement + class_offset) | direction;
  }
  plan.requests[2] = request(ActionMotionBank::additive, recoil, true);
  plan.count = 3;
  return plan;
}

ActionMotionPlan accepted_weapon_change(AliciaStance stance,
                                        std::uint32_t body_slot) {
  validate_action(stance, body_slot);
  ActionMotionPlan plan;
  plan.requests[0] = request(
      ActionMotionBank::upper_body,
      (kFirstUpperFamily + stance_offset(stance)) | (body_slot & kNibbleMask),
      true);
  plan.count = 1;
  return plan;
}

bool permits_support_hand(std::optional<std::uint32_t> upper_slot) noexcept {
  if (!upper_slot) return true;
  // The eight standing/crouched discharge families own recoil while retaining
  // a grip. Other upper owners need their authored free-hand motion.
  constexpr std::uint32_t kDischargeFamilies = 8;
  return *upper_slot >= kFirstDischargeFamily &&
         *upper_slot <
             kFirstDischargeFamily + kDischargeFamilies * kFamilyStride;
}

std::optional<ActionMotionRequest> adapt_upper_stance(
    AliciaStance stance, std::uint32_t upper_slot) {
  validate_action(stance, upper_slot);
  if (upper_slot < kFirstUpperFamily || upper_slot >= kUpperFamilyEnd) {
    return {};
  }
  const bool crouched = ((upper_slot / kFamilyStride) & 1U) == 0;
  if (crouched == (stance == AliciaStance::crouched)) return {};
  auto result = request(
      ActionMotionBank::upper_body,
      crouched ? upper_slot - kFamilyStride : upper_slot + kFamilyStride, true);
  result.start = ActionMotionStart::preserve_phase;
  return result;
}

std::optional<ActionMotionRequest> completed_weapon_motion(
    bool repeating, bool completed, std::uint32_t rest_slot) {
  if (rest_slot != 1 && rest_slot != 2) {
    throw std::invalid_argument("Unknown desired weapon rest stance");
  }
  if (repeating || !completed) return {};
  return request(ActionMotionBank::weapon, rest_slot, true);
}
}  // namespace bw::game
