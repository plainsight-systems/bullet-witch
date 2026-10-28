// Public prepared-program and owner contracts. Synthetic packs verify schema,
// lifecycle, bounded publication and pause; original execution witnesses live
// in ignored research and are not replaced by these fixture tests.
// Core SL.con.3 (#rsl-bounds) and performance MEM.9 (guidelines/memory/
// MEM.9-embedded-allocate-at-init.md) cover pool limits and stable allocation.
#include "bw/effects/sprite_world.hpp"

#include <bit>
#include <cmath>
#include <memory_resource>
#include <stdexcept>
#include <thread>

#include "../../../fixtures/effect_fixture.hpp"

namespace {
using model_fixture::word;
void require(bool value, const char* message) {
  if (!value) throw std::runtime_error(message);
}
template <class Function>
void rejects(Function function) {
  bool caught = false;
  try {
    function();
  } catch (const std::exception&) {
    caught = true;
  }
  require(caught, "Invalid sprite operation was admitted");
}
using effect_fixture::fixture;
using effect_fixture::number;
class TestBacking final : public bw::memory::CpuBacking {
 public:
  void* allocate(std::size_t bytes, std::size_t alignment) noexcept override {
    ++allocations;
    try {
      return std::pmr::new_delete_resource()->allocate(bytes, alignment);
    } catch (const std::bad_alloc&) {
      return nullptr;
    }
  }
  void deallocate(void* pointer, std::size_t bytes,
                  std::size_t alignment) noexcept override {
    std::pmr::new_delete_resource()->deallocate(pointer, bytes, alignment);
  }
  std::size_t allocations{};
};
class EmptyEnvironment final : public bw::effects::SpriteEnvironment {
 public:
  bw::effects::SpriteVector force_at(const bw::effects::SpriteVector&,
                                     bw::effects::SpriteHandle) const override {
    return {};
  }
  std::optional<bw::effects::SpriteContact> intersect(
      const bw::effects::SpriteVector&,
      const bw::effects::SpriteVector&) const override {
    return std::nullopt;
  }
};
}  // namespace
void sprite_world_tests() {
  using bw::effects::SpriteProgram;
  using bw::effects::SpriteTexture;
  using bw::effects::SpriteVector;
  using bw::effects::SpriteWorld;
  const bw::math::Affine3<float> camera_view{
      {{0, 0, -1}, {0, 1, 0}, {1, 0, 0}, {3, 4, 5}}};
  const auto camera = bw::effects::make_sprite_camera(camera_view, {6, 7, 8});
  require(camera.position == SpriteVector{6, 7, 8} &&
              camera.view_translation == SpriteVector{3, 4, 5},
          "Camera eye and view translation were collapsed");
  require(
      camera.billboard ==
              bw::effects::SpriteBasis{{{0, 0, 1}, {0, 1, 0}, {-1, 0, 0}}} &&
          camera.cylindrical_billboard == camera.billboard,
      "Source billboard rotation conversion");
  auto degenerate = camera_view;
  degenerate[2] = {0, 1, 0};
  rejects([&] { (void)bw::effects::make_sprite_camera(degenerate, {}); });
  constexpr std::array<SpriteTexture, 1> textures{{{0, 64, 64}}};
  auto bytes = fixture();
  const bw::formats::EffectPackView pack(bytes);
  std::array programs{SpriteProgram(pack, 0, 3, textures)};
  require(
      programs[0].parts().size() == 1 && programs[0].parts()[0].capacity == 9,
      "Program preserves source pool tier");
  auto invalid = bytes;
  word(invalid, 816 + 0x5c, 1);
  rejects([&] {
    const bw::formats::EffectPackView view(invalid);
    (void)SpriteProgram(view, 0, 0, textures);
  });
  invalid = bytes;
  number(invalid, 272 + 0x14, .2F);
  rejects([&] {
    const bw::formats::EffectPackView view(invalid);
    (void)SpriteProgram(view, 0, 0, textures);
  });
  rejects([&] { (void)SpriteProgram(pack, 0, 0, {}); });
  auto backing = std::make_shared<TestBacking>();
  auto domain = bw::memory::Domain::create(backing);
  bw::world::WorldClock clock, foreign;
  bw::effects::ParticleRandom random;
  EmptyEnvironment environment;
  {
    SpriteWorld world(domain, clock, random, programs, {1});
    const auto allocations = backing->allocations;
    const auto handle = world.spawn(42, {});
    if (!handle) throw std::runtime_error("First effect was not admitted");
    require(handle.has_value() && !world.spawn(42, {}),
            "Capacity rejects without stealing an instance");
    const auto paused = clock.publish(.05F, {false, true});
    world.advance(paused, environment);
    require(world.active_particles() == 0, "Paused frame does not emit");
    rejects([&] { world.advance(paused, environment); });
    rejects([&] { world.advance(foreign.publish(.01F, {}), environment); });
    world.advance(clock.publish(.05F, {}), environment);
    require(world.active_particles() == 1, "Initial burst precedes motion");
    const auto draw = world.publish({});
    require(draw.vertices.size() == 6 && draw.batches.size() == 1 &&
                draw.batches[0].pack_slot == 3 &&
                draw.batches[0].vertex_count == 6,
            "CPU publication carries bounded geometry and pack identity");
    require(draw.vertices[0].position == SpriteVector{-2, 3, 0} &&
                draw.vertices[2].position == SpriteVector{2, -3, 0},
            "Camera-facing authored extents");
    require(world.stop(handle.value()), "Stop begins authored fade");
    world.advance(clock.publish(.05F, {}), environment);
    const auto faded = world.publish({});
    require(faded.vertices.size() == 6 &&
                std::abs(faded.vertices[0].color[3] - .8F) < 1e-6F,
            "Stop fade modulates alpha");
    world.advance(clock.publish(.21F, {}), environment);
    require(!world.alive(handle.value()) && world.active_particles() == 0,
            "Fade retirement releases capacity");
    const auto replacement = world.spawn(42, {});
    require(replacement &&
                replacement->generation != handle.value().generation &&
                !world.move(handle.value(), {}),
            "Stale generation cannot mutate reused slot");
    bool wrong_thread = false;
    std::thread other([&] {
      try {
        (void)world.active_instances();
      } catch (const std::logic_error&) {
        wrong_thread = true;
      }
    });
    other.join();
    require(wrong_thread, "Owner-thread check rejects foreign observer");
    require(backing->allocations == allocations,
            "Steady-state emission/publication does not allocate");
  }
  require(domain->snapshot().live_allocations == 0,
          "World teardown releases owned storage");
  number(bytes, 80 + 0x48, 0);
  const bw::formats::EffectPackView immediate_pack(bytes);
  programs[0] = SpriteProgram(immediate_pack, 0, 0, textures);
  const auto prior_random = random.state();
  SpriteWorld immediate(domain, clock, random, programs, {1});
  require(random.state() == prior_random,
          "Mission reconstruction reseeded session particle random");
  const auto first = immediate.spawn(42, {});
  require(first && immediate.stop(*first), "Immediate stop before first burst");
  require(immediate.spawn(42, {}).has_value(),
          "Immediate stop admits replacement");
  immediate.advance(clock.publish(.05F, {}), environment);
  require(immediate.active_particles() == 1,
          "Reused instance is advanced once in original order");
  // Distance fade observes camera position without advancing simulation; tint
  // bypass affects RGB only, while instance opacity still multiplies alpha.
  auto distance_bytes = fixture();
  word(distance_bytes, 272 + 0x194, 1);
  number(distance_bytes, 272 + 0x13c, 1);
  number(distance_bytes, 272 + 0x140, 3);
  word(distance_bytes, 816 + 0x20, 1);
  const bw::formats::EffectPackView distance_pack(distance_bytes);
  std::array distance_programs{SpriteProgram(distance_pack, 0, 0, textures)};
  SpriteWorld distance_world(domain, clock, random, distance_programs, {2});
  bw::effects::SpritePlacement near_placement;
  near_placement.transform[3] = {0, 0, 2};
  near_placement.tint = {.1F, .2F, .3F, .5F};
  require(distance_world.spawn(42, near_placement).has_value(),
          "Distance fade fixture admission");
  auto far_placement = near_placement;
  far_placement.transform[3] = {0, 0, 4};
  require(distance_world.spawn(42, far_placement).has_value(),
          "Depth order fixture admission");
  distance_world.advance(clock.publish(.01F, {}), environment);
  const auto ordered = distance_world.publish({});
  require(ordered.vertices.size() == 12 &&
              ordered.vertices[0].position[2] == 4 &&
              ordered.vertices[6].position[2] == 2,
          "Source instance sorting publishes back to front");
  require(ordered.vertices[0].color[0] == 1 &&
              std::abs(ordered.vertices[0].color[3] - .5F) < 1e-6F &&
              std::abs(ordered.vertices[6].color[3] - .1875F) < 1e-6F,
          "Squared camera fade and RGB tint bypass preserve opacity");
  bw::effects::SpriteCamera nearby;
  nearby.position = {0, 0, 2};
  nearby.view_translation = {0, 0, -2};
  const auto culled_near = distance_world.publish(nearby);
  require(culled_near.vertices.size() == 6,
          "Near-distance fade removes the camera-coincident particle");
}
