// Alicia/weapon receiver representation policy, prepared once against immutable
// catalog identity. The caller supplies one unknown-initialized slot per clip;
// this function grants only source-witnessed ordinary/additive semantics and
// rejects mismatched output size. Borrowed catalog/output live through the
// call. No allocation; O(aliases + explicit weapon names), never per-frame
// work. Core I.1 (#ri-explicit) makes receiver policy an explicit constructor
// input; performance CDSA.32 (transform-static-data-into-the-consumer-layout)
// stores one byte per prepared clip. Both corpora read in
// design/implementation. Existing source mapping: animation-review-geometry.md
// and alicia-animation-coverage.md.
#include "pose_semantics.hpp"

#include <algorithm>
#include <stdexcept>

#include "alicia_motion_bindings.hpp"
namespace bw::game {
namespace {
// These bounded receiver ranges are actor policy, not format heuristics. A
// publication-table entry alone does not prove its consumer representation.
MotionPoseSemantics alicia_slot_semantics(std::size_t slot) {
  struct Range {
    std::size_t first, last;
  };
  constexpr std::array ordinary_ranges{
      Range{0x130, 0x28f},  // Shooter switch/reload/discharge/raise/lower.
      Range{656, 704},      // Runner scripted start/flight/landing.
      Range{706, 721},      // Close attack, Dodger, fall.
      Range{730, 740},      // Weighted and crouched death.
      Range{749, 760}};     // Directional ordinary damage.
  constexpr std::array<std::size_t, 12> ordinary_slots{
      767, 770, 771, 779, 782, 783, 784, 785, 786, 787, 788, 789};
  constexpr std::array<std::size_t, 3> grounded_families{0, 16, 144};
  constexpr std::size_t kDirectionMask = 15, kFirstRaisedDirection = 8,
                        kLastRaisedDirection = 14;
  const auto direction = slot & kDirectionMask;
  const auto family = slot & ~kDirectionMask;
  const bool grounded =
      std::ranges::find(grounded_families, family) != grounded_families.end() &&
      (direction <= 1 || (direction >= kFirstRaisedDirection &&
                          direction <= kLastRaisedDirection));
  if (grounded ||
      std::ranges::any_of(ordinary_ranges,
                          [slot](const Range& range) {
                            return slot >= range.first && slot <= range.last;
                          }) ||
      std::ranges::find(ordinary_slots, slot) != ordinary_slots.end()) {
    return MotionPoseSemantics::ordinary;
  }
  constexpr std::size_t kFirstRecoilSlot = 0xd0;
  constexpr std::size_t kRecoilSlotsEnd = 0x130;
  constexpr std::size_t kAirRecoilSlot = 0x2c1;
  constexpr std::size_t kDamageDeltaSlot = 0x394;
  if ((slot >= kFirstRecoilSlot && slot < kRecoilSlotsEnd) ||
      slot == kAirRecoilSlot || slot == kDamageDeltaSlot) {
    return MotionPoseSemantics::additive;
  }
  return MotionPoseSemantics::unknown;
}
}  // namespace
void alicia_pose_semantics(const animation::PreparedClipCatalog& catalog,
                           std::string_view owner,
                           std::span<MotionPoseSemantics> result) {
  if (result.size() != catalog.size()) {
    throw std::invalid_argument("Pose semantics size differs from catalog");
  }
  constexpr std::array weapon_owners{"wa0", "wa1", "wa2", "wa3"};
  if (std::ranges::find(weapon_owners, owner) != weapon_owners.end()) {
    // The original weapon constructor registers these exact ordinary inputs;
    // unrelated resources in the same package do not inherit that contract.
    constexpr std::array<std::string_view, 7> names{
        "defaultmotion", "stand_l",   "shoot_l",    "stand_reload",
        "shoot_reload",  "shoot_end", "shoot_after"};
    for (const auto name : names) {
      if (const auto clip = catalog.find(name)) {
        result[*clip] = MotionPoseSemantics::ordinary;
      }
    }
    return;
  }
  if (owner != "pw0") return;
  for (const auto& binding : alicia_motion_aliases()) {
    const auto semantics = alicia_slot_semantics(binding.slot);
    if (semantics == MotionPoseSemantics::unknown ||
        binding.slot >= catalog.slot_count()) {
      continue;
    }
    const auto clip = catalog.alias(binding.slot).clip;
    if (!clip || catalog.name(*clip) != binding.name) continue;
    auto& destination = result[*clip];
    if (destination != MotionPoseSemantics::unknown &&
        destination != semantics) {
      throw std::invalid_argument("Conflicting source pose representations");
    }
    destination = semantics;
  }
  // This exact unaliased asset has independently witnessed delta data. Its
  // source action caller remains unresolved; this compatibility entry admits
  // representation, not a fabricated action or a filename suffix heuristic.
  if (const auto clip = catalog.find("shoot_stand_fire_add")) {
    result[*clip] = MotionPoseSemantics::additive;
  }
  return;
}
}  // namespace bw::game
