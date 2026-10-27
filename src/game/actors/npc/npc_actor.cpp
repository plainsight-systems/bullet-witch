// Implement the instance contract in npc_actor.hpp. Only world delta advances
// playback; publication copies completed palettes without ticking a clock.
// I.11/E.6 and MEM.9 retain stable mission borrows and bounded instance
// buffers.
#include "npc_actor.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <stdexcept>

namespace bw::game {
namespace {
const formats::SkeletonDefinition& skeleton(const PreparedNpcModel& model) {
  const auto& value = model.resident.definition.get().skeleton();
  if (!value) throw std::runtime_error("NPC requires an authored skeleton");
  return *value;
}
animation::Matrix actor_model_transform(const SpawnCandidate& placement,
                                        float body_radius) {
  const float radians =
      placement.yaw_degrees * std::numbers::pi_v<float> / 180.F;
  formats::SkeletonTransform actor;
  actor.rotation = {0, std::sin(radians / 2), 0, std::cos(radians / 2)};
  actor.translation = placement.position;
  actor.translation[1] -= body_radius;
  actor.scale = {1, 1, 1};
  formats::SkeletonTransform basis;
  basis.rotation = {0, 1, 0, 0};
  basis.scale = {1, 1, 1};
  // Common actor update602b70 replaces the constructor offset with the
  // negative effective body radius. StateStart already placed the center.
  return animation::multiply(animation::transform_matrix(basis),
                             animation::transform_matrix(actor));
}
}  // namespace
NpcModelPose::NpcModelPose(const PreparedNpcModel& prepared,
                           const std::shared_ptr<memory::Domain>& domain)
    : prepared_(prepared),
      playback_(4, domain),
      pose_(skeleton(prepared), domain),
      locals_(memory::Allocator<formats::SkeletonTransform>(domain)),
      palettes_(memory::Allocator<Palette>(domain)) {
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
    if (!prepared.appearance.draws()[index].selected) continue;
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
  constexpr double kInitialBlendSeconds = .3;
  playback_.request({.clip = &prepared.motion.clip(),
                     .binding = prepared.motion.binding(),
                     .blend_seconds = kInitialBlendSeconds,
                     .fresh = true});
}
void NpcModelPose::advance(double seconds, const animation::Matrix& transform) {
  model_transform_ = math::transpose(transform);
  playback_.advance(seconds);
  const auto slot = playback_.current_slot();
  if (!slot) throw std::logic_error("NPC initial controller was lost");
  prepared_.motion.sample(playback_.state(*slot).seconds, locals_);
  locals_.at(root_) = {{0, 0, 0, 1}, {}, {1, 1, 1}};
  pose_.evaluate_locals(locals_, transform);
  const auto skin = pose_.skin_transforms();
  for (auto& palette : palettes_) {
    for (std::size_t joint = 0; joint < palette.count; ++joint) {
      const auto& source = skin[palette.bones.at(joint)];
      auto& destination = palette.matrices.at(joint);
      destination = math::transpose(source);
    }
  }
  playback_.finish_frame();
}
void NpcModelPose::publish(std::uint64_t instance,
                           render::ActorPublisher& output) const {
  for (const auto& palette : palettes_) {
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
      body_transform_(actor_model_transform(state.placement, body_radius)),
      weapon_transform_(animation::kIdentityMatrix) {
  if (identity.value == 0) throw std::invalid_argument("NPC identity is zero");
  if (weapon) {
    const auto& names = body.resident.definition.get().bone_names();
    const auto attachment =
        std::ranges::find(names, "RightWeapon", &formats::ModelBoneName::name);
    if (attachment != names.end()) attachment_ = attachment->bone;
    weapon_.emplace(*weapon, domain);
  }
  evaluate(0);
}
void NpcActor::evaluate(double seconds) {
  body_.advance(seconds, body_transform_);
  if (weapon_) {
    // Weapon activation and the common render conversion each apply a Y half
    // turn. Their product is identity before the completed attachment bone.
    weapon_transform_ =
        attachment_ ? body_.bones()[*attachment_] : body_transform_;
    weapon_->advance(seconds, weapon_transform_);
  }
}
void NpcActor::advance(double seconds, render::ActorPublisher& output) {
  evaluate(seconds);
  body_.publish(identity_.value, output);
  if (weapon_) weapon_->publish(identity_.value, output);
}
}  // namespace bw::game
