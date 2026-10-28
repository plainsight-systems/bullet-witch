// Mission-resident original effect packs and textures. Resolve original startup
// aliases in slot order, select requested IDs with later-write precedence, then
// decode/upload only their referenced texture records through the shared world
// texture path. This owns archives and resident handles; files/catalogs are
// borrowed during construction, renderer must outlive this owner. Returned
// pack spans remain valid until destruction. Retire scene and effect consumers
// before releasing these resources. No frame work, simulation or clock access.
// Missing requested content and malformed tables throw; partial uploads retire
// on failure. Serial loading-thread calls; O(pack records + requested
// references
// + selected image bytes), domain allocation only at preparation. Header design
// E.6 (#re-raii) owns rollback; MEM.9 (guidelines/memory/
// MEM.9-embedded-allocate-at-init.md) confines allocation to preparation.
// Source binding/reference comparison: first-combat-effects.md.
#pragma once
#include "game/combat/combat_effects.hpp"
#include "game/content/original/stage.hpp"
namespace bw::game {
class CombatEffectAssets {
 public:
  CombatEffectAssets(const platform::Files& files,
                     const resources::CatalogSet& catalogs,
                     render::WorldResources& renderer,
                     const std::shared_ptr<memory::Domain>& domain,
                     std::span<const std::uint32_t> requested_ids,
                     bool texture_mips,
                     const content::LoadProgress& progress = {});
  ~CombatEffectAssets();
  CombatEffectAssets(const CombatEffectAssets&) = delete;
  CombatEffectAssets& operator=(const CombatEffectAssets&) = delete;
  CombatEffectAssets(CombatEffectAssets&&) = delete;
  CombatEffectAssets& operator=(CombatEffectAssets&&) = delete;
  std::span<const CombatEffectPack> packs() const noexcept;

 private:
  struct State;
  std::shared_ptr<State> state_;
};
}  // namespace bw::game
