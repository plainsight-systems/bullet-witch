// Prepare original NPC appearance without mutating shared model residency.
// Inputs borrow immutable mission data only during construction. The returned
// owner retains draw selection and runtime texture-slot changes in the injected
// domain; spans remain valid until owner mutation/destruction. No clocks,
// simulation or device ownership. Draw order equals the resident definition.
// Setup tokenizes authored masks, applies ordered palette aliases, then emits
// only changed slots. O(palette selections * slots squared + draws), no work or
// allocation on access. Invalid tokens/groups fail explicitly. Both MCP phases
// I.1 (#ri-explicit) expose immutable inputs and lifetime; MEM.9
// (guidelines/memory/MEM.9-embedded-allocate-at-init.md) confines preparation
// allocations to admission. Source: npc-appearance.md.
#pragma once
#include "game/content/original/mission_placement.hpp"
#include "game/content/original/npc_palette.hpp"
#include "game/content/original/resident_model.hpp"
namespace bw::game {
struct NpcDrawAppearance {
  bool selected{};
};
class PreparedNpcAppearance {
 public:
  static constexpr std::size_t kGroupWordCount = 8;
  static PreparedNpcAppearance prepare(
      const content::PlacementArchetype& archetype,
      const ResidentModelView& model,
      std::span<const NpcPaletteBinding> palettes,
      std::span<const ResidentTextureSlot> slots,
      const std::shared_ptr<memory::Domain>& domain);
  // The weapon model constructor leaves all eight group-mask words enabled.
  // Weapon motion/visibility belongs to the actor, never to this preparation.
  static PreparedNpcAppearance default_weapon(
      const ResidentModelView& model,
      const std::shared_ptr<memory::Domain>& domain);
  std::span<const NpcDrawAppearance> draws() const { return draws_; }
  std::span<const std::uint32_t, kGroupWordCount> groups() const {
    return groups_;
  }
  std::span<const render::WorldTextureOverride> texture_overrides() const {
    return texture_overrides_;
  }

 private:
  explicit PreparedNpcAppearance(const std::shared_ptr<memory::Domain>& domain)
      : draws_(memory::Allocator<NpcDrawAppearance>(domain)),
        texture_overrides_(
            memory::Allocator<render::WorldTextureOverride>(domain)) {}
  memory::Vector<NpcDrawAppearance> draws_;
  std::array<std::uint32_t, kGroupWordCount> groups_{};
  memory::Vector<render::WorldTextureOverride> texture_overrides_;
};
}  // namespace bw::game
