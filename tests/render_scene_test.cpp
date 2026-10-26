// In-process scene lifecycle/capability tests, without GPU or game data. The
// fake backend observes calls/failure ordering, not material/render algorithms.
// Core I.1/E.6 and performance MEM.9 (guidelines/memory/
// MEM.9-embedded-allocate-at-init.md) enforce one retirement authority and
// fixed-size observations; native removal pixels are checked separately.
#include "bw/render/render_scene.hpp"

#include <array>
#include <stdexcept>
#include <type_traits>

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
  void update_world_lighting(std::span<const WorldLightingUpdate>) override {}
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
}  // namespace
void render_scene_contracts() {
  admission_and_retirement();
  construction_unwinding();
}
