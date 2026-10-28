// Prepared Alicia binding; both-phase I.1 exposes immutable input poses and
// owned results; MEM.9 prepares all metadata/scratch before frame evaluation.
// Authored weapon targets remain independent of the corrected left arm.
#include "alicia_support_arm.hpp"

#include <algorithm>
#include <stdexcept>

#include "bw/math/two_bone.hpp"

namespace bw::game {
namespace {
constexpr std::size_t kHandTarget = 100, kElbowPole = 101;
animation::Matrix rotated_frame(const animation::Matrix& original,
                                const math::Vector<float, 4>& rotation,
                                const math::Vector3<float>& origin) {
  const auto delta = math::homogeneous_transform(
      math::TransformComponents<float>{rotation, {}, {1, 1, 1}});
  auto result = animation::multiply(original, delta);
  math::set_translation(result, origin);
  return result;
}
}  // namespace
AliciaSupportArm::AliciaSupportArm(
    SupportArmSource source, const std::shared_ptr<memory::Domain>& domain)
    : control_points_(std::move(source.weapon_control_points)),
      frames_(memory::Allocator<animation::Matrix>(domain)) {
  if (!control_points_) {
    throw std::invalid_argument("Support arm requires prepared weapon targets");
  }
  frames_.resize(control_points_->frames().size(), animation::kIdentityMatrix);
  const auto& body = source.body.get();
  const auto& body_skeleton = body.skeleton();
  if (!body_skeleton) {
    throw std::invalid_argument(
        "Support arm requires an authored body skeleton");
  }
  constexpr std::array<std::string_view, 3> names{"LeftArm", "LeftForeArm",
                                                  "LeftHand"};
  for (std::size_t index = 0; index < names.size(); ++index) {
    const auto found = std::ranges::find(body.bone_names(), names.at(index),
                                         &formats::ModelBoneName::name);
    if (found == body.bone_names().end()) {
      throw std::invalid_argument("Support arm lacks an authored chain bone");
    }
    bones_.at(index) = found->bone;
  }
  const auto& hierarchy = body_skeleton->bones();
  if (hierarchy.at(bones_[1]).parent != static_cast<std::int32_t>(bones_[0])) {
    throw std::invalid_argument("Support arm has an incompatible upper chain");
  }
  auto parent = hierarchy.at(bones_[2]).parent;
  std::size_t traversed = 0;
  while (parent >= 0 && parent != static_cast<std::int32_t>(bones_[1]) &&
         traversed++ < hierarchy.size()) {
    parent = hierarchy.at(static_cast<std::size_t>(parent)).parent;
  }
  if (parent != static_cast<std::int32_t>(bones_[1])) {
    throw std::invalid_argument("Support hand is not below the forearm");
  }
  for (const auto slot : {kHandTarget, kElbowPole}) {
    if (slot >= frames_.size() || !control_points_->frames().at(slot).present) {
      throw std::invalid_argument("Weapon lacks authored support-arm targets");
    }
  }
}
std::optional<std::array<animation::BoneWorldOverride, 3>>
AliciaSupportArm::evaluate(SupportArmPoses poses, float weight) {
  const auto& body = poses.body.get();
  const auto& weapon = poses.weapon.get();
  if (!std::isfinite(weight) || weight < 0 || weight > 1) {
    throw std::invalid_argument("Invalid support-arm presentation weight");
  }
  if (weight == 0) return std::nullopt;
  animation::evaluate_control_points(*control_points_, weapon.skin_transforms(),
                                     frames_);
  const auto pose = body.bone_transforms();
  const auto& upper = pose[bones_[0]];
  const auto& lower = pose[bones_[1]];
  auto hand = pose[bones_[2]];
  const auto root = math::translation(upper), joint = math::translation(lower),
             end = math::translation(hand);
  // Blend both the target and bend-plane guide from the current authored pose.
  // A target-only blend would swivel the elbow immediately at tiny weights.
  const auto target = math::add(
      end, math::scale(
               math::subtract(math::translation(frames_.at(kHandTarget)), end),
               weight));
  const auto pole = math::add(
      joint, math::scale(math::subtract(
                             math::translation(frames_.at(kElbowPole)), joint),
                         weight));
  const auto solved = math::solve_two_bone(math::TwoBoneRequest<float>{
      root, joint, end, target, pole, math::length(math::subtract(joint, root)),
      math::length(math::subtract(end, joint))});
  if (!solved) {
    throw std::runtime_error("Invalid support-arm pose geometry");
  }
  math::set_translation(hand, solved->end);
  const std::array overrides{
      animation::BoneWorldOverride{
          bones_[0], rotated_frame(upper, solved->upper_rotation, root)},
      animation::BoneWorldOverride{
          bones_[1],
          rotated_frame(lower, solved->lower_rotation, solved->joint)},
      animation::BoneWorldOverride{bones_[2], hand}};
  return overrides;
}
}  // namespace bw::game
