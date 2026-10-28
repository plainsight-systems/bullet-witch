// Preserve the original message order and camera-ownership branch. Ground
// contact cannot select the actor's recoil family. I.11/CDSA.32 retain copied
// policy inputs and shared immutable alias identities.
#include "weapon_motion_policy.hpp"

#include <stdexcept>

namespace bw::game {
ActionMotionPlan accepted_weapon_shot_motion(AcceptedWeaponShotMotion action) {
  constexpr std::uint32_t kSlotCount = 1108, kWeaponClasses = 4;
  if (action.body_slot >= kSlotCount || action.weapon_class >= kWeaponClasses) {
    throw std::invalid_argument("Unknown source weapon motion identity");
  }
  constexpr std::uint32_t kDirectionMask = 0xf, kFamilyStride = 0x10;
  constexpr std::uint32_t kFirstDischarge = 0x1d0, kWeaponDischarge = 6;
  constexpr std::uint32_t kNpcRecoil = 0x2c1;
  constexpr std::array<std::uint32_t, 6> kRecoilFamilies{0xd0,  0xe0,  0xf0,
                                                         0x100, 0x110, 0x120};
  const auto direction = action.body_slot & kDirectionMask;
  auto recoil = kNpcRecoil;
  if (action.player_camera) {
    const auto family = action.body_slot & ~kDirectionMask;
    const std::size_t movement = family == 0               ? 0
                                 : family == kFamilyStride ? 1
                                                           : 2;
    constexpr std::uint32_t kShotgunClass = 2;
    const std::size_t offset = action.weapon_class == kShotgunClass ? 3 : 0;
    recoil = kRecoilFamilies.at(movement + offset) | direction;
  }
  ActionMotionPlan plan;
  plan.requests[0] = {
      .bank = ActionMotionBank::upper_body,
      .slot = (kFirstDischarge + action.weapon_class * 2 * kFamilyStride +
               (action.crouched ? kFamilyStride : 0)) |
              direction,
      .channel = 1,
      .fresh = true};
  plan.requests[1] = {.bank = ActionMotionBank::weapon,
                      .slot = kWeaponDischarge,
                      .fresh = true};
  plan.requests[2] = {
      .bank = ActionMotionBank::additive, .slot = recoil, .fresh = true};
  plan.count = plan.requests.size();
  return plan;
}
ActionMotionPlan accepted_weapon_reload_motion(
    AcceptedWeaponReloadMotion action) {
  constexpr std::uint32_t kSlotCount = 1108, kWeaponClasses = 4;
  constexpr std::uint32_t kNibble = 0xf, kStride = 0x10;
  constexpr std::uint32_t kFirstReload = 0x150, kUpperEnd = 0x2c0;
  constexpr std::uint32_t kRaisedBit = 8;
  if (action.body_slot >= kSlotCount || action.weapon_class >= kWeaponClasses) {
    throw std::invalid_argument("Unknown reload motion identity");
  }
  const bool raised =
      action.body_slot < kUpperEnd && (action.body_slot & kRaisedBit) != 0;
  ActionMotionPlan plan;
  plan.requests[0] = {.bank = ActionMotionBank::weapon,
                      .slot = raised ? 4U : 3U,
                      .fresh = true};
  plan.requests[1] = {
      .bank = ActionMotionBank::upper_body,
      .slot = (kFirstReload + action.weapon_class * 2 * kStride +
               (action.crouched ? kStride : 0)) |
              (action.body_slot & kNibble),
      .channel = 1};
  plan.count = 2;
  return plan;
}
ActionMotionPlan accepted_weapon_ready_motion(
    AcceptedWeaponReadyMotion action) {
  constexpr std::uint32_t kSlotCount = 1108, kNibble = 0xf, kStride = 0x10;
  constexpr std::uint32_t kRaise = 0x250, kLower = 0x270, kWeaponLower = 5;
  if (action.body_slot >= kSlotCount) {
    throw std::invalid_argument("Unknown ready motion identity");
  }
  const auto family =
      (action.raised ? kRaise : kLower) + (action.crouched ? kStride : 0);
  ActionMotionPlan plan;
  plan.requests[0] = {.bank = ActionMotionBank::upper_body,
                      .slot = family | (action.body_slot & kNibble),
                      .channel = 1};
  plan.requests[1] = {.bank = ActionMotionBank::weapon,
                      .slot = action.raised ? 2U : kWeaponLower,
                      .fresh = true};
  plan.count = 2;
  plan.clear_upper = action.raised;
  plan.weapon_rest_slot = action.raised ? 2U : 1U;
  return plan;
}
}  // namespace bw::game
