// Prepare original effect identities once, then publish copied simulation
// observations through the scene's effects-only capability. Mission assets
// retain GPU textures; this adapter cannot register/release device resources.
// Implementation I.11 (#ri-raw) and MEM.9 (guidelines/memory/
// MEM.9-embedded-allocate-at-init.md) were reread: all retained programs and
// packets use the mission domain; frame inputs are observations, not owners.
#include "combat_effects.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

#include "bw/math/matrix.hpp"
#include "bw/memory/allocator.hpp"
#include "game/world/world_physics.hpp"

namespace bw::game {
CombatEffectEmitter CombatEffects::emitter() noexcept {
  return CombatEffectEmitter(*this);
}
std::optional<effects::SpriteHandle> CombatEffectEmitter::spawn(
    std::uint32_t id, const effects::SpritePlacement& placement) const {
  return owner_.get().spawn(id, placement);
}
bool CombatEffectEmitter::move(
    effects::SpriteHandle handle,
    const effects::SpritePlacement& placement) const {
  return owner_.get().move(handle, placement);
}
bool CombatEffectEmitter::stop(effects::SpriteHandle handle) const {
  return owner_.get().stop(handle);
}
bool CombatEffectEmitter::alive(effects::SpriteHandle handle) const {
  return owner_.get().alive(handle);
}

namespace {
constexpr std::uint32_t kQuadVertices = 6;
constexpr unsigned kTextureIndexBits = 32;
struct TextureBinding {
  std::uint64_t key{};
  render::WorldTextureHandle resident;
};
std::uint64_t texture_key(std::uint16_t pack, std::uint32_t index) {
  return (static_cast<std::uint64_t>(pack) << kTextureIndexBits) | index;
}
class Environment final : public effects::SpriteEnvironment {
 public:
  Environment(const WorldPhysics& physics, const effects::SpriteVector& force)
      : physics_(physics), force_(force) {}
  effects::SpriteVector force_at(const effects::SpriteVector&,
                                 effects::SpriteHandle) const override {
    return force_;
  }
  std::optional<effects::SpriteContact> intersect(
      const effects::SpriteVector& start,
      const effects::SpriteVector& end) const override {
    constexpr std::uint32_t kExcludedObjectFlags = 8;
    const auto hit =
        physics_.queries().ray({.start = start,
                                .end = end,
                                .layers = WorldPhysics::scenery_layers(),
                                .exclude_flags = kExcludedObjectFlags});
    if (!hit) return std::nullopt;
    const auto receiver = physics_.receiver(hit->receiver);
    if (!receiver) {
      throw std::logic_error("Effect collision lacks source receiver metadata");
    }
    // Mesh receivers return their transformed geometric face normal; other
    // original receivers use the fixed up-axis in the particle response.
    constexpr std::uint32_t kMeshReceiver = 5;
    return effects::SpriteContact{hit->position,
                                  receiver->kind == kMeshReceiver
                                      ? hit->normal
                                      : effects::SpriteVector{0, 1, 0}};
  }

 private:
  const WorldPhysics& physics_;
  effects::SpriteVector force_;
};
}  // namespace
struct CombatEffects::State {
  State(const std::shared_ptr<memory::Domain>& domain,
        const world::WorldClock& clock, effects::ParticleRandom& random,
        std::span<const CombatEffectPack> packs,
        std::span<const std::uint32_t> requested_ids,
        render::EffectsPublisher& sink, const WorldPhysics& physics_owner,
        effects::SpriteWorldConfiguration configuration)
      : publisher(sink),
        physics(physics_owner),
        programs(memory::Allocator<effects::SpriteProgram>(domain)),
        textures(memory::Allocator<TextureBinding>(domain)),
        vertices(memory::Allocator<render::WorldSpriteVertex>(domain)),
        batches(memory::Allocator<render::WorldSpriteBatch>(domain)) {
    if (packs.empty() || requested_ids.empty()) {
      throw std::invalid_argument("Missing combat effect resources");
    }
    memory::Vector<formats::EffectPackView> views{
        memory::Allocator<formats::EffectPackView>(domain)};
    views.reserve(packs.size());
    for (std::size_t i = 0; i < packs.size(); ++i) {
      views.emplace_back(packs[i].bytes);
      for (std::size_t j = i + 1; j < packs.size(); ++j) {
        if (packs[i].slot == packs[j].slot) {
          throw std::invalid_argument("Duplicate original effect pack slot");
        }
      }
      for (const auto& texture : packs[i].textures) {
        if (texture.resident.value == 0 || texture.source.width == 0 ||
            texture.source.height == 0) {
          throw std::invalid_argument("Missing resident combat effect texture");
        }
        textures.push_back({texture_key(packs[i].slot, texture.source.index),
                            texture.resident});
      }
    }
    std::ranges::sort(textures, {}, &TextureBinding::key);
    if (std::adjacent_find(textures.begin(), textures.end(),
                           [](const auto& a, const auto& b) {
                             return a.key == b.key;
                           }) != textures.end()) {
      throw std::invalid_argument("Duplicate original effect texture identity");
    }
    programs.reserve(requested_ids.size());
    memory::Vector<effects::SpriteTexture> source_textures{
        memory::Allocator<effects::SpriteTexture>(domain)};
    std::uint64_t maximum_particles = 0;
    for (const auto id : requested_ids) {
      if (std::ranges::any_of(programs,
                              [id](const auto& p) { return p.id() == id; })) {
        throw std::invalid_argument("Duplicate requested combat effect");
      }
      std::optional<std::size_t> selected;
      std::size_t entry = 0;
      // Original startup registers packs in slot order; later matching IDs win.
      for (std::size_t i = 0; i < packs.size(); ++i) {
        if (const auto found = views[i].find_effect(id);
            found && (!selected || packs[*selected].slot < packs[i].slot)) {
          selected = i;
          entry = *found;
        }
      }
      if (!selected) {
        throw std::invalid_argument(
            "Required original combat effect is absent");
      }
      source_textures.clear();
      source_textures.reserve(packs[*selected].textures.size());
      for (const auto& texture : packs[*selected].textures) {
        source_textures.push_back(texture.source);
      }
      programs.emplace_back(views[*selected], entry, packs[*selected].slot,
                            source_textures);
      std::uint64_t particle_count = 0;
      for (const auto& part : programs.back().parts()) {
        particle_count += part.capacity;
      }
      maximum_particles = std::max(maximum_particles, particle_count);
    }
    const auto particles = maximum_particles * configuration.instance_capacity;
    if (particles == 0 ||
        particles > std::numeric_limits<std::uint32_t>::max() / kQuadVertices) {
      throw std::length_error("Combat sprite packet capacity overflow");
    }
    capacity = {static_cast<std::uint32_t>(particles * kQuadVertices),
                static_cast<std::uint32_t>(particles)};
    vertices.reserve(capacity.vertices);
    batches.reserve(capacity.batches);
    simulation.emplace(domain, clock, random, programs, configuration);
  }
  render::WorldTextureHandle texture(std::uint16_t pack,
                                     std::uint32_t index) const {
    const auto key = texture_key(pack, index);
    const auto found =
        std::ranges::lower_bound(textures, key, {}, &TextureBinding::key);
    if (found == textures.end() || found->key != key) {
      throw std::logic_error(
          "Prepared combat effect lost its resident texture");
    }
    return found->resident;
  }
  effects::SpriteWorld& simulation_owner() {
    if (!simulation) {
      throw std::logic_error("Combat effect construction is incomplete");
    }
    return *simulation;
  }
  render::EffectsPublisher& publisher;
  const WorldPhysics& physics;
  memory::Vector<effects::SpriteProgram> programs;
  memory::Vector<TextureBinding> textures;
  memory::Vector<render::WorldSpriteVertex> vertices;
  memory::Vector<render::WorldSpriteBatch> batches;
  render::WorldSpriteCapacity capacity{};
  std::optional<effects::SpriteWorld> simulation;
};
CombatEffects::CombatEffects(const std::shared_ptr<memory::Domain>& domain,
                             const world::WorldClock& clock,
                             effects::ParticleRandom& random,
                             std::span<const CombatEffectPack> packs,
                             std::span<const std::uint32_t> requested_ids,
                             render::EffectsPublisher& publisher,
                             const WorldPhysics& physics,
                             effects::SpriteWorldConfiguration configuration)
    : state_(std::allocate_shared<State>(
          memory::Allocator<State>(domain), domain, clock, random, packs,
          requested_ids, publisher, physics, configuration)) {}
CombatEffects::~CombatEffects() = default;
render::WorldSpriteCapacity CombatEffects::capacity() const noexcept {
  return state_->capacity;
}
std::optional<effects::SpriteHandle> CombatEffects::spawn(
    std::uint32_t effect, const effects::SpritePlacement& placement) {
  return state_->simulation_owner().spawn(effect, placement);
}
bool CombatEffects::move(effects::SpriteHandle handle,
                         const effects::SpritePlacement& placement) {
  return state_->simulation_owner().move(handle, placement);
}
bool CombatEffects::stop(effects::SpriteHandle handle) {
  return state_->simulation_owner().stop(handle);
}
bool CombatEffects::alive(effects::SpriteHandle handle) const {
  return state_->simulation_owner().alive(handle);
}
void CombatEffects::advance(const world::WorldFrame& frame,
                            const effects::SpriteVector& base_force) {
  if (!math::all_finite(base_force)) {
    throw std::invalid_argument("Nonfinite combat effect force observation");
  }
  const Environment environment(state_->physics, base_force);
  state_->simulation_owner().advance(frame, environment);
}
void CombatEffects::publish(const CombatEffectView& view) {
  const auto& environment = view.environment;
  if (!math::all_finite(view.clip_rows) ||
      !math::all_finite(environment.alpha_color_rate) ||
      !math::all_finite(environment.additive_color_rate) ||
      !std::isfinite(environment.fog_near) ||
      !std::isfinite(environment.fog_far) ||
      environment.fog_far <= environment.fog_near) {
    throw std::invalid_argument("Invalid combat effect view observation");
  }
  auto& state = *state_;
  const auto draw = state.simulation_owner().publish(view.camera);
  state.vertices.clear();
  state.batches.clear();
  for (const auto& vertex : draw.vertices) {
    state.vertices.push_back({vertex.position, vertex.color, vertex.uv});
  }
  for (const auto& batch : draw.batches) {
    state.batches.push_back(
        {batch.first_vertex, batch.vertex_count,
         state.texture(batch.pack_slot, batch.texture_index),
         batch.blend == effects::SpriteBlend::alpha
             ? render::WorldSpriteBlend::alpha
             : render::WorldSpriteBlend::additive});
  }
  state.publisher.publish({state.vertices, state.batches, view.clip_rows,
                           environment.alpha_color_rate,
                           environment.additive_color_rate, environment.fog_far,
                           1 / (environment.fog_far - environment.fog_near)});
}
}  // namespace bw::game
