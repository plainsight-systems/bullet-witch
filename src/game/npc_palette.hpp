// Construct the original NPC material-variation registry after texture preload.
// Definitions and device handles borrow the mission texture owner; returned
// bindings own only small metadata in the supplied domain. Compatibility names
// refer to static program rules; no asset bytes are embedded. Native family
// variation indexes remain stable even when a texture lookup is absent. Load
// completes after real available textures are linked; actor material switching
// is a later consumer. O(rules * loaded textures), bounded original rule count.
// Design/implementation SL.con.3 (#rsl-bounds) validates aligned texture
// handles; E.6 (#re-raii) publishes atomically; MEM.6 (guidelines/memory/
// MEM.6-inject-allocators-behind-one-interface.md) injects metadata allocation.
// Source reconstruction is recorded in mission-load-consumers.md.
#pragma once
#include "bw/content/stage.hpp"
#include "bw/render/world.hpp"
namespace bw::game {
struct NpcPaletteBinding {
  std::string_view family, material;
  std::size_t variation{};
  render::WorldTextureHandle texture;
};
memory::Vector<NpcPaletteBinding> make_npc_palette(
    const content::StageDefinition& definition,
    std::span<const render::WorldTextureHandle> textures,
    const std::shared_ptr<memory::Domain>& domain);
}  // namespace bw::game
