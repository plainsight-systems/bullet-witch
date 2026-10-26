// Debug separation: Core I.1 (#ri-explicit), R.3 (#rr-ptr), and performance
// TLM.1 (guidelines/telemetry/TLM.1-compile-telemetry-out-by-default.md),
// retrieved in design and implementation, keep preview capabilities bounded
// and compile out their owner/state/calls in ordinary production builds.
// Implement the owner contract in the matching header. Design I.1/C.9 keeps
// authoritative state private; implementation E.6 (#re-raii) preserves cleanup
// on failed preparation. Both-phase MEM.9
// (guidelines/memory/MEM.9-embedded-allocate-at-init.md) keeps retained buffers
// fixed during admitted frames. Existing source algorithms and ordering remain.
#include "mission_simulation.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

#include "animation_preview.hpp"
#include "bw/math/vector.hpp"
#include "game/mission/loading/mission_loading.hpp"
#include "game/world/presentation/stage_presentation.hpp"
#include "placed_collision.hpp"
namespace bw::game {
namespace {
constexpr float kTriggerThreshold = .8F;
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
MissionSimulation::MissionSimulation(
    const world::WorldClock& clock, const MissionControl& control,
    const content::StageDefinition& stage,
    const formats::CustomizationDefinition& customization,
    StageBackgroundSource background,
    const std::optional<PlayerActorSource>& player,
    const std::shared_ptr<memory::Domain>& domain,
    const content::LoadProgress& progress,
    const std::optional<render::ViewPose>& camera_pose,
    const std::optional<ScriptedPlayerPose>& inspection_placement)
    : clock_(clock), control_(control), memory_(domain), collision_(domain) {
  if (!player && !inspection_placement) {
    throw std::invalid_argument(
        "Playerless inspection requires an explicit placement");
  }
  const auto camera_settings =
      formats::PlayerCameraSettings::parse_initial(customization);
  register_stage_collision(stage, collision_);
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
        prepared.register_instance(body_world, collision_);
      }
    }
  }
  automatic_targeting_camera_ = camera_settings.automatic_targeting_camera;
  collision_.prepare_character_queries();
  const auto placement =
      player ? player->placement : inspection_placement.value();
  auto camera =
      make_initial_player_camera(placement, camera_settings, collision_);
  if (camera_pose) camera.pose = *camera_pose;
  fixed_view_ = camera;
  if (player) {
    for (unsigned index = 0; index < weapon_settings_.size(); ++index) {
      weapon_settings_.at(index) =
          WeaponPresentationSettings::parse(customization, index);
    }
    const auto weapon_name = player->weapon.name;
    if (weapon_name.size() != 3 || !weapon_name.starts_with("wa") ||
        weapon_name[2] < '0' || weapon_name[2] > '3') {
      throw std::invalid_argument("Unknown admitted player weapon class");
    }
    weapon_class_ = static_cast<unsigned>(weapon_name[2] - '0');
    player_.emplace(*player, domain, progress);
    if (!camera_pose) {
      controller_.emplace(*player_, collision_,
                          PlayerBodySettings::parse(customization), placement,
                          targeting_);
      camera_.emplace(camera_settings, placement, collision_);
      camera_->set_far_distance(camera.perspective.far_plane);
    }
  }
  audio_listener_ = {camera.pose.eye, camera.pose.forward, camera.pose.up};
}
AliciaActionUpdate MissionSimulation::begin_player_scripted_jump(
    const AcceptedScriptedJump& request) {
  if (!controller_) {
    throw std::logic_error("Scripted jump requires a physical player owner");
  }
  return controller_->begin_scripted_jump(request);
}
void MissionSimulation::bind_audio(const PreparedCharacterAudio& prepared,
                                   audio::CueOutput& output,
                                   SourceRandom& random) {
  if (audio_output_ != nullptr || !player_) {
    throw std::logic_error("Stage audio requires an unbound physical player");
  }
  player_->bind_audio({prepared, output, collision_, random}, memory_);
  output.set_listener(audio_listener_);
  player_->set_audio_listener(audio_listener_);
  audio_paused_ = control_.audio_paused();
  output.pause(audio_paused_);
  audio_output_ = &output;
}
void MissionSimulation::synchronize_pause() {
  const bool paused = control_.audio_paused();
  if (paused == audio_paused_) return;
  if (audio_output_ != nullptr) audio_output_->pause(paused);
  audio_paused_ = paused;
}
bool MissionSimulation::advance(const world::WorldFrame& frame,
                                StageTraversalInput input,
                                render::ActorPublisher& output) {
  if (!frame.belongs_to(clock_) || frame.sequence() <= last_frame_) {
    throw std::invalid_argument("Foreign or repeated world frame");
  }
  last_frame_ = frame.sequence();
  synchronize_pause();
  if (!frame.admission().simulation) return false;
  const double delta_seconds = frame.delta_seconds();
  // Input restriction publishes a neutral event; it does not skip actor work.
  // Accepted scripted observations retain their distinct admission contract.
  if (!frame.admission().hardware_input) {
    input.movement = {};
    input.look = {};
    input.presentation = {};
  }
  if (controller_ && camera_) {
    if (!player_) {
      throw std::logic_error("Traversal controller has no player owner");
    }
    auto& actor = player_.value();
    auto& controller = controller_.value();
    auto& camera = camera_.value();
    constexpr float kFireTriggerThreshold = .8F;
    const auto& view = camera.view().pose;
    if (audio_output_ != nullptr) {
      audio_listener_ = {view.eye, view.forward, view.up};
      audio_output_->set_listener(audio_listener_);
      actor.set_audio_listener(audio_listener_);
    }
    input.action.aim.anchor = math::orthonormal_basis(view.forward, view.up);
    math::set_translation(input.action.aim.anchor, view.eye);
    const auto* targeting_upper =
        actor.animation_state(CharacterMotionBank::upper_body);
    constexpr std::size_t kFirstTargetingUpper = 0x1d0;
    const bool fire_targeting =
        input.presentation.fire_trigger > kFireTriggerThreshold &&
        (targeting_upper == nullptr ||
         targeting_upper->binding >= kFirstTargetingUpper);
    const bool ordinary_shooter = controller.accepts_shooter_input();
    if (controller.action_owner().updates_shooter()) {
      targeting_.advance(
          static_cast<float>(delta_seconds),
          ordinary_shooter && input.presentation.aim_mode_pressed,
          ordinary_shooter && fire_targeting, automatic_targeting_camera_);
    }
    present_input(input.presentation, input.movement);
    // Source input settings belong to the input owner. Both camera look and
    // raised turn consume the same copied sensitivity/inversion observation.
    if (input.look.right != 0) {
      input.action.aim.signed_turn_input = raised_turn_input(
          input.look.right, static_cast<std::uint8_t>(input.look.turn_speed),
          false);
    }
    input.action.aim.weapon_class = weapon_class_;
    input.action.aim.weapon_movement_scale =
        weapon_settings_.at(weapon_class_).movement_scale;
    input.action.aim.accepted_refresh =
        input.action.aim.accepted_refresh || targeting_.active();
    double consumed = 0;
#if BW_DEVELOPER_VIEWS
    if (preview_ != nullptr && preview_->active()) {
      consumed = preview_->advance(input.movement, camera.yaw_radians(),
                                   delta_seconds, input.action);
    } else
#endif
    {
      consumed = controller.advance(input.movement, camera.yaw_radians(),
                                    delta_seconds, output, input.action);
    }
    camera.set_context(targeting_.camera_targeted()
                           ? PlayerCameraContext::targeting
                           : PlayerCameraContext::ordinary,
                       controller.state().stance == AliciaStance::crouched);
#if BW_DEVELOPER_VIEWS
    const double camera_seconds =
        preview_ != nullptr && preview_->active() ? delta_seconds : consumed;
#else
    const double camera_seconds = consumed;
#endif
    camera.advance(controller.state().pose, input.look, camera_seconds);
  } else if (player_) {
    player_->advance(delta_seconds, output);
  }
  return true;
}
void MissionSimulation::change_costume(MissionLoading& loading,
                                       unsigned costume,
                                       StagePresentation& presentation) {
  if (!paused() || !player_ || !controller_ || !camera_) {
    throw std::logic_error("Costume change requires paused player traversal");
  }
  auto& actor = player_.value();
  if (loading.current_player_costume() == costume) return;
  loading.prepare_player_costume(costume);
  try {
    auto source = actor.source();
    source.body = loading.candidate_player();
    PlayerActor candidate(source, actor, memory_);
    presentation.replace_appearance(candidate, false, camera_->view());
    // Everything that can fail precedes publication. Retire old instance data
    // before its mission-owned geometry/definitions, keeping controller
    // address.
    actor.adopt_appearance(std::move(candidate));
    loading.commit_player_costume();
  } catch (...) {
    loading.discard_player_costume();
    throw;
  }
}
void MissionSimulation::replace_weapon(MissionLoading& loading,
                                       unsigned weapon_class,
                                       bool animate_switch,
                                       StagePresentation& presentation) {
  if ((!animate_switch && !paused()) || !player_ || !controller_ || !camera_) {
    throw std::logic_error(
        "Weapon selection requires admitted traversal; menu replacement "
        "requires pause");
  }
  auto& actor = player_.value();
  auto& controller = controller_.value();
  if (weapon_class >= weapon_settings_.size()) {
    throw std::out_of_range("Unknown weapon presentation class");
  }
  if (loading.current_weapon_class() == weapon_class) return;
  if (controller.state().phase != PlayerMovementPhase::grounded ||
      !controller.state().contact.grounded) {
    throw std::logic_error("Land before changing weapon class");
  }
  if (animate_switch &&
      (
#if BW_DEVELOPER_VIEWS
          (preview_ != nullptr && preview_->blocks_shooter()) ||
#endif
          controller.action_owner().blocks_locomotion())) {
    throw std::logic_error("Finish the active action before switching weapons");
  }
  const auto* body = actor.animation_state(CharacterMotionBank::body);
  if (body == nullptr) {
    throw std::logic_error("Weapon switch has no body motion");
  }
  const auto plan = accepted_weapon_change(
      controller.state().stance, static_cast<std::uint32_t>(body->binding));
  if (animate_switch &&
      !actor.body_animations().motions().alias(plan.requests[0].slot).clip) {
    throw std::logic_error("Authored switch motion is absent");
  }
  loading.prepare_weapon_class(weapon_class);
  try {
    auto source = actor.source();
    source.weapon = loading.candidate_weapon();
    source.weapon_support = loading.candidate_weapon_support();
    PlayerActor candidate(source, actor, memory_,
                          animate_switch ? PlayerAppearanceChange::live_weapon
                                         : PlayerAppearanceChange::weapon,
                          animate_switch && controller.state().weapon_raised);
    presentation.replace_appearance(candidate, true, camera_->view());
    // Publication is the only fallible commit step. Stable physical ownership
    // survives; incompatible action clocks retire before old weapon resources.
    const auto physical = controller.state();
    if (!animate_switch) controller.restore_review_state(physical);
    actor.adopt_appearance(std::move(candidate));
    loading.commit_weapon_class();
    weapon_class_ = weapon_class;
    if (animate_switch) {
      // 62ed80 changes selected weapon before dispatching the upper request.
      // Body clocks and controller ownership survive that publication.
      (void)actor.apply_action_plan(plan);
      action_notice_ = "Weapon switch animation; no inventory change";
      return;
    }
#if BW_DEVELOPER_VIEWS
    if (preview_ != nullptr) {
      preview_->clear_after_equipment();
    } else
#endif
    {
      targeting_.select_for_review(false);
    }
    action_notice_ = {};
  } catch (...) {
    loading.discard_weapon_class();
    throw;
  }
}
void MissionSimulation::bind_action_resources(PlayerActionResources resources) {
  if (action_resources_) {
    throw std::logic_error("Player action resources are already bound");
  }
  action_resources_ = resources;
}
void MissionSimulation::present_input(PlayerPresentationInput input,
                                      PlayerMovementInput movement) {
  if (!player_ || !controller_ || !camera_) return;
  auto& actor = player_.value();
  auto& controller = controller_.value();
  const auto& camera = camera_.value();
  if (!std::isfinite(input.fire_trigger) || input.fire_trigger < 0 ||
      input.fire_trigger > 1) {
    throw std::invalid_argument("Invalid presentation trigger");
  }
#if BW_DEVELOPER_VIEWS
  if (preview_ != nullptr && preview_->blocks_shooter()) return;
#endif
  if (!controller.accepts_shooter_input()) {
    return;
  }
  const auto* body = actor.animation_state(CharacterMotionBank::body);
  if (body == nullptr) return;
  const bool upper =
      actor.animation_state(CharacterMotionBank::upper_body) != nullptr;
  const auto slot = static_cast<std::uint32_t>(body->binding);
  if (input.attack_pressed && controller.state().contact.grounded) {
    const auto& pose = camera.view().pose;
    const auto basis = math::orthonormal_basis(pose.forward, pose.up);
    const auto& orientation = controller.state().orientation;
    const auto direction =
        process_player_movement(movement.direction).direction;
    const auto variant = select_close_attack_variant(
        {direction[0], 0, direction[1]}, basis,
        {orientation[2][0], orientation[2][1], orientation[2][2]});
    (void)controller.action_owner().begin_close_attack(variant);
    action_notice_ = "Melee animation; world hit receiver not yet admitted";
    return;
  }
  if (upper || !action_resources_) return;
  auto& resources = *action_resources_;
  const auto recharge = [&] {
    const auto result = resources.try_reload();
    if (result == ResourceActionResult::committed) {
      (void)actor.apply_action_plan(
          accepted_reload({.stance = controller.state().stance,
                           .body_slot = slot,
                           .weapon_class = weapon_class_}));
      action_notice_ = "Weapon recharged from magic";
    } else if (result == ResourceActionResult::insufficient_magic) {
      action_notice_ = "Not enough magic to recharge";
    } else if (result == ResourceActionResult::magazine_full &&
               input.reload_pressed) {
      action_notice_ = "Weapon is fully charged";
    }
  };
  if (input.reload_pressed) {
    recharge();
  } else if (input.fire_trigger > kTriggerThreshold &&
             controller.state().weapon_raised) {
    const auto shot = resources.try_fire();
    if (shot.status == ShotStatus::accepted) {
      (void)actor.apply_action_plan(
          accepted_shot({.stance = controller.state().stance,
                         .body_slot = slot,
                         .weapon_class = weapon_class_,
                         .grounded = controller.state().contact.grounded}));
      action_notice_ =
          "Ammunition consumed; projectile receiver not yet admitted";
    } else if (shot.status == ShotStatus::empty) {
      // The source caller attempts recharge on empty, including a catch-up
      // call that consumed its final rounds before returning that status.
      recharge();
    }
  }
}
void MissionSimulation::set_camera_far(float distance) {
  if (camera_) camera_->set_far_distance(distance);
  fixed_view_.perspective.far_plane = distance;
}
}  // namespace bw::game
