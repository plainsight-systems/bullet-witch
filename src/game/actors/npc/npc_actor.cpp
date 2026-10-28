// Implement the instance contract in npc_actor.hpp. Only world delta advances
// playback; publication copies completed palettes without ticking a clock.
// I.11/E.6 and MEM.9 retain stable mission borrows and bounded instance
// buffers.
#include "npc_actor.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>
#include <numbers>
#include <stdexcept>

#include "bw/content/formats/binary_reader.hpp"
#include "bw/math/euler.hpp"
#include "bw/math/quaternion.hpp"
#include "game/actors/animation/action_motion_request.hpp"
#include "game/actors/animation/combat_pose.hpp"
#include "game/actors/animation/root_motion.hpp"
#include "game/actors/animation/weapon_motion_policy.hpp"
#include "game/actors/character_body_radius.hpp"
#include "game/combat/health_damage.hpp"

namespace bw::game {
namespace {
template <class T>
T& required_motion(std::optional<T>& owner) {
  if (!owner) throw std::logic_error("NPC motion bank is not admitted");
  return *owner;
}
template <class T>
const T& required_motion(const std::optional<T>& owner) {
  if (!owner) throw std::logic_error("NPC motion bank is not admitted");
  return *owner;
}

constexpr float kHalfTurnDegrees = 180.F;
const formats::SkeletonDefinition& skeleton(const PreparedNpcModel& model) {
  const auto& value = model.resident.definition.get().skeleton();
  if (!value) throw std::runtime_error("NPC requires an authored skeleton");
  return *value;
}
animation::Matrix placement_orientation(const SpawnCandidate& placement) {
  return math::euler_xyz_transform<float>(
             {{0,
               placement.yaw_degrees * std::numbers::pi_v<float> /
                   kHalfTurnDegrees,
               0},
              {}})
      .forward;
}
animation::Matrix actor_model_transform(const SpawnCandidate& placement,
                                        float model_offset_y,
                                        const animation::Matrix& orientation) {
  auto actor = orientation;
  auto position = placement.position;
  position[1] += model_offset_y;
  math::set_translation(actor, position);
  constexpr animation::Matrix half_turn{
      {{-1, 0, 0, 0}, {0, 1, 0, 0}, {0, 0, -1, 0}, {0, 0, 0, 1}}};
  return animation::multiply(half_turn, actor);
}
}  // namespace
PreparedNpcShapes::PreparedNpcShapes(
    const formats::ModelDefinition& source,
    const content::CharacterSupport& support,
    std::span<const std::string_view> names,
    const std::shared_ptr<memory::Domain>& domain)
    : model(source, domain), expressions(support, model, names, domain) {}
struct NpcModelPose::ShapeState {
  ShapeState(const PreparedNpcShapes& prepared,
             const std::shared_ptr<memory::Domain>& domain)
      : prepared(prepared),
        pose(prepared.model, domain),
        face(prepared.expressions, domain) {}
  void advance(double seconds) {
    face.advance(seconds);
    pose.evaluate(face.weights());
  }
  std::reference_wrapper<const PreparedNpcShapes> prepared;
  animation::ShapePose pose;
  FacialAnimation face;
};
FacialAdmission NpcModelPose::facial(FacialMotionCommand command) noexcept {
  return shapes_ ? shapes_->face.command(command, true)
                 : FacialAdmission::missing_expression;
}
void NpcModelPose::reset_facial() noexcept {
  if (shapes_) shapes_->face.reset_motion_banks();
}
NpcModelPose::NpcModelPose(const PreparedNpcModel& prepared,
                           const std::shared_ptr<memory::Domain>& domain)
    : prepared_(prepared),
      playback_(4, domain),
      pose_(skeleton(prepared), domain),
      release_(skeleton(prepared).bones().size(), domain),
      locals_(memory::Allocator<formats::SkeletonTransform>(domain)),
      palettes_(memory::Allocator<Palette>(domain)),
      visibility_(prepared.appearance.groups()) {
  if (prepared.shapes) {
    shapes_ = std::allocate_shared<ShapeState>(
        memory::Allocator<ShapeState>(domain), *prepared.shapes, domain);
  }
  const auto& bones = skeleton(prepared).bones();
  locals_.reserve(bones.size());
  for (const auto& bone : bones) locals_.push_back(bone.relative);
  const auto root =
      std::ranges::find(bones, -1, &formats::SkeletonBone::parent);
  if (root == bones.end()) throw std::runtime_error("NPC has no skeleton root");
  root_ = static_cast<std::size_t>(root - bones.begin());
  const auto& model = prepared.resident.definition.get();
  palettes_.reserve(model.draws().size());
  for (std::size_t index = 0; index < model.draws().size(); ++index) {
    Palette palette{.draw = index};
    std::array<bool, kPaletteCapacity> assigned{};
    const auto& material =
        model.materials().at(model.draws().at(index).material);
    constexpr std::uint32_t kSkinned = 0x10;
    if ((material.shader_selector[0] & kSkinned) == 0) {
      palettes_.push_back(palette);
      continue;
    }
    for (const auto& binding : material.matrices) {
      constexpr std::uint32_t kJointSemantic = 0x4a494e54;
      if (binding.semantic != kJointSemantic ||
          binding.destination_matrix >= palette.bones.size() ||
          binding.source_matrix >= bones.size()) {
        throw std::runtime_error("NPC draw has an unresolved joint binding");
      }
      palette.bones.at(binding.destination_matrix) = binding.source_matrix;
      assigned.at(binding.destination_matrix) = true;
      palette.count =
          std::max(palette.count,
                   static_cast<std::size_t>(binding.destination_matrix) + 1);
    }
    if (palette.count == 0 ||
        !std::ranges::all_of(std::span(assigned).first(palette.count),
                             [](bool value) { return value; })) {
      throw std::runtime_error("NPC draw has an incomplete skin palette");
    }
    palettes_.push_back(palette);
  }
  if (prepared.animations) {
    constexpr std::size_t kMotionOwners = 4, kUpperOwners = 2,
                          kAdditiveOwners = 1;
    ordinary_.emplace(*prepared.animations, events_.at(0), kMotionOwners,
                      domain);
    upper_.emplace(*prepared.animations, events_.at(1), kUpperOwners, domain);
    additive_.emplace(*prepared.animations, events_.at(2), kAdditiveOwners,
                      domain);
    if (prepared.resident.name == "wg1") {
      constexpr std::array<std::string_view, 7> names{
          "defaultmotion", "stand_l",   "shoot_l",    "stand_reload",
          "shoot_reload",  "shoot_end", "shoot_after"};
      for (std::size_t slot = 0; slot < names.size(); ++slot) {
        weapon_slots_.at(slot) =
            prepared.animations->motions().find(names.at(slot));
      }
    }
    const bool selected = prepared.resident.name == "wg1"
                              ? request_weapon_motion("defaultmotion", true)
                              : request_motion(1, true);
    if (!selected) {
      throw std::runtime_error("Missing NPC catalog initial motion");
    }
    return;
  }
  constexpr double kInitialBlendSeconds = .3;
  playback_.request({.clip = &prepared.motion.clip(),
                     .binding = prepared.motion.binding(),
                     .blend_seconds = kInitialBlendSeconds,
                     .fresh = true});
}
void NpcModelPose::advance(double seconds, const animation::Matrix& transform) {
  advance_motion(seconds);
  evaluate_pose(seconds, transform);
}
void NpcModelPose::advance_motion(double seconds) {
  if (!std::isfinite(seconds) || seconds < 0) {
    throw std::invalid_argument("Invalid NPC motion interval");
  }
  if (shapes_) shapes_->advance(seconds);
  if (ordinary_) {
    required_motion(ordinary_).advance(seconds);
    required_motion(upper_).advance(seconds);
    required_motion(additive_).advance(seconds);
    (void)required_motion(upper_).retire_completed_owner();
    aim_blend_.advance(static_cast<float>(seconds), aim_active_);
    aim_active_ = false;
    pending_root_ = animation::kIdentityMatrix;
    if (const auto* current = required_motion(ordinary_).current()) {
      pending_root_ = current->clip->root_relative_transform(
          {current->previous_seconds, current->seconds,
           current->unwrapped_seconds - current->previous_unwrapped_seconds});
    }
  } else {
    playback_.advance(seconds);
  }
}
void NpcModelPose::evaluate_pose(double seconds,
                                 const animation::Matrix& transform) {
  model_transform_ = math::transpose(transform);
  if (ordinary_) {
    constexpr std::size_t kOrdinaryLayers = 8;
    std::array<animation::PoseLayer, kOrdinaryLayers> layers{};
    std::size_t count = 0;
    for (const auto& layer : required_motion(ordinary_).layers()) {
      layers.at(count++) = layer;
    }
    for (const auto& layer : required_motion(upper_).layers()) {
      layers.at(count++) = layer;
    }
    std::ranges::sort(std::span(layers).first(count),
                      [](const auto& left, const auto& right) {
                        return left.channel < right.channel;
                      });
    constexpr float kUpperReleaseDecayPerSecond = 3;
    pose_.evaluate_layers(
        {std::span(layers).first(count), required_motion(additive_).layers()},
        transform, animation::RootPosePolicy::identity,
        source_modifiers_
            ? std::span(&direction_reference_, 1)
            : std::span<const animation::BoneLocalBasisOverride>{},
        {&release_, seconds, kUpperReleaseDecayPerSecond},
        {std::span(aim_chain_).first(aim_chain_count_), aim_direction_,
         aim_blend_.weight()});
    update_palettes();
    required_motion(ordinary_).dispatch_events();
    required_motion(upper_).dispatch_events();
    required_motion(additive_).dispatch_events();
    required_motion(ordinary_).finish_frame();
    required_motion(upper_).finish_frame();
    required_motion(additive_).finish_frame();
    return;
  }
  const auto slot = playback_.current_slot();
  if (!slot) throw std::logic_error("NPC initial controller was lost");
  prepared_.motion.sample(playback_.state(*slot).seconds, locals_);
  locals_.at(root_) = {{0, 0, 0, 1}, {}, {1, 1, 1}};
  pose_.evaluate_locals(locals_, transform);
  update_palettes();
  playback_.finish_frame();
}
void NpcModelPose::enable_source_modifiers() {
  if (source_modifiers_) {
    throw std::logic_error("NPC source modifiers already enabled");
  }
  if (!ordinary_) throw std::logic_error("NPC motion catalog is not prepared");
  const auto& names = prepared_.resident.definition.get().bone_names();
  const auto root = std::ranges::find(names, "Waist_direction",
                                      &formats::ModelBoneName::name);
  if (root == names.end()) {
    throw std::runtime_error("NPC movement reference bone is absent");
  }
  direction_reference_ = animation::BoneLocalBasisOverride{root->bone};
  constexpr std::array<std::string_view, kAimChainCapacity> chain{
      "Hipsbone", "Spine", "Spine1", "Spine2", "Spine3", "Shoots_direction"};
  for (const auto name : chain) {
    const auto bone =
        std::ranges::find(names, name, &formats::ModelBoneName::name);
    if (bone != names.end()) aim_chain_.at(aim_chain_count_++) = bone->bone;
  }
  if (aim_chain_count_ < 2) {
    throw std::runtime_error("NPC authored aim chain is absent");
  }
  source_modifiers_ = true;
}
math::Vector3<float> NpcModelPose::root_displacement() const {
  if (!source_modifiers_) {
    throw std::logic_error("NPC source modifiers are not admitted");
  }
  const auto* current = required_motion(ordinary_).current();
  if (!current) return {};
  const auto basis = original_root_motion_basis(current->clip->local_transform(
      direction_reference_.bone, current->seconds));
  return math::transform_direction<float>(math::translation(pending_root_),
                                          basis);
}
animation::Matrix NpcModelPose::root_rotation() const {
  if (!source_modifiers_) {
    throw std::logic_error("NPC source modifiers are not admitted");
  }
  auto delta = pending_root_;
  delta[3] = {0, 0, 0, 1};
  return delta;
}
float NpcModelPose::horizontal_root_distance() const {
  return std::hypot(pending_root_[3][0], pending_root_[3][2]);
}
animation::Matrix NpcModelPose::authored_facing_reference() const {
  const auto* current = required_motion(ordinary_).current();
  return current ? animation::transform_matrix(current->clip->local_transform(
                       direction_reference_.bone, current->seconds))
                 : animation::kIdentityMatrix;
}
bool NpcModelPose::upper_active() const {
  return upper_ && upper_->current() != nullptr;
}
void NpcModelPose::aim(math::Vector3<float> direction, bool active) {
  if (!source_modifiers_) {
    throw std::logic_error("NPC aim modifiers are not admitted");
  }
  constexpr float kUnitTolerance = .001F;
  if (!math::all_finite(direction) ||
      std::abs(math::dot(direction, direction) - 1) > kUnitTolerance) {
    throw std::invalid_argument("NPC aim direction is not unit length");
  }
  if (active) aim_direction_ = direction;
  aim_active_ = active;
}
bool NpcModelPose::has_motion(std::uint32_t slot) const {
  return prepared_.animations &&
         slot < prepared_.animations->motions().slot_count() &&
         prepared_.animations->motions().alias(slot).clip.has_value();
}
bool NpcModelPose::request_motion(std::uint32_t slot, bool fresh,
                                  double blend_seconds) {
  if (!prepared_.animations ||
      slot >= prepared_.animations->motions().slot_count()) {
    return false;
  }
  const auto& catalog = *prepared_.animations;
  const auto clip = catalog.motions().alias(slot).clip;
  if (!clip) return false;
  const auto semantics = catalog.pose_semantics(*clip);
  if (semantics == MotionPoseSemantics::unknown) {
    throw std::invalid_argument("NPC action has unclassified pose semantics");
  }
  auto& owner = semantics == MotionPoseSemantics::additive
                    ? required_motion(additive_)
                    : required_motion(ordinary_);
  owner.request(
      {.clip = *clip,
       .binding = slot,
       .parameter = catalog.parameter_for_clip(*clip),
       .blend_seconds = blend_seconds,
       .fresh = fresh,
       .remove_on_completion = semantics == MotionPoseSemantics::additive});
  return true;
}
bool NpcModelPose::request_weapon_motion(std::string_view name, bool fresh) {
  if (!prepared_.animations) return false;
  const auto& catalog = *prepared_.animations;
  const auto clip = catalog.motions().find(name);
  if (!clip) return false;
  if (catalog.pose_semantics(*clip) != MotionPoseSemantics::ordinary) {
    throw std::invalid_argument("NPC weapon has unclassified pose semantics");
  }
  required_motion(ordinary_).request(
      {.clip = *clip,
       .binding = *clip,
       .parameter = catalog.parameter_for_clip(*clip),
       .blend_seconds = kSourceActionBlendSeconds,
       .fresh = fresh});
  return true;
}
bool NpcModelPose::request_action(const ActionMotionRequest& request) {
  if (!prepared_.animations) return false;
  const auto& catalog = *prepared_.animations;
  auto* owner = &required_motion(ordinary_);
  bool parameters = false;
  switch (request.bank) {
    case ActionMotionBank::body:
      parameters = true;
      break;
    case ActionMotionBank::upper_body:
      owner = &required_motion(upper_);
      parameters = true;
      break;
    case ActionMotionBank::additive:
      owner = &required_motion(additive_);
      break;
    case ActionMotionBank::weapon:
      break;
    default:
      throw std::invalid_argument("Unknown NPC action bank");
  }
  const auto clip = request.bank == ActionMotionBank::weapon
                        ? weapon_slots_.at(request.slot)
                        : catalog.motions().alias(request.slot).clip;
  const auto resolved = resolve_action_motion(catalog, clip, request,
                                              owner->current(), parameters);
  if (!resolved) return false;
  const auto expected = request.bank == ActionMotionBank::additive
                            ? MotionPoseSemantics::additive
                            : MotionPoseSemantics::ordinary;
  if (catalog.pose_semantics(resolved->clip) != expected) {
    throw std::invalid_argument(
        "NPC action bank and source representation differ: slot=" +
        std::to_string(request.slot) +
        " clip=" + std::to_string(resolved->clip) + " bank=" +
        std::to_string(static_cast<unsigned>(request.bank)) + " semantics=" +
        std::to_string(
            static_cast<unsigned>(catalog.pose_semantics(resolved->clip))));
  }
  owner->request(*resolved);
  owner->set_rate(resolved->rate);
  return true;
}
void NpcModelPose::clear_upper() {
  if (upper_) required_motion(upper_).clear();
}
const animation::ClipPlaybackState* NpcModelPose::current_motion() const {
  if (ordinary_) return required_motion(ordinary_).current();
  const auto slot = playback_.current_slot();
  return slot ? &playback_.state(*slot) : nullptr;
}
bool NpcModelPose::cancellable() const {
  return ordinary_ && required_motion(ordinary_).cancellable();
}
void NpcModelPose::update_palettes() {
  const auto skin = pose_.skin_transforms();
  for (auto& palette : palettes_) {
    for (std::size_t joint = 0; joint < palette.count; ++joint) {
      const auto& source = skin[palette.bones.at(joint)];
      auto& destination = palette.matrices.at(joint);
      destination = math::transpose(source);
    }
  }
}
animation::Matrix NpcModelPose::publish_physics(CharacterPhysics& physics) {
  const auto transform = physics.publish(pose_);
  model_transform_ = math::transpose(transform);
  update_palettes();
  return transform;
}
void NpcModelPose::visibility(std::size_t bank,
                              VisibilityMotionCommand command) {
  visibility_.command(bank, command.target, command.enabled);
}
void NpcModelPose::reset_visibility(std::size_t bank) {
  visibility_.reset(bank);
}
void NpcModelPose::set_visible_group(std::uint32_t group,
                                     bool enabled) noexcept {
  visibility_.set_group(group, enabled);
}
bool NpcModelPose::draw_visible(std::size_t draw) const {
  return visibility_.visible(
      prepared_.resident.definition.get().draws().at(draw).group);
}
void NpcActor::visibility(std::size_t bank, VisibilityMotionCommand command) {
  constexpr std::int32_t kNoOpListener = 1, kWeaponListener = 1000;
  if (command.target == kNoOpListener) return;
  if (command.target == kWeaponListener) {
    if (weapon_) {
      weapon_->set_visible_group(1, command.enabled);
      if (!command.enabled) weapon_->set_visible_group(2, false);
    }
    return;
  }
  body_.visibility(bank, command);
}
void NpcActor::reset_visibility(std::size_t bank) {
  // Reset invokes registered listeners even when no positive event occurred.
  if (weapon_) weapon_->set_visible_group(1, true);
  body_.reset_visibility(bank);
}
void NpcModelPose::publish(std::uint64_t instance,
                           render::ActorPublisher& output) const {
  if (shapes_) {
    for (std::size_t buffer = 0;
         buffer < shapes_->prepared.get().model.buffers().size(); ++buffer) {
      const render::WorldShapePositionUpdate update{
          prepared_.resident.handle, buffer, shapes_->pose.positions(buffer),
          instance};
      output.update_shapes(std::span(&update, 1));
    }
  }
  for (const auto& palette : palettes_) {
    const render::WorldVisibilityUpdate visibility{
        prepared_.resident.handle, palette.draw, draw_visible(palette.draw),
        std::nullopt, instance};
    output.update_visibility(std::span(&visibility, 1));
    render::WorldPoseUpdate update{
        prepared_.resident.handle, palette.draw,
        std::span(palette.matrices).first(palette.count), instance};
    if (palette.count == 0) update.model_transform = model_transform_;
    output.update_poses(std::span(&update, 1));
  }
}
std::span<const render::WorldJointMatrix> NpcModelPose::palette(
    std::size_t draw) const {
  const auto found = std::ranges::find(palettes_, draw, &Palette::draw);
  if (found == palettes_.end()) throw std::out_of_range("Unselected NPC draw");
  return std::span(found->matrices).first(found->count);
}
NpcActor::NpcActor(PopulationIdentity identity, PopulationActorState state,
                   const PreparedNpcModel& body, const PreparedNpcModel* weapon,
                   float body_radius,
                   const std::shared_ptr<memory::Domain>& domain)
    : identity_(identity),
      state_(state),
      body_(body, domain),
      orientation_(placement_orientation(state.placement)),
      body_transform_(actor_model_transform(
          state.placement, -character_body_radius(body_radius, state.life > 0),
          orientation_)),
      weapon_transform_(animation::kIdentityMatrix),
      radius_(body_radius),
      model_offset_y_(-character_body_radius(body_radius, state.life > 0)) {
  if (identity.value == 0) throw std::invalid_argument("NPC identity is zero");
  constexpr std::uint64_t kGroupRadiusOffset = 0x40;
  const resources::detail::Reader header{
      body.resident.definition.get().header()};
  model_radius_ = std::bit_cast<float>(header.u32(kGroupRadiusOffset));
  if (!std::isfinite(model_radius_) || model_radius_ < 0) {
    throw std::invalid_argument("NPC original model radius is invalid");
  }
  if (weapon) {
    const auto& names = body.resident.definition.get().bone_names();
    const auto attachment =
        std::ranges::find(names, "RightWeapon", &formats::ModelBoneName::name);
    if (attachment != names.end()) attachment_ = attachment->bone;
    weapon_.emplace(*weapon, domain);
  }
  evaluate(0);
}
math::Affine3<float> NpcActor::attachment_transform(std::int32_t slot) const {
  const auto frame = original_attachment_pose(
      body_.prepared().attachments.get(), body_.pose().skin_transforms(),
      math::affine_part(body_transform_), slot);
  if (frame) return *frame;
  // Source actor physical frame retains identity orientation; placement and
  // Runner turns change the separate model/anchor, and only move this origin.
  auto fallback = math::affine_part(animation::kIdentityMatrix);
  fallback[3] = state_.placement.position;
  return fallback;
}
math::Vector3<float> NpcActor::attachment_position(std::int32_t slot) const {
  return attachment_transform(slot)[3];
}
math::Vector3<float> NpcActor::motion_attachment_position(
    std::int32_t slot) const {
  const auto model = math::affine_part(body_transform_);
  const auto frame = slot > 0 ? original_attachment_pose(
                                    body_.prepared().attachments.get(),
                                    body_.pose().skin_transforms(), model, slot)
                              : std::nullopt;
  return frame ? (*frame)[3] : model[3];
}
math::Vector3<float> NpcActor::aim_position() const {
  constexpr std::int32_t eye_attachment = 54;
  return original_actor_aim_point(state_.placement.position,
                                  attachment_position(eye_attachment)[1],
                                  model_radius_);
}
std::uint8_t NpcActor::movement_category() const {
  constexpr std::int32_t eye_attachment = 54;
  NpcMovementObservation observation{state_.placement.position[1],
                                     attachment_position(eye_attachment)[1],
                                     model_radius_};
  const auto* motion = body_.current_motion();
  if (reacting_ && !sustained_.active() && motion) {
    observation.damage_motion = static_cast<std::uint32_t>(motion->binding);
  }
  return original_npc_movement_category(observation);
}
void NpcActor::evaluate(double seconds) {
  if (collision_ && seconds > 0) {
    const auto dt = static_cast<float>(seconds);
    constexpr float gravity = 9.800000190734863F;
    if (reacting_ || dying_ || melee_blast_.active()) flight_.interrupt();
    if (flight_.phase() == ScriptedJumpPhase::landing) {
      const auto* current = body_.current_motion();
      apply_flight_transition(
          flight_.advance_landing(current && current->completed,
                                  body_.cancellable(), !contact_.grounded));
    }
    const bool generic_flight = flight_.phase() == ScriptedJumpPhase::flight;
    if (melee_blast_.phase() == MeleeBlastPhase::start) {
      const auto heading =
          melee_blast_.facing(math::affine_part(body_transform_)[2], dt);
      state_.placement.yaw_degrees = std::atan2(-heading[0], -heading[2]) *
                                     kHalfTurnDegrees /
                                     std::numbers::pi_v<float>;
      orientation_ = placement_orientation(state_.placement);
    }
    body_.advance_motion(seconds);
    const auto* motion = body_.current_motion();
    if (melee_blast_.phase() == MeleeBlastPhase::start && motion &&
        motion->completed) {
      const auto heading = melee_blast_.settled_facing();
      state_.placement.yaw_degrees = std::atan2(-heading[0], -heading[2]) *
                                     kHalfTurnDegrees /
                                     std::numbers::pi_v<float>;
      orientation_ = placement_orientation(state_.placement);
    }
    const bool ordinary_ground = !reacting_ && !dying_ && !action_reacting_ &&
                                 !contact_action_ && !melee_blast_.active() &&
                                 flight_.phase() == ScriptedJumpPhase::inactive;
    math::Vector3<float> displacement{};
    if (ordinary_ground) {
      // Grounded Runner steers horizontal authored distance along the selected
      // travel direction, then tilts it onto qualified support. It does not
      // integrate turn-clip root rotation; model facing already owns that turn.
      displacement =
          math::scale(locomotion_direction_, body_.horizontal_root_distance());
      if (contact_.grounded) {
        const auto slope = math::rotation_between<float>(
            {{0, 1, 0}, contact_.ground_normal, {-1, 0, 0}});
        displacement = math::transform_direction<float>(
            displacement,
            math::homogeneous_transform(
                math::TransformComponents<float>{slope, {}, {1, 1, 1}}));
      }
    } else {
      // Generic action Runner consumes the complete relative root transform.
      displacement = math::transform_direction<float>(body_.root_displacement(),
                                                      orientation_);
      orientation_ = animation::multiply(orientation_, body_.root_rotation());
      state_.placement.yaw_degrees =
          std::atan2(orientation_[2][0], orientation_[2][2]) *
          kHalfTurnDegrees / std::numbers::pi_v<float>;
    }
    if (melee_blast_.active()) {
      displacement = melee_blast_.displacement(displacement, dt,
                                               motion && motion->completed);
    }
    if (generic_flight) {
      const auto step = scripted_flight_step(flight_velocity_, dt, gravity);
      displacement = step.displacement;
      flight_velocity_ = step.velocity;
    }
    const auto previous = state_.placement.position;
    const float contact_radius =
        character_body_radius(radius_, state_.life > 0);
    contact_ = collision_->move_character(
        {.previous_position = previous,
         .proposed_position = math::add(previous, displacement),
         .radius = contact_radius,
         .cylinder_height = height_ - 2 * contact_radius,
         .delta_seconds = dt,
         .vertical_velocity =
             generic_flight ? flight_velocity_[1] : displacement[1] / dt,
         .previously_grounded = !generic_flight && contact_.grounded,
         .mode = generic_flight ||
                         (melee_blast_.active() &&
                          melee_blast_.phase() != MeleeBlastPhase::landing)
                     ? scene::CharacterContactMode::airborne
                     : scene::CharacterContactMode::grounded});
    state_.placement.position = contact_.position;
    velocity_ = math::divide(math::subtract(contact_.position, previous), dt);
    body_transform_ =
        actor_model_transform(state_.placement, model_offset_y_, orientation_);
    body_.evaluate_pose(seconds, body_transform_);
    if (generic_flight) {
      if (contact_.grounded && flight_velocity_[1] <= 0) {
        apply_flight_transition(flight_.landed(flight_velocity_[1], gravity));
      } else {
        flight_velocity_ = scripted_flight_feedback(
            flight_velocity_, math::subtract(contact_.position, previous), dt);
      }
    } else if (contact_action_ && !contact_.grounded &&
               !melee_blast_.active() && !reacting_ && !dying_) {
      // Grounded contact states request a fresh default Runner StateJump.
      // Its constructor starts with zero velocity, not pose-root momentum.
      contact_action_ = false;
      constexpr std::uint32_t contact_flags = 0xA;
      set_hit_query_flags(contact_flags, false);
      apply_flight_transition(flight_.begin_flight({}));
    }
    if (melee_blast_.active()) {
      constexpr float kDeathFrame = 5;
      const auto update = melee_blast_.finish(
          {.motion_completed = motion && motion->completed,
           .grounded = contact_.grounded,
           .crossed_death_frame =
               motion && animation::crossed_frame(*motion, kDeathFrame)});
      if (update.motion && !body_.request_action(*update.motion)) {
        throw std::runtime_error("Original melee blast motion is unavailable");
      }
      constexpr std::uint32_t kBlastQueryFlags = 0xA;
      if (update.release_hit_flags) {
        set_hit_query_flags(kBlastQueryFlags, false);
      }
      blast_handoff_ = update.physical_handoff;
    }
  } else {
    body_.advance(seconds, body_transform_);
  }
  if (weapon_) {
    const auto* current = weapon_->current_motion();
    if (current && current->completed && !current->clip->repeating()) {
      // Original gun completion requests its retained rest slot; a missing
      // authored rest leaves the prior controller installed.
      (void)weapon_->request_action({.bank = ActionMotionBank::weapon,
                                     .slot = weapon_rest_slot_,
                                     .fresh = true});
    }
    // Weapon activation and the common render conversion each apply a Y half
    // turn. Their product is identity before the completed attachment bone.
    weapon_transform_ =
        attachment_ ? body_.bones()[*attachment_] : body_transform_;
    weapon_->advance(seconds, weapon_transform_);
  }
}
void NpcActor::advance(double seconds, render::ActorPublisher& output) {
  // Common actor publication updates its retained model offset before the
  // death receiver. Small dead bodies use twice their authored radius. This
  // same derived radius also governs custom support before physical handoff.
  // The immutable skeletal shape recipe remains unchanged. I.1 and MEM.9
  // were reread for the shared copied-value policy.
  model_offset_y_ = -character_body_radius(radius_, state_.life > 0);
  if (!physical_ || !physical_->physical()) {
    body_transform_ =
        actor_model_transform(state_.placement, model_offset_y_, orientation_);
  }
  // Death retention continues after animation hands its pose to physics.
  // Only the world retires the resulting identity (I.11/E.4, MEM.9).
  if (dying_) death_lifetime_.advance(static_cast<float>(seconds));
  if (physical_ && physical_->physical()) return;
  advance_reaction(seconds);
  if (physical_ && physical_->physical()) return;
  evaluate(seconds);
  if (hit_) hit_->publish(body_.pose());
  if (physical_) physical_->follow_animation(body_.pose());
  // Current-frame support loss transfers the just-completed pose, without
  // waiting for the death clip or advancing the reaction owner twice.
  if (dying_ && !melee_blast_.active() && !contact_.grounded) handoff_death();
  if (blast_handoff_) {
    if (!physical_ || !combat_random_) {
      throw std::logic_error("Melee blast lost its admitted physical owner");
    }
    constexpr float kBaseImpulse = .1F, kHorizontal = .5F, kUp = .75F;
    const auto magnitude = kBaseImpulse + combat_random_->unit();
    const auto angle = combat_random_->unit() * 2 * std::numbers::pi_v<float>;
    const math::Vector3<float> impulse{std::sin(angle) * kHorizontal, kUp,
                                       std::cos(angle) * kHorizontal};
    (void)physical_->request_impulse(
        {impulse, state_.placement.position, magnitude, 0, -1});
    physical_->handoff();
    blast_handoff_ = false;
  }
  body_.publish(identity_.value, output);
  if (weapon_) weapon_->publish(identity_.value, output);
}
bool NpcActor::corpse_retirement_due(
    const DeathRetirementObservation& observation) const {
  return dying_ && death_lifetime_.retirement_due(observation);
}
void NpcActor::bind_locomotion(const scene::StaticCollision& collision,
                               float radius, float height) {
  if (collision_ || !std::isfinite(radius) || !std::isfinite(height) ||
      radius <= 0 || height < 2 * radius) {
    throw std::invalid_argument("Invalid NPC locomotion admission");
  }
  body_.enable_source_modifiers();
  collision_ = &collision;
  radius_ = radius;
  height_ = height;
}
void NpcActor::locomotion(const NpcLocomotionSelection& selected,
                          const animation::Matrix& anchor, bool ready,
                          float seconds) {
  if (reacting_ || dying_ || action_reacting_ || contact_action_ ||
      flight_.phase() != ScriptedJumpPhase::inactive) {
    return;
  }
  if (!body_.request_action(selected.motion)) {
    throw std::runtime_error("NPC locomotion motion is unavailable");
  }
  locomotion_direction_ = selected.movement_direction;
  update_facing(selected.retained_heading, anchor, ready, seconds);
}
void NpcActor::reaction_facing(const math::Vector3<float>& retained_heading,
                               const animation::Matrix& anchor, bool ready,
                               float seconds) {
  if (action_reacting_ && reaction_updates_facing_ && !reacting_ && !dying_) {
    update_facing(retained_heading, anchor, ready, seconds);
  }
}
void NpcActor::update_facing(const math::Vector3<float>& retained_heading,
                             const animation::Matrix& anchor, bool ready,
                             float seconds) {
  // Grounded and named-reaction states share the source model-facing receiver.
  const auto facing = raised_facing_step(
      {forward(),
       retained_heading,
       {anchor[2][0], anchor[2][1], anchor[2][2]},
       ready ? body_.authored_facing_reference() : animation::kIdentityMatrix,
       seconds,
       ready});
  orientation_ = math::orthonormal_basis(facing, math::Vector3<float>{0, 1, 0});
  state_.placement.yaw_degrees = std::atan2(facing[0], facing[2]) *
                                 kHalfTurnDegrees / std::numbers::pi_v<float>;
  body_transform_ =
      actor_model_transform(state_.placement, model_offset_y_, orientation_);
}
math::Vector3<float> NpcActor::forward() const {
  return {orientation_[2][0], orientation_[2][1], orientation_[2][2]};
}
void NpcActor::face(const math::Vector3<float>& direction) {
  if (!math::all_finite(direction) ||
      (direction[0] == 0 && direction[2] == 0)) {
    throw std::invalid_argument("Invalid NPC facing direction");
  }
  if (dying_ || physical()) return;
  state_.placement.yaw_degrees = std::atan2(direction[0], direction[2]) *
                                 kHalfTurnDegrees / std::numbers::pi_v<float>;
  orientation_ = placement_orientation(state_.placement);
  body_transform_ =
      actor_model_transform(state_.placement, model_offset_y_, orientation_);
}
void NpcActor::aim(const animation::Matrix& anchor, bool active) {
  body_.aim(active ? aim_posture_target({.anchor = anchor,
                                         .model = orientation_,
                                         .camera_present = false})
                   : math::Vector3<float>{0, 0, 1},
            active);
}
bool NpcActor::has_reaction(std::uint32_t slot) const {
  return body_.has_motion(slot);
}
bool NpcActor::react(std::uint32_t slot, double blend, bool update_facing) {
  if (reacting_ || dying_ ||
      !body_.request_action(
          {.slot = slot, .blend_seconds = blend, .fresh = true})) {
    return false;
  }
  flight_.interrupt();
  action_reacting_ = true;
  reaction_updates_facing_ = update_facing;
  return true;
}
void NpcActor::clear_reaction() { action_reacting_ = false; }
bool NpcActor::motion_looping() const {
  const auto* current = body_.current_motion();
  return current && current->clip->repeating();
}
bool NpcActor::motion_completed() const {
  const auto* current = body_.current_motion();
  return current && current->completed;
}
bool NpcActor::begin_contact_action(const ActionMotionRequest& request) {
  if (reacting_ || dying_ || physical() ||
      flight_.phase() != ScriptedJumpPhase::inactive) {
    return false;
  }
  contact_action_ = true;
  return body_.request_action(request);
}
void NpcActor::end_contact_action() noexcept { contact_action_ = false; }
void NpcActor::apply_flight_transition(
    const ScriptedJumpTransition& transition) {
  require_plan(transition.motion);
  if (transition.flight_velocity) {
    flight_velocity_ = *transition.flight_velocity;
  }
}
void NpcActor::require_plan(const ActionMotionPlan& plan) {
  const auto admission = apply_action_plan(plan);
  if (admission.unavailable != 0) {
    std::string detail = "Required NPC action motion unavailable:";
    for (const auto& request : plan.motions()) {
      detail += " bank=" + std::to_string(static_cast<unsigned>(request.bank)) +
                " slot=" + std::to_string(request.slot);
    }
    throw std::runtime_error(detail);
  }
}
void NpcActor::set_hit_query_flags(std::uint32_t mask, bool enabled) {
  if (hit_) hit_->query_flags(mask, enabled);
}
void NpcActor::accepted_shot() {
  const auto* current = body_.current_motion();
  if (!current ||
      current->binding > std::numeric_limits<std::uint32_t>::max()) {
    throw std::logic_error("NPC firing has no source body controller");
  }
  // Shooter accepts the shot before requesting optional upper/weapon/recoil
  // slots. Native model admission returns false for an absent authored slot.
  (void)apply_action_plan(accepted_weapon_shot_motion(
      {static_cast<std::uint32_t>(current->binding), 0, false, false}));
}
HitReception NpcActor::receive_ordinary_hit(
    const CombatImpact& hit, const ActorDamageDefinition& definition,
    SourceRandom& random) {
  if (hit.target != identity_.value || hit.source == identity_.value) {
    return {};
  }
  if (!physical_) return {};
  if (state_.life <= 0) {
    return {physical_->receive_corpse_shot(hit.direction, hit.position), {}};
  }
  const float magnitude = definition.magnitude(hit.body, hit.damage);
  const auto health =
      apply_health_damage({state_.life, state_.life, state_.maximum_life,
                           magnitude, 1, HealthDamageMode::ordinary});
  if (!health.accepted) return {};
  const auto category = movement_category();
  const auto response =
      ordinary_hit_response(definition,
                            {.health = health.current,
                             .maximum_health = state_.maximum_life,
                             .magnitude = magnitude,
                             .body = hit.body,
                             .movement_category = category,
                             .damage_reaction_active = reacting_,
                             .sustained_family = true},
                            random);
  const auto matrix = math::affine_part(body_transform_);
  const ReactionDirection direction{hit.direction, math::scale(matrix[2], -1.F),
                                    matrix[2]};
  // Geometry is a copied provider observation. Only this actor commits its
  // health and chooses the source action owner; physics never writes health.
  state_.life = health.current;
  combat_random_ = &random;
  switch (response) {
    case HitReaction::none:
      break;
    case HitReaction::additive: {
      constexpr std::uint32_t kOrdinaryAdditiveDamage = 0x394;
      ActionMotionPlan plan;
      plan.count = 1;
      plan.requests[0] = {.bank = ActionMotionBank::additive,
                          .slot = kOrdinaryAdditiveDamage,
                          .fresh = true};
      require_plan(plan);
      break;
    }
    case HitReaction::ordinary:
      action_reacting_ = false;
      sustained_.interrupt();
      require_plan(damage_.begin({direction, matrix[0]}));
      reacting_ = true;
      break;
    case HitReaction::sustained: {
      action_reacting_ = false;
      const AcceptedSustainedDamageReaction accepted{
          direction, definition.sustained_recovery_seconds, random.unit()};
      const auto result = sustained_.active() ? sustained_.impact(accepted)
                                              : sustained_.begin(accepted);
      damage_.interrupt();
      require_plan(result.motion);
      reacting_ = true;
      break;
    }
    case HitReaction::death_motion:
      action_reacting_ = false;
      damage_.interrupt();
      sustained_.interrupt();
      body_.clear_upper();
      require_plan(accepted_death(
          {direction, category != 0, definition.nonpositive_death_weights,
           definition.positive_death_weights, random.unit()}));
      reacting_ = false;
      dying_ = true;
      break;
    case HitReaction::death_physics:
      action_reacting_ = false;
      damage_.interrupt();
      sustained_.interrupt();
      flight_.interrupt();
      body_.clear_upper();
      reacting_ = false;
      dying_ = true;
      // StateDie default impulse radius is zero, but its random-direction
      // helper still consumes one source draw before the upward handoff.
      (void)random.unit();
      handoff_death();
      break;
  }
  return {true, ordinary_hit_voice(response), !health.alive};
}
bool NpcActor::receive_melee(const MeleeImpact& hit,
                             const ActorDamageDefinition& definition,
                             SourceRandom& random) {
  if (hit.target != identity_.value || hit.source == identity_.value ||
      !physical_) {
    return false;
  }
  if (state_.life <= 0) {
    return physical_->receive_corpse_melee(hit.center, hit.contact,
                                           hit.outer_radius);
  }
  const auto magnitude = melee_magnitude(hit);
  const auto health =
      apply_health_damage({state_.life, state_.life, state_.maximum_life,
                           magnitude, 1, HealthDamageMode::ordinary});
  const auto name = body_.prepared().resident.name;
  const bool physical_family = name.starts_with("ea") ||
                               name.starts_with("hm") ||
                               name.starts_with("hw") || name.starts_with("ee");
  const auto response =
      melee_reaction(definition,
                     {.health = health.current,
                      .magnitude = magnitude,
                      .reaction_blocked = melee_blast_.active(),
                      .crouched = movement_category() != 0,
                      .physical_death_family = physical_family},
                     health.current == 0 ? random.unit() : 0.F);
  const auto matrix = math::affine_part(body_transform_);
  const ReactionDirection direction{melee_direction(hit, matrix[3]),
                                    math::scale(matrix[2], -1.F), matrix[2]};
  state_.life = health.current;
  combat_random_ = &random;
  switch (response) {
    case MeleeReaction::none:
      break;
    case MeleeReaction::ordinary:
    case MeleeReaction::heavy:
    case MeleeReaction::crouched: {
      const auto category =
          response == MeleeReaction::heavy      ? DamageImpactClass::heavy
          : response == MeleeReaction::crouched ? DamageImpactClass::crouched
                                                : DamageImpactClass::ordinary;
      sustained_.interrupt();
      require_plan(damage_.begin({direction, matrix[0], category}));
      reacting_ = true;
      break;
    }
    case MeleeReaction::death_motion:
      damage_.interrupt();
      sustained_.interrupt();
      body_.clear_upper();
      require_plan(accepted_melee_death(
          {direction, definition.melee_death_weights, random.unit()}));
      reacting_ = false;
      dying_ = true;
      break;
    case MeleeReaction::death_blast_start: {
      auto pending = melee_blast_;
      const auto motion = pending.begin(direction.incoming, body_transform_);
      if (!body_.request_action(motion)) {
        throw std::runtime_error("Original melee blast start is unavailable");
      }
      damage_.interrupt();
      sustained_.interrupt();
      body_.clear_upper();
      melee_blast_ = pending;
      contact_action_ = false;
      action_reacting_ = false;
      constexpr std::uint32_t kBlastQueryFlags = 0xA;
      set_hit_query_flags(kBlastQueryFlags, true);
      reacting_ = false;
      dying_ = true;
      break;
    }
  }
  return health.accepted;
}
void NpcActor::handoff_death() {
  if (!physical_) throw std::logic_error("NPC death has no physical owner");
  if (physical_->physical()) return;
  (void)physical_->request_impulse(
      {{0, 1, 0}, state_.placement.position, 1.F, 1, -1});
  physical_->handoff();
}
void NpcActor::advance_reaction(double seconds) {
  if (melee_blast_.active()) return;
  const auto* current = body_.current_motion();
  if (!current) return;
  if (dying_) {
    if (death_requires_dynamic_handoff({current->completed,
                                        current->clip->repeating(), false,
                                        !contact_.grounded})) {
      handoff_death();
    }
    return;
  }
  if (sustained_.active()) {
    const bool crossed = animation::crossed_frame(*current, 10);
    if (!combat_random_) throw std::logic_error("NPC sustain lost source RNG");
    const auto update =
        sustained_.advance({static_cast<float>(seconds), crossed,
                            crossed ? combat_random_->unit() : 0.F});
    require_plan(update.motion);
    reacting_ = update.active;
  } else if (reacting_) {
    const auto update =
        damage_.advance({current->completed, current->clip->repeating(),
                         body_.cancellable(), false, false});
    require_plan(update.motion);
    reacting_ = update.active;
  }
}
ActionPresentationAdmission NpcActor::apply_action_plan(
    const ActionMotionPlan& plan) {
  if (plan.count > plan.requests.size() ||
      (plan.weapon_rest_slot && *plan.weapon_rest_slot != 1 &&
       *plan.weapon_rest_slot != 2)) {
    throw std::invalid_argument("Invalid NPC action presentation plan");
  }
  if (plan.clear_upper) body_.clear_upper();
  if (plan.weapon_rest_slot) weapon_rest_slot_ = *plan.weapon_rest_slot;
  ActionPresentationAdmission result;
  for (const auto& request : plan.motions()) {
    const bool accepted = request.bank == ActionMotionBank::weapon
                              ? weapon_ && weapon_->request_action(request)
                              : body_.request_action(request);
    if (accepted) {
      ++result.admitted;
    } else {
      ++result.unavailable;
    }
  }
  return result;
}
void NpcActor::bind_combat(const physics::Resources& resources,
                           const physics::Queries& queries,
                           const physics::Mutations& mutations,
                           std::shared_ptr<const ActorHitGeometry> geometry,
                           const CharacterPhysicsRecipe& recipe,
                           physics::Material material, NpcCombatLayers layers,
                           const std::shared_ptr<memory::Domain>& domain) {
  if (hit_ || physical_) throw std::logic_error("NPC combat already admitted");
  const auto& model = body_.prepared().resident.definition.get();
  if (!model.skeleton()) throw std::logic_error("NPC physics needs a skeleton");
  try {
    hit_.emplace(resources, mutations, std::move(geometry),
                 body_.prepared().appearance.groups(), body_.pose(),
                 identity_.value, layers.hit, domain);
    physical_.emplace(
        resources, queries, mutations, recipe, *model.skeleton(),
        model.bone_names(), body_.pose(),
        CharacterPhysicsCollision{material, identity_.value, layers.body},
        domain);
  } catch (...) {
    physical_.reset();
    hit_.reset();
    throw;
  }
}
void NpcActor::finish_physics(render::ActorPublisher& output) {
  if (!physical_ || !physical_->physical()) return;
  body_transform_ = body_.publish_physics(*physical_);
  // Physics owns the completed model root. Recover the game actor origin from
  // that root and its retained publication offset; do not feed a stale spawn
  // position into corpse retirement or later world observations.
  state_.placement.position = {body_transform_[3][0],
                               body_transform_[3][1] - model_offset_y_,
                               body_transform_[3][2]};
  if (hit_) hit_->publish(body_.pose());
  body_.publish(identity_.value, output);
  if (weapon_) {
    weapon_transform_ =
        attachment_ ? body_.bones()[*attachment_] : body_transform_;
    weapon_->advance(0, weapon_transform_);
    weapon_->publish(identity_.value, output);
  }
}
}  // namespace bw::game
