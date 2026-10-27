// Original shared-motion preparation and allocation-free sampling. Public
// ownership, failure, cost and both-phase I.11/R.14/MEM.9 contracts are in the
// header. Name lookup occurs only during preparation; caller pose scratch
// remains private until SkeletonPose validates/composes the complete batch.
#include "shared_motion_pose.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

#include "bw/math/quaternion.hpp"

namespace bw::game {
namespace {
void validate_names(std::span<const formats::ModelBoneName> names,
                    std::size_t bones) {
  if (names.size() != bones) {
    throw std::invalid_argument("Shared motion needs complete bone names");
  }
  for (std::size_t i = 0; i < names.size(); ++i) {
    const auto& name = names[i];
    if (name.name.empty() || name.bone >= bones) {
      throw std::invalid_argument("Invalid shared motion bone name");
    }
    for (std::size_t j = 0; j < i; ++j) {
      if (names[j].bone == name.bone || names[j].name == name.name) {
        throw std::invalid_argument("Duplicate shared motion bone identity");
      }
    }
  }
}
std::optional<std::size_t> find_bone(
    std::span<const formats::ModelBoneName> names, std::string_view name) {
  const auto found =
      std::ranges::find(names, name, &formats::ModelBoneName::name);
  if (found == names.end()) return std::nullopt;
  return found->bone;
}
bool finite(const formats::SkeletonTransform& value) {
  return math::all_finite(value.rotation) &&
         math::all_finite(value.translation) && math::all_finite(value.scale);
}
}  // namespace

SharedMotionPose SharedMotionPose::prepare(
    const formats::SkeletonDefinition& target,
    std::span<const formats::ModelBoneName> target_names,
    const formats::SkeletonDefinition& owner,
    std::span<const formats::ModelBoneName> owner_names,
    const std::shared_ptr<memory::Domain>& domain) {
  validate_names(target_names, target.bones().size());
  validate_names(owner_names, owner.bones().size());
  SharedMotionPose result(domain);
  result.owner_bones_ = owner.bones().size();
  result.hips_ = find_bone(owner_names, "HipsBone");
  const auto target_hips = find_bone(target_names, "HipsBone");
  float hips_scale = 1;
  if (result.hips_ && target_hips) {
    const float from = owner.bones().at(*result.hips_).absolute.translation[1];
    const float to = target.bones().at(*target_hips).absolute.translation[1];
    if (from != to) hips_scale = to / from;
  }
  if (!std::isfinite(hips_scale)) {
    throw std::invalid_argument("Nonfinite shared motion hips scale");
  }
  result.entries_.resize(target.bones().size());
  for (const auto& name : target_names) {
    auto& entry = result.entries_.at(name.bone);
    entry.bind = target.bones().at(name.bone).relative;
    entry.owner = find_bone(owner_names, name.name);
    if (!entry.owner) continue;
    const auto index = *entry.owner;
    const auto& from = owner.bones().at(index).relative;
    // Preserve the original mapper's owner-index lookup in the target table.
    // This is distinct from the name-matched target rotation index above.
    if (index >= target.bones().size()) {
      throw std::invalid_argument("Shared motion scale index exceeds target");
    }
    const auto& to = target.bones().at(index).relative;
    const float from_length = math::length(from.translation);
    entry.translation_scale = hips_scale;
    if (!result.hips_ || index > *result.hips_) {
      entry.translation_scale =
          from_length > 0 ? math::length(to.translation) / from_length : 0;
    }
    const auto inverse = math::quaternion_inverse_or_zero(
        from.rotation, std::numeric_limits<float>::epsilon());
    entry.rotation = math::quaternion_product(math::QuaternionProduct<float>{
        .left = entry.bind.rotation, .right = inverse});
    if (!std::isfinite(entry.translation_scale) ||
        !math::all_finite(entry.rotation)) {
      throw std::invalid_argument("Nonfinite shared motion correction");
    }
  }
  return result;
}

void SharedMotionPose::sample(const animation::PreparedClip& clip,
                              double sampled_seconds,
                              std::span<formats::SkeletonTransform> destination,
                              math::Vector3<float> hips_offset) const {
  if (destination.size() != entries_.size() || clip.bones() != owner_bones_ ||
      !std::isfinite(sampled_seconds) || sampled_seconds < 0 ||
      sampled_seconds > clip.duration_seconds() ||
      !math::all_finite(hips_offset)) {
    throw std::invalid_argument("Invalid shared motion sample request");
  }
  for (std::size_t bone = 0; bone < entries_.size(); ++bone) {
    const auto& entry = entries_.at(bone);
    auto local = entry.bind;
    if (entry.owner && (clip.channel_mask(*entry.owner) & 1U) != 0) {
      local = clip.local_transform(*entry.owner, sampled_seconds);
      local.translation =
          math::scale(local.translation, entry.translation_scale);
      local.rotation = math::quaternion_product(math::QuaternionProduct<float>{
          .left = entry.rotation, .right = local.rotation});
      // Original playback compares the target slot against its stored owner
      // hips index. Keep that receiver convention instead of inventing a move.
      if (hips_ && bone == *hips_) {
        local.translation = math::add(local.translation, hips_offset);
      }
    }
    if (!finite(local)) {
      throw std::overflow_error("Nonfinite retargeted local transform");
    }
    destination[bone] = local;
  }
}
}  // namespace bw::game
