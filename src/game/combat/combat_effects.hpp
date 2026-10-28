// Game admission and publication bridge for original combat sprites. Mission
// content owns resident textures; constructor input bytes/bindings are borrowed
// only while immutable SpritePrograms and texture identities are prepared.
// The adapter owns simulation/storage, borrows the const issuing world clock
// and session particle random stream (draw only), update-only render publisher,
// and observes query-only world physics. No files, native resources, shot
// policy, damage, audio or second clock here. Successful frame work uses
// preallocated packets; resource capacity is explicit. Header design Core I.11
// (#ri-raw) separates retained ownership from borrows; performance MEM.9
// (guidelines/memory/MEM.9-embedded-allocate-at-init.md) bounds preparation and
// prohibits successful frame allocation. Both corpora read for this design;
// original/reference contracts: first-combat-effects.md.
#pragma once

#include <functional>

#include "bw/effects/sprite_world.hpp"
#include "bw/render/scene/render_scene.hpp"
#include "game/content/original/stage_environment_settings.hpp"

namespace bw::game {
class WorldPhysics;
struct CombatEffectTexture {
  effects::SpriteTexture source;
  render::WorldTextureHandle resident;
};
struct CombatEffectPack {
  std::uint16_t slot{};
  std::span<const std::uint8_t> bytes;
  std::span<const CombatEffectTexture> textures;
};
struct CombatEffectView {
  effects::SpriteCamera camera;
  std::array<std::array<float, 4>, 4> clip_rows{};
  formats::StageEffects environment;
};
class CombatEffectEmitter;
class CombatEffects {
 public:
  CombatEffects(const std::shared_ptr<memory::Domain>& domain,
                const world::WorldClock& clock, effects::ParticleRandom& random,
                std::span<const CombatEffectPack> packs,
                std::span<const std::uint32_t> requested_ids,
                render::EffectsPublisher& publisher,
                const WorldPhysics& physics,
                effects::SpriteWorldConfiguration configuration);
  ~CombatEffects();
  CombatEffects(const CombatEffects&) = delete;
  CombatEffects& operator=(const CombatEffects&) = delete;
  CombatEffects(CombatEffects&&) = delete;
  CombatEffects& operator=(CombatEffects&&) = delete;
  // Mission configures its scene with this bound before first publication.
  render::WorldSpriteCapacity capacity() const noexcept;
  CombatEffectEmitter emitter() noexcept;
  std::optional<effects::SpriteHandle> spawn(
      std::uint32_t effect, const effects::SpritePlacement& placement);
  bool move(effects::SpriteHandle handle,
            const effects::SpritePlacement& placement);
  bool stop(effects::SpriteHandle handle);
  bool alive(effects::SpriteHandle handle) const;
  // Original setup initializes base force to zero. The mission supplies its
  // current copied value; no radial force producer belongs to this encounter.
  void advance(const world::WorldFrame& frame,
               const effects::SpriteVector& base_force);
  void publish(const CombatEffectView& view);

 private:
  struct State;
  std::shared_ptr<State> state_;
};
// Non-owning emission capability; CombatEffects must outlive it. No frame,
// publication, configuration or random-stream authority is granted. I.11 and
// MEM.2 retain explicit lifetime and fixed prepared source storage.
class CombatEffectEmitter {
 public:
  std::optional<effects::SpriteHandle> spawn(
      std::uint32_t id, const effects::SpritePlacement& placement) const;
  bool move(effects::SpriteHandle, const effects::SpritePlacement&) const;
  bool stop(effects::SpriteHandle) const;
  bool alive(effects::SpriteHandle) const;

 private:
  friend class CombatEffects;
  explicit CombatEffectEmitter(CombatEffects& owner) : owner_(owner) {}
  std::reference_wrapper<CombatEffects> owner_;
};
}  // namespace bw::game
