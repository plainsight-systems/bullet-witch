// Registration preserves the accepted source category/name filtering and
// original placed body transforms. Ownership moves to the world; no collision
// algorithm changes. Implementation Core E.6/I.11 and MEM.9/MEM.6 retain
// exception cleanup and setup-only domain storage from the header contract.
#include "world_collision.hpp"

#include <algorithm>

#include "placed_collision.hpp"
namespace bw::game {
namespace {
// Collision ownership follows the original static-object construction path.
// A combined file replaces the separate character/environment registrations.
const formats::CollisionDefinition* collision_input(
    const content::StageDefinition& stage, std::string_view model,
    std::string_view suffix) {
  const auto found = std::ranges::find_if(stage.inputs, [&](const auto& input) {
    std::string_view name = input.name;
    constexpr std::string_view kPrefix = "model.";
    if (!name.starts_with(kPrefix)) return false;
    name.remove_prefix(kPrefix.size());
    return name.starts_with(model) && name.substr(model.size()) == suffix;
  });
  return found != stage.inputs.end() && found->collision ? &*found->collision
                                                         : nullptr;
}
void register_stage_collision(const content::StageDefinition& stage,
                              scene::StaticCollision& collision) {
  for (const auto& model : stage.models) {
    if (!model.static_world_object) continue;
    if (const auto* combined = collision_input(stage, model.name, ".hit")) {
      collision.add(*combined, scene::CollisionSourceMode::combined);
      continue;
    }
    if (const auto* character = collision_input(stage, model.name, ".chit")) {
      collision.add(*character, scene::CollisionSourceMode::character);
    }
    if (const auto* environment = collision_input(stage, model.name, ".ehit")) {
      collision.add(*environment, scene::CollisionSourceMode::environment);
    }
  }
}
}  // namespace
scene::StaticCollision prepare_world_collision(
    const content::StageDefinition& stage, StageBackgroundSource background,
    const std::shared_ptr<memory::Domain>& domain) {
  scene::StaticCollision collision(domain);
  register_stage_collision(stage, collision);
  // Prepare each placed model's authored shapes once, then register its frozen
  // body poses. Render geometry is never substituted for collision geometry.
  if (background.placements != nullptr) {
    const auto& placements = *background.placements;
    for (const auto& model : background.models) {
      const auto name = std::ranges::find(placements.names(), model.name);
      if (name == placements.names().end() ||
          model.physics_collision == nullptr) {
        continue;
      }
      const auto model_index =
          static_cast<std::size_t>(name - placements.names().begin());
      PreparedPropCollision prepared(*model.physics_collision, domain);
      for (const auto& placement : placements.placements()) {
        if (placement.model != model_index) continue;
        const auto body_world =
            std::span(placements.matrices())
                .subspan(placement.first_matrix, placement.matrix_count);
        prepared.register_instance(body_world, collision);
      }
    }
  }
  collision.prepare_character_queries();
  return collision;
}
}  // namespace bw::game
