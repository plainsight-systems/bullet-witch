// In-process scene lifecycle/capability tests, without GPU or game data. The
// fake backend observes calls/failure ordering, not material/render algorithms.
// Core I.1/E.6 and performance MEM.9 (guidelines/memory/
// MEM.9-embedded-allocate-at-init.md) enforce one retirement authority and
// fixed-size observations; native removal pixels are checked separately.
#include "bw/render/render_scene.hpp"

#include <array>
#include <limits>
#include <stdexcept>
#include <type_traits>

#include "bw/scene/lighting/lighting_publication.hpp"
#include "camera_fixture.hpp"

namespace {
using bw::render::ActorPublisher;
using bw::render::RenderScene;
using bw::render::WorldColorDraw;
using bw::render::WorldGeometryStatus;
using bw::render::WorldLightingUpdate;
using bw::render::WorldModelHandle;
using bw::render::WorldOutputParameters;
using bw::render::WorldPoseUpdate;
using bw::render::WorldPresentation;
using bw::render::WorldShapePositionUpdate;
using bw::render::WorldTextureOffsetUpdate;
using bw::render::WorldViewUpdate;
using bw::render::WorldVisibilityUpdate;
void require(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}
template <class Operation>
void rejects(Operation operation, const char* message) {
  bool rejected = false;
  try {
    operation();
  } catch (const std::logic_error&) {
    rejected = true;
  }
  require(rejected, message);
}
template <class T>
concept RetiresScene = requires(T& owner) { owner.retire(); };
template <class T>
concept ReplacesDraws = requires(T& owner) {
  owner.replace_model_draws(WorldModelHandle{},
                            std::span<const WorldColorDraw>{});
};
template <class T>
concept PublishesLighting = requires(T& owner) {
  owner.update_world_lighting(std::span<const WorldLightingUpdate>{});
};
static_assert(!RetiresScene<ActorPublisher> && !ReplacesDraws<ActorPublisher> &&
              !PublishesLighting<ActorPublisher>);
static_assert(!std::is_copy_constructible_v<RenderScene> &&
              !std::is_move_constructible_v<RenderScene> &&
              !std::is_copy_constructible_v<ActorPublisher> &&
              !std::is_move_constructible_v<ActorPublisher>);
static_assert(!std::is_convertible_v<ActorPublisher*, WorldPresentation*>);

class Output final : public WorldPresentation {
 public:
  std::array<WorldLightingUpdate, 4> lighting{};
  std::size_t lighting_count{};
  bool reject_lighting{};
  WorldGeometryStatus geometry_status(WorldModelHandle) const override {
    return {};
  }
  void set_world_output_parameters(const WorldOutputParameters&) override {
    ++configurations;
  }
  void set_world_draws(std::span<const WorldColorDraw> draws) override {
    if (reject_registration) {
      throw std::invalid_argument("Fixture rejects registration");
    }
    ++registrations;
    selected = draws.empty() ? WorldModelHandle{} : draws.front().model;
  }
  void replace_world_model_draws(
      WorldModelHandle previous,
      std::span<const WorldColorDraw> draws) override {
    if (previous != selected) {
      throw std::invalid_argument("Fixture stale model");
    }
    selected = draws.empty() ? WorldModelHandle{} : draws.front().model;
    ++replacements;
  }
  void update_world_texture_offsets(
      std::span<const WorldTextureOffsetUpdate>) override {}
  void update_world_lighting(
      std::span<const WorldLightingUpdate> updates) override {
    if (reject_lighting) {
      throw std::invalid_argument("Fixture lighting rejection");
    }
    require(updates.size() <= lighting.size(), "Fixture packet capacity");
    std::ranges::copy(updates, lighting.begin());
    lighting_count = updates.size();
  }
  void update_world_poses(std::span<const WorldPoseUpdate>) override {
    ++poses;
  }
  void update_shape_positions(
      std::span<const WorldShapePositionUpdate>) override {
    ++shapes;
  }
  void update_world_visibility(
      std::span<const WorldVisibilityUpdate>) override {
    ++visibility;
  }
  void update_world_view(const WorldViewUpdate&) override {}
  void clear_world_draws() noexcept override {
    ++clears;
    selected = {};
  }
  WorldModelHandle selected;
  unsigned configurations{}, registrations{}, replacements{}, poses{}, shapes{},
      visibility{}, clears{};
  bool reject_registration{};
};
void admission_and_retirement() {
  Output output;
  {
    RenderScene scene(output);
    auto& actor = scene.actor_publisher();
    rejects([&] { RenderScene duplicate(output); },
            "Second scene owner must reject");
    rejects([&] { actor.update_poses({}); },
            "Preparing scene cannot accept actor poses");
    rejects([&] { actor.update_shapes({}); },
            "Preparing scene cannot accept actor shapes");
    scene.set_world_output_parameters({});
    const WorldColorDraw draw{
        .model = {1}, .parameters = {}, .state = {}, .joint_matrices = {}};
    output.reject_registration = true;
    rejects([&] { scene.register_draws(std::span(&draw, 1)); },
            "Failed registration preserves preparing");
    require(output.registrations == 0 && output.clears == 0,
            "No retirement of failed publication");
    output.reject_registration = false;
    scene.register_draws(std::span(&draw, 1));
    rejects([&] { scene.register_draws({}); }, "Registration happens once");
    actor.update_poses({});
    actor.update_shapes({});
    actor.update_visibility({});
    require(output.poses == 1 && output.shapes == 1 && output.visibility == 1,
            "Active publisher forwards only admitted mutations");
    std::array<WorldVisibilityUpdate, 2> visibility{};
    visibility.back().casts_shadow = false;
    rejects([&] { actor.update_visibility(visibility); },
            "Explicit false shadow remains scene policy");
    visibility.back().casts_shadow.reset();
    visibility.back().owner_opacity = 0;
    rejects([&] { actor.update_visibility(visibility); },
            "Explicit zero opacity remains scene policy");
    require(output.visibility == 1,
            "Late policy rejection precedes entire backend batch");
    scene.update_world_visibility(visibility);
    require(output.visibility == 2,
            "Scene owner retains shadow/opacity publication");
    const WorldColorDraw replacement{
        .model = {2}, .parameters = {}, .state = {}, .joint_matrices = {}};
    scene.replace_model_draws({1}, std::span(&replacement, 1));
    rejects([&] { scene.replace_model_draws({2}, {}); },
            "Removal must be named explicitly");
    scene.remove_model_draws({2});
    require(output.selected.value == 0 && output.replacements == 2,
            "Removal uses sole retained backend");
    scene.retire();
    scene.retire();
    rejects([&] { actor.update_poses({}); }, "Retired publisher rejects poses");
    rejects([&] { actor.update_shapes({}); },
            "Retired publisher rejects shapes");
    rejects([&] { actor.update_visibility({}); },
            "Retired publisher rejects visibility");
    rejects([&] { scene.set_world_output_parameters({}); },
            "Retired scene rejects output changes");
    require(output.clears == 1, "Retirement clears exactly once");
    RenderScene next(output);
    next.register_draws(std::span(&draw, 1));
    scene.retire();
    require(output.selected.value == 1 && output.clears == 1,
            "Old retired owner cannot clear a newly admitted scene");
  }
  require(output.clears == 2,
          "Later scene destructor retires only its own registration");
}
void construction_unwinding() {
  Output output;
  struct FailingOwner {
    RenderScene scene;
    explicit FailingOwner(Output& backend, bool register_first)
        : scene(backend) {
      if (register_first) scene.register_draws({});
      throw std::runtime_error("Later scene setup failed");
    }
  };
  for (const bool registered : {false, true}) {
    bool failed = false;
    try {
      FailingOwner failure(output, registered);
    } catch (const std::runtime_error&) {
      failed = true;
    }
    require(failed, "Fixture must throw during construction");
    // Successful construction proves the unwound owner released its claim.
    RenderScene next(output);
  }
  require(output.clears == 1,
          "Construction unwinding retires only registered submissions");
  output.reject_registration = true;
  bool rejected = false;
  try {
    RenderScene failed(output);
    failed.register_draws({});
  } catch (const std::invalid_argument&) {
    rejected = true;
  }
  require(rejected, "Initial backend registration failure must propagate");
  output.reject_registration = false;
  RenderScene recovered(output);
  recovered.register_draws({});
  require(output.clears == 1,
          "Rejected initial registration does not clear unrelated state");
}
// Prepared receiver membership transfers without sharing scene authority.
// This fixture checks public packets/lifetimes; existing sampler and native
// corpus checks independently cover source arithmetic and rendered material
// use.
void lighting_publication() {
  using bw::scene::LightingPublication;
  using bw::scene::ReceiverLightingPolicy;
  using bw::scene::ReceiverPublication;
  static_assert(!std::is_copy_constructible_v<LightingPublication> &&
                !std::is_move_constructible_v<LightingPublication>);
  static_assert(!std::is_copy_constructible_v<ReceiverPublication> &&
                std::is_nothrow_move_constructible_v<ReceiverPublication>);
  static_assert(!RetiresScene<bw::render::LightingPublisher> &&
                !ReplacesDraws<bw::render::LightingPublisher>);
  static_assert(!PublishesLighting<RenderScene>);
  auto domain =
      bw::memory::Domain::create(std::make_shared<camera_fixture::Backing>());
  Output output;
  {
    RenderScene render(output);
    bw::scene::SceneLightingParameters parameters;
    parameters.ambient = {.2F, .3F, .4F};
    // Minimal synthetic field with a constant fallback and no spatial zones.
    // Local-light membership then independently exposes model-transform input.
    bw::resources::Bytes field(160);
    camera_fixture::word(field, 0, 0x544c4853);
    camera_fixture::word(field, 4, 1);
    camera_fixture::word(field, 16, 32);
    camera_fixture::word(field, 20, 152);
    camera_fixture::scalar(field, 24, 1);
    camera_fixture::scalar(field, 152, 1);
    const std::array<bw::scene::SceneLightFieldSource, 1> fields{{{field, 1}}};
    const std::array<bw::scene::LocalLight, 1> lights{
        {{{3, 0, 0}, {1, 1, 1}, {2, 3, 4}, {1, 1, 0}}}};
    LightingPublication scene(
        {fields, lights, {.2F, .2F, 4, false}, parameters}, {},
        render.lighting_publisher(), domain);
    rejects(
        [&] {
          (void)scene.prepare_receiver(
              {.policy = ReceiverLightingPolicy::stage_ambient}, 2);
        },
        "Receiver registration requires its published view");
    const bw::math::Affine3<float> camera{
        {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}, {0, 0, -5}}};
    scene.set_view(
        {{0, 0, -5},
         bw::math::make_perspective_frustum(camera, {-1, 1, -1, 1, 1, 100})});
    auto receiver = scene.prepare_receiver(
        {.policy = ReceiverLightingPolicy::stage_ambient}, 2);
    std::array<WorldColorDraw, 2> draws{};
    draws[0].model = {1};
    draws[0].draw = 3;
    draws[0].instance = 4;
    draws[1].model = {1};
    draws[1].draw = 5;
    draws[1].instance = 6;
    receiver.add_draw(draws[0]);
    receiver.add_draw(draws[1]);
    rejects([&] { receiver.add_draw(draws[0]); }, "Receiver capacity is fixed");
    rejects([&] { receiver.publish(); }, "Unsealed receiver cannot publish");
    receiver.seal();
    rejects([&] { receiver.add_draw(draws[0]); },
            "Sealed membership is immutable");
    rejects([&] { receiver.publish(); },
            "Unregistered scene rejects publication");
    render.register_draws(draws);
    const auto prepared = domain->snapshot();
    receiver.publish();
    require(output.lighting_count == 2 && output.lighting[0].draw == 3 &&
                output.lighting[1].instance == 6 &&
                output.lighting[0].irradiance_coefficients ==
                    draws[0].parameters.irradiance_coefficients,
            "Prepared destination identities and initial coefficients survive");
    parameters.ambient = {.6F, .4F, .2F};
    scene.set_parameters(parameters);
    auto installed = std::move(receiver);
    installed.publish();
    require(output.lighting[0].irradiance_coefficients !=
                    draws[0].parameters.irradiance_coefficients &&
                output.lighting[0].irradiance_coefficients ==
                    output.lighting[1].irradiance_coefficients,
            "One new scene observation updates every bound receiver draw");
    require(domain->snapshot().live_bytes == prepared.live_bytes &&
                domain->snapshot().peak_live_bytes == prepared.peak_live_bytes,
            "Sealed publication uses only prepared domain storage");
    rejects([&] { installed.publish_model(camera); },
            "Static policy cannot be replaced with a model transform");
    output.reject_lighting = true;
    rejects([&] { installed.publish(); }, "Native rejection propagates");
    output.reject_lighting = false;
    installed.publish();
    auto model =
        scene.prepare_receiver({.policy = ReceiverLightingPolicy::spatial,
                                .model_center = {3, 0, 0},
                                .model_radius = .1F,
                                .field = {0}},
                               1);
    model.add_draw(draws[0]);
    model.seal();
    auto placed = scene.prepare_placed({0}, {3, 0, 0}, .1F, 1);
    placed.add_draw(draws[1]);
    placed.seal();
    model.publish();
    require(output.lighting[0].local_light_count == 1,
            "Model receiver consumes its own transformed center");
    const auto direct = output.lighting[0].local_lights[0];
    require(direct.position == bw::math::Vector<float, 4>{3, 0, 0, 1} &&
                direct.specular == bw::math::Vector<float, 4>{2, 3, 4, 1},
            "Receiver packing preserves source local-light components");
    auto transform = camera;
    transform[3] = {100, 0, 0};
    model.publish_model(transform);
    require(output.lighting[0].local_light_count == 0,
            "Changed model transform refreshes local-light admission");
    transform[0][0] = std::numeric_limits<float>::quiet_NaN();
    rejects([&] { model.publish_model(transform); },
            "Invalid model observation rejects before publication");
    model.publish();
    require(output.lighting[0].local_light_count == 0,
            "Rejected observation retains prior transform");
    placed.publish();
    require(output.lighting[0].local_light_count == 1 &&
                output.lighting[0].instance == 6,
            "Placed publication retains independent root and destination");
    rejects([&] { placed.publish_model(camera); },
            "Placed cache cannot silently switch to actor policy");
    render.retire();
    rejects([&] { installed.publish(); }, "Retired scene rejects old receiver");
    RenderScene next(output);
    next.register_draws(draws);
    rejects([&] { installed.publish(); },
            "Old receiver cannot publish to reentry");
  }
  require(domain->snapshot().live_bytes == 0,
          "Receiver storage retires with owners");
}
}  // namespace
void render_scene_contracts() {
  admission_and_retirement();
  construction_unwinding();
  lighting_publication();
}
