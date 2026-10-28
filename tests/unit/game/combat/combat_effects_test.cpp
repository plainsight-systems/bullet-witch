// Synthetic public adapter contracts: source precedence, copied admission,
// resident texture publication, authored environment and supplied-clock pause.
// Real provider remains empty here; native collision/effect execution witnesses
// are separate. I.11/MEM.9 enforce borrows and bounded frame allocations.
#include "game/combat/combat_effects.hpp"

#include <algorithm>
#include <memory_resource>
#include <stdexcept>
#include <type_traits>

#include "../../../fixtures/effect_fixture.hpp"
#include "game/combat/combat_world.hpp"
#include "game/combat/weapon_presentation.hpp"
#include "game/world/world_physics.hpp"
namespace {
class Backing final : public bw::memory::CpuBacking {
 public:
  void* allocate(std::size_t bytes, std::size_t alignment) noexcept override {
    ++allocations;
    try {
      return std::pmr::new_delete_resource()->allocate(bytes, alignment);
    } catch (const std::bad_alloc&) {
      return nullptr;
    }
  }
  void deallocate(void* p, std::size_t bytes,
                  std::size_t alignment) noexcept override {
    std::pmr::new_delete_resource()->deallocate(p, bytes, alignment);
  }
  std::size_t allocations{};
};
void require(bool value, const char* message) {
  if (!value) {
    throw std::runtime_error(message);
  }
}
template <class F>
void rejects(F function) {
  bool caught = false;
  try {
    function();
  } catch (const std::exception&) {
    caught = true;
  }
  require(caught, "Invalid effect adapter input admitted");
}
void weapon_presentation(bw::world::WorldClock& clock,
                         bw::game::CombatEffects& effects,
                         const std::shared_ptr<bw::memory::Domain>& domain) {
  using bw::game::WeaponPresentation;
  static_assert(!std::is_copy_constructible_v<WeaponPresentation>);
  static_assert(!std::is_move_constructible_v<WeaponPresentation>);
  bw::scene::SceneLighting lights({{}, {}, {.05F, .15F, 4, false}, {}}, domain);
  lights.prepare_emitters(1);
  const bw::scene::LocalLightViewQuery view{
      {},
      bw::math::make_perspective_frustum(
          {{{1, 0, 0}, {0, 1, 0}, {0, 0, 1}, {0, 0, 0}}},
          {-1, 1, -1, 1, 1, 100})};
  const bw::scene::LightingReceiver receiver{
      .policy = bw::scene::ReceiverLightingPolicy::stage_ambient};
  bw::game::WeaponPresentationDefinition definition{
      {42, 42}, {2, 4, 6}, {3, 5, 7}, {1.8F, 1, .15F}, .2F};
  WeaponPresentation weapon(clock, definition, effects.emitter(),
                            lights.acquire_emitter());
  bw::math::Affine3<float> muzzle{{{1, 0, 0}, {0, 1, 0}, {0, 0, 1}, {0, 0, 3}}};
  const auto bytes = domain->snapshot().live_bytes;
  weapon.accepted_shot(muzzle, {.5F, 1, 2}, true);
  lights.set_view(view);
  auto initial = lights.evaluate(receiver).local_lights;
  require(
      initial.count == 1 && initial.lights[0].diffuse[0] < 1 &&
          initial.lights[0].specular == bw::math::Vector3<float>{1.5F, 5, 14},
      "Shot captures separate source RGB and exact initial light intensity");
  const auto paused = clock.publish(.1F, {false, true});
  weapon.advance(paused);
  lights.set_view(view);
  require(lights.evaluate(receiver).local_lights.lights[0].diffuse ==
              initial.lights[0].diffuse,
          "Paused world must not fade muzzle light");
  rejects([&] { weapon.advance(paused); });
  weapon.advance(clock.publish(.1F, {}));
  lights.set_view(view);
  auto faded = lights.evaluate(receiver).local_lights;
  require(faded.count == 1 &&
              std::abs(faded.lights[0].diffuse[0] - .5F) < 1e-5F &&
              faded.lights[0].specular == initial.lights[0].specular,
          "Original lifetime fades diffuse linearly but preserves specular");
  muzzle[3][0] = 1;
  weapon.move(muzzle);
  lights.set_view(view);
  require(
      lights.evaluate(receiver).local_lights.lights[0].position == muzzle[3],
      "Muzzle attachment updates only owned emitter");
  weapon.advance(clock.publish(.0999F, {}));
  lights.set_view(view);
  require(
      lights.evaluate(receiver).local_lights.count == 1,
      "Fading source light retains unordered internal radius without throwing");
  weapon.advance(clock.publish(.001F, {}));
  lights.set_view(view);
  require(lights.evaluate(receiver).local_lights.count == 0,
          "Expired muzzle light leaves scene selection");
  weapon.accepted_shot(muzzle, {1, 1, 1}, true);
  lights.set_view(view);
  require(lights.evaluate(receiver).local_lights.count == 1,
          "New accepted shot replaces lifetime without another registration");
  weapon.retire();
  lights.set_view(view);
  require(lights.evaluate(receiver).local_lights.count == 0 &&
              domain->snapshot().live_bytes == bytes,
          "Weapon retirement removes light without frame allocations");
  effects.advance(clock.publish(.3F, {}), {});
}
class Output final : public bw::render::WorldPresentation {
 public:
  bw::render::WorldGeometryStatus geometry_status(
      bw::render::WorldModelHandle) const override {
    return {};
  }
  void set_world_output_parameters(
      const bw::render::WorldOutputParameters&) override {}
  void set_world_draws(std::span<const bw::render::WorldColorDraw>) override {}
  void replace_world_model_draws(
      bw::render::WorldModelHandle,
      std::span<const bw::render::WorldColorDraw>) override {}
  void add_world_instance_draws(
      std::span<const bw::render::WorldColorDraw>) override {}
  void remove_world_instance_draws(bw::render::WorldModelHandle,
                                   std::uint64_t) override {}
  void update_world_texture_offsets(
      std::span<const bw::render::WorldTextureOffsetUpdate>) override {}
  void configure_world_sprites(
      const bw::render::WorldSpriteCapacity&) override {}
  void update_world_sprites(
      const bw::render::WorldSpriteUpdate& update) override {
    vertex_count = update.vertices.size();
    batch_count = update.batches.size();
    if (!update.batches.empty()) {
      texture = update.batches.front().texture;
    }
    if (!update.vertices.empty()) {
      first = update.vertices.front();
    }
    alpha = update.alpha_color_rate;
    additive = update.additive_color_rate;
    far = update.fog_far;
    reciprocal = update.fog_distance_reciprocal;
  }
  void update_world_lighting(
      std::span<const bw::render::WorldLightingUpdate>) override {}
  void update_world_poses(
      std::span<const bw::render::WorldPoseUpdate>) override {}
  void update_shape_positions(
      std::span<const bw::render::WorldShapePositionUpdate>) override {}
  void update_world_visibility(
      std::span<const bw::render::WorldVisibilityUpdate>) override {}
  void update_world_view(const bw::render::WorldViewUpdate&) override {}
  void clear_world_draws() noexcept override {}
  std::size_t vertex_count{}, batch_count{};
  bw::render::WorldTextureHandle texture;
  bw::render::WorldSpriteVertex first{};
  std::array<float, 3> alpha{}, additive{};
  float far{}, reciprocal{};
};
// A per-projectile discharge remains at its copied ejection frame after the
// barrel moves and its retained weapon presentation retires.
void detached_discharge(bw::world::WorldClock& clock,
                        bw::game::CombatEffects& effects, Output& output,
                        const bw::game::CombatEffectView& view,
                        const std::shared_ptr<bw::memory::Domain>& domain) {
  effects.advance(clock.publish(3.F, {}), {});
  bw::scene::SceneLighting lights({{}, {}, {.05F, .15F, 4, false}, {}}, domain);
  lights.prepare_emitters(1);
  bw::game::WeaponPresentationDefinition definition;
  definition.attenuation = {1, 1, 1};
  definition.discharge_effect = 42;
  bw::game::WeaponPresentation weapon(clock, definition, effects.emitter(),
                                      lights.acquire_emitter());
  bw::math::Affine3<float> discharge{
      {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}, {20, 0, 0}}};
  weapon.discharge(discharge);
  effects.advance(clock.publish(.01F, {}), {});
  effects.publish(view);
  require(output.vertex_count == 6 && output.first.position[0] > 15,
          "Discharge did not use its independent copied attachment");
  auto barrel = discharge;
  barrel[3] = {-20, 0, 0};
  weapon.move(barrel);
  weapon.retire();
  effects.advance(clock.publish(.01F, {}), {});
  effects.publish(view);
  require(output.vertex_count == 6 && output.first.position[0] > 15,
          "Retained barrel movement or retirement stole detached discharge");
  effects.advance(clock.publish(.4F, {}), {});
}
// Observing audio and copied game receiver verify dispatch, not audible parity.
class SoundOutput final : public bw::audio::CueOutput {
 public:
  void register_bank(std::uint16_t, std::span<const std::uint8_t>,
                     std::span<const std::uint8_t>,
                     std::span<const std::uint16_t>) override {}
  void seal() override {}
  std::optional<bw::audio::CueHandle> play(
      const bw::audio::CueRequest& value) override {
    request = value;
    return bw::audio::CueHandle{0, ++plays};
  }
  bool set_position(bw::audio::CueHandle,
                    const bw::math::Vector3<float>&) override {
    return false;
  }
  bool stop(bw::audio::CueHandle) override { return false; }
  bool active(bw::audio::CueHandle) const override { return false; }
  void set_listener(const bw::audio::Listener&) override {}
  void set_mix_gains(const bw::audio::MixGains&) override {}
  void pause(bool) override {}
  void update() override {}
  bw::audio::CueRequest request;
  std::uint32_t plays{};
};
class Receivers final : public bw::game::CombatReceivers {
 public:
  void receive(const bw::game::CombatImpact& value) override {
    impact = value;
    ++hits;
  }
  bw::game::CombatImpact impact;
  unsigned hits{};
};
void projectile_response(bw::world::WorldClock& clock,
                         bw::game::WorldPhysics& physics,
                         bw::game::CombatEffects& effects,
                         const std::shared_ptr<bw::memory::Domain>& domain) {
  constexpr std::array<bw::physics::Vector3, 3> vertices{
      {{-10, -10, 5}, {0, 10, 5}, {10, -10, 5}}};
  constexpr std::array<bw::physics::Triangle, 1> faces{{{{0, 1, 2}, 0x0703}}};
  const auto resources = physics.resources();
  bw::physics::BodyDescription receiver;
  receiver.shape =
      resources.create_shape(bw::physics::TriangleMesh{vertices, faces});
  receiver.layer = 6;
  receiver.user_data = 9;
  const auto body = resources.create_body(receiver);
  bw::game::StraightWeaponDefinition weapon;
  weapon.range = 100;
  weapon.speed = 10;
  weapon.damage = {{10, 10, 10}, {1, 1, 1}, 10, 100};
  weapon.effects.projectile = 42;
  weapon.effects.surface_impact.fill(-1);
  weapon.effects.surface_impact[7] = 42;
  const auto sound = bw::game::WeaponSoundProfile::original("wa0");
  SoundOutput audio;
  Receivers received;
  bw::game::CombatWorld combat(clock, physics.queries(), {2, 10}, effects,
                               audio, domain);
  combat.emit({.definition = &weapon,
               .direction = {0, 0, 1},
               .owner = 4,
               .level = 1,
               .actor_layers = 1 << 6},
              sound);
  auto frame = clock.publish(.6F, {.simulation = false});
  physics.advance(frame);
  combat.advance(frame, {100, 100, 100}, received);
  require(combat.active_count() == 1 && received.hits == 0 && audio.plays == 0,
          "Paused projectile dispatched damage or presentation");
  frame = clock.publish(.6F, {});
  physics.advance(frame);
  combat.advance(frame, {100, 100, 100}, received);
  effects.advance(frame, {});
  require(combat.active_count() == 0 && received.hits == 1 &&
              received.impact.target == 9 && received.impact.source == 4 &&
              received.impact.body == 3 && received.impact.surface == 7 &&
              received.impact.damage == 10 &&
              received.impact.receiver == bw::physics::ReceiverId{body},
          "Provider contact did not preserve source actor/body/surface damage");
  require(audio.plays == 1 && audio.request.cue == 0xc2,
          "Impact did not dispatch original surface cue");
  combat.retire();
}
}  // namespace
void combat_effects_tests() {
  using bw::game::CombatEffects;
  static_assert(!std::is_copy_constructible_v<CombatEffects> &&
                !std::is_move_constructible_v<CombatEffects>);
  auto backing = std::make_shared<Backing>();
  auto domain = bw::memory::Domain::create(backing);
  {
    bw::world::WorldClock clock;
    bw::effects::ParticleRandom random;
    bw::scene::StaticCollision geometry(domain);
    geometry.prepare_character_queries();
    bw::physics::Configuration config;
    config.limits = {.bodies = 8,
                     .shapes = 8,
                     .constraints = 4,
                     .body_pairs = 32,
                     .contacts = 32,
                     .commands = 32,
                     .scratch_bytes = 1024 * 1024};
    bw::game::WorldPhysics physics(clock, geometry, {}, config, domain);
    Output output;
    bw::render::RenderScene scene(output);
    scene.register_draws({});
    auto bytes = effect_fixture::fixture();
    const std::array<bw::game::CombatEffectTexture, 1> first{
        {{{0, 64, 64}, {11}}}};
    const std::array<bw::game::CombatEffectTexture, 1> second{
        {{{0, 64, 64}, {22}}}};
    const std::array<bw::game::CombatEffectPack, 2> packs{
        {{1, bytes, first}, {4, bytes, second}}};
    const std::array<std::uint32_t, 1> ids{42};
    CombatEffects effects(domain, clock, random, packs, ids,
                          scene.effects_publisher(), physics, {2});
    scene.configure_sprites(effects.capacity());
    require(effects.capacity().vertices == 108,
            "Prepared capacity lost source particle bound");
    // Constructor must retain programs, not the byte view used to prepare them.
    std::ranges::fill(bytes, 0);
    const auto allocations = backing->allocations;
    const auto handle = effects.spawn(42, {});
    if (!handle) {
      throw std::runtime_error("Prepared effect failed admission");
    }
    bw::game::CombatEffectView view;
    view.clip_rows = bw::math::identity_matrix<float, 4>();
    view.environment = {{.2F, .4F, .6F}, {.8F, .7F, .5F}, 2, 10};
    effects.advance(clock.publish(.01F, {}), {});
    effects.publish(view);
    require(
        output.vertex_count == 6 && output.batch_count == 1 &&
            output.texture.value == 22,
        "Publication did not resolve winning source pack to resident texture");
    require(output.alpha == view.environment.alpha_color_rate &&
                output.additive == view.environment.additive_color_rate &&
                output.far == 10 && output.reciprocal == .125F,
            "Authored environment did not reach renderer");
    const auto before = output.first;
    effects.advance(clock.publish(.1F, {false, true}), {});
    effects.publish(view);
    require(output.first.position == before.position &&
                output.first.color == before.color,
            "Paused effect advanced");
    require(backing->allocations == allocations,
            "Successful frame work allocated");
    require(effects.stop(handle.value()), "Prepared handle stop failed");
    effects.advance(clock.publish(.3F, {}), {});
    projectile_response(clock, physics, effects, domain);
    weapon_presentation(clock, effects, domain);
    detached_discharge(clock, effects, output, view, domain);
    bytes = effect_fixture::fixture();
    const std::array<std::uint32_t, 1> missing{100};
    rejects([&] {
      CombatEffects invalid(domain, clock, random, packs, missing,
                            scene.effects_publisher(), physics, {2});
    });
    const std::array<bw::game::CombatEffectPack, 1> no_textures{
        {{1, bytes, {}}}};
    rejects([&] {
      CombatEffects invalid(domain, clock, random, no_textures, ids,
                            scene.effects_publisher(), physics, {2});
    });
    scene.retire();
    rejects([&] { effects.publish(view); });
  }
  require(domain->snapshot().live_allocations == 0,
          "Effect adapter teardown retained domain storage");
}
