// Mission-owned original sprite simulation and ordered draw publication.
// Programs and WorldClock are immutable borrows; the shared ParticleRandom
// is a draw-only mutable dependency, never reseeded here. All must outlive
// this owner. Handles are local to this world; stale generations reject writes.
// All methods run on the constructing thread. advance accepts each frame once,
// preserves admission/pause and consumes its supplied delta; no wall clock,
// independent simulation clock, or game mutation exists here. Local ages belong
// to particles/emitters. Spawn binds ordered parts; update bursts,
// ages/retires, then emits periodic particles in original order using one
// shared RNG. Fixed pools and publication storage allocate from the supplied
// memory domain at construction, never during successful steady-state work.
// Full admission reports failure without stealing live effects. Draw spans
// borrow this owner until the next publication/destruction; renderers must copy
// for async use. Core I.11 (#ri-raw) names borrowed dependencies; SL.con.3
// (#rsl-bounds) bounds slots. Performance MEM.9
// (guidelines/memory/MEM.9-embedded-allocate-at-init.md) preallocates pools.
// Both corpora queried for design; source-first ordering and the pinned Unreal
// ownership comparison are in first-combat-effects.md.
#pragma once

#include <memory>

#include "bw/effects/particle_sampling.hpp"
#include "bw/effects/sprite_program.hpp"
#include "bw/memory/domain.hpp"
#include "bw/timing/world_clock.hpp"

namespace bw::effects {
struct SpriteHandle {
  std::uint32_t slot{}, generation{};
  friend bool operator==(const SpriteHandle&, const SpriteHandle&) = default;
};
struct SpritePlacement {
  math::Affine3<float> transform{{{1, 0, 0}, {0, 1, 0}, {0, 0, 1}, {0, 0, 0}}};
  std::array<float, 4> tint{1, 1, 1, 1};
  std::array<float, 2> size_scale{1, 1};
};
struct SpriteContact {
  SpriteVector position;
  // Original response axis is receiver-dependent, not necessarily the contact
  // normal. The game adapter resolves that metadata after the shared ray query.
  SpriteVector response_axis;
};
class SpriteEnvironment {
 public:
  SpriteEnvironment() = default;
  virtual ~SpriteEnvironment() = default;
  SpriteEnvironment(const SpriteEnvironment&) = delete;
  SpriteEnvironment& operator=(const SpriteEnvironment&) = delete;
  SpriteEnvironment(SpriteEnvironment&&) = delete;
  SpriteEnvironment& operator=(SpriteEnvironment&&) = delete;
  // Includes original global/radial effects force; caller supplies the
  // immutable world observation. A zero vector means the world has no active
  // force here.
  virtual SpriteVector force_at(const SpriteVector& position,
                                SpriteHandle source) const = 0;
  virtual std::optional<SpriteContact> intersect(
      const SpriteVector& start, const SpriteVector& end) const = 0;
};
struct SpriteCamera {
  // World position and affine view translation are distinct source inputs.
  SpriteVector position{}, view_translation{};
  SpriteBasis view_rotation{math::identity_matrix<float, 3>()};
  SpriteBasis billboard{math::identity_matrix<float, 3>()};
  SpriteBasis cylindrical_billboard{math::identity_matrix<float, 3>()};
};
// Prepare the original billboard matrices from basis-first world-to-view
// affine input and copied world eye. The fixed-up cylindrical path is formed
// before transposing to billboard space. Finite, nondegenerate inputs required;
// rejects invalid input with no allocation. Both-phase I.11/MEM.9 apply.
SpriteCamera make_sprite_camera(const math::Affine3<float>& view,
                                const SpriteVector& eye);
struct SpriteVertex {
  SpriteVector position;
  std::array<float, 4> color;
  std::array<float, 2> uv;
};
struct SpriteBatch {
  std::uint32_t first_vertex{}, vertex_count{};
  std::uint16_t pack_slot{};
  std::uint32_t texture_index{};
  SpriteBlend blend{};
};
struct SpriteDraw {
  std::span<const SpriteVertex> vertices;
  std::span<const SpriteBatch> batches;
};
struct SpriteWorldConfiguration {
  std::uint32_t instance_capacity{};
};
class SpriteWorld {
 public:
  SpriteWorld(const std::shared_ptr<memory::Domain>& domain,
              const world::WorldClock& clock, ParticleRandom& random,
              std::span<const SpriteProgram> programs,
              SpriteWorldConfiguration configuration);
  ~SpriteWorld();
  SpriteWorld(const SpriteWorld&) = delete;
  SpriteWorld& operator=(const SpriteWorld&) = delete;
  SpriteWorld(SpriteWorld&&) = delete;
  SpriteWorld& operator=(SpriteWorld&&) = delete;
  std::optional<SpriteHandle> spawn(std::uint32_t effect,
                                    const SpritePlacement& placement);
  bool move(SpriteHandle handle, const SpritePlacement& placement);
  // Stops according to the program's fade duration; zero duration retires now.
  bool stop(SpriteHandle handle);
  bool alive(SpriteHandle handle) const;
  void advance(const world::WorldFrame& frame,
               const SpriteEnvironment& environment);
  SpriteDraw publish(const SpriteCamera& camera);
  std::size_t active_instances() const;
  std::size_t active_particles() const;

 private:
  struct State;
  std::shared_ptr<State> state_;
};
}  // namespace bw::effects
