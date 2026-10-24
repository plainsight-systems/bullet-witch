// Ordered mask and additive evaluation uses construction-time pose scratch,
// preserving the source channel/chain order before modifiers and hierarchy.
// Both-phase I.1/E.4/MEM.9/CDSA.32 contracts are in pose.hpp and
// alicia-animation-layers.md; no archive access or allocation during
// evaluation. Explicit local/world override batches preserve pose-space
// invariants and publish only complete finite hierarchies. Both-phase
// I.1/E.4/MEM.9 contracts are in pose.hpp; prepared index scratch avoids
// per-frame search/allocation. Shared math retains each caller's
// scalar/accumulator precision; tolerance and failure policy remain here.
// Both-phase I.9/MEM.9 contracts are in bw/math/vector.hpp and matrix.hpp.
// Prepare immutable animation keys and evaluate retained instance transforms.
// Contracts, row-vector conventions and both-phase I.11/CDSA.32/CACHE.4 are in
// pose.hpp. Implementation SL.con.3 validates source extents and channel counts
// before conversion. Runtime searches contiguous typed keys, not archive bytes.
// Pending pose buffers publish together only after finite matrix validation;
// failure retains the previous pose. No allocation, locks or shared mutation
// occurs in evaluate(). Original trigonometric approximation bit parity is not
// asserted; skeletal-animation-baseline.md records the numeric boundary.
#include "bw/animation/pose.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>
#include <stdexcept>

#include "bw/math/half.hpp"
#include "bw/math/quaternion.hpp"

namespace bw::animation {
namespace {
constexpr std::size_t kWordBytes = 4;
constexpr std::uint32_t kFloatEncoding = 0x46000000;
std::uint32_t word(std::span<const std::uint8_t> bytes, std::size_t offset) {
  if (offset > bytes.size() || kWordBytes > bytes.size() - offset) {
    throw resources::Error(resources::ErrorCode::malformed,
                           "Animation scalar exceeds source region");
  }
  const auto value = bytes.subspan(offset, kWordBytes);
  constexpr unsigned kByte1Shift = 8, kByte2Shift = 16, kByte3Shift = 24;
  return static_cast<std::uint32_t>(value[0]) |
         (static_cast<std::uint32_t>(value[1]) << kByte1Shift) |
         (static_cast<std::uint32_t>(value[2]) << kByte2Shift) |
         (static_cast<std::uint32_t>(value[3]) << kByte3Shift);
}
float scalar(std::span<const std::uint8_t> bytes, std::size_t offset) {
  const auto value = std::bit_cast<float>(word(bytes, offset));
  if (!std::isfinite(value)) {
    throw resources::Error(resources::ErrorCode::malformed,
                           "Animation scalar must be finite");
  }
  return value;
}
// Preserve the original finite exponent-31 conversion, including signed zero
// and subnormals. This is deliberately not an IEEE infinity/NaN conversion.
float half_scalar(std::span<const std::uint8_t> bytes, std::size_t offset) {
  constexpr std::size_t kHalfBytes = 2;
  if (offset > bytes.size() || kHalfBytes > bytes.size() - offset) {
    throw resources::Error(resources::ErrorCode::malformed,
                           "Animation half exceeds source region");
  }
  const auto data = bytes.subspan(offset, kHalfBytes);
  const auto bits = static_cast<std::uint32_t>(data[0]) |
                    (static_cast<std::uint32_t>(data[1]) << 8U);
  return math::finite_half_to_float(static_cast<std::uint16_t>(bits));
}
// Keep the source shortest-path sign and near-parallel branch. Exact key
// values bypass this path, including the authored quaternion magnitude.
std::array<float, 4> rotation_blend(const std::array<float, 4>& left,
                                    const std::array<float, 4>& right,
                                    float alpha) {
  constexpr float kLinearThreshold = 0.9999899864F;
  return math::quaternion_slerp(
      left, right, math::SphericalBlend<float>{alpha, kLinearThreshold});
}
Matrix from_affine(const std::array<std::array<float, 3>, 4>& source) {
  auto result = kIdentityMatrix;
  for (std::size_t row = 0; row < source.size(); ++row) {
    std::ranges::copy(source.at(row), result.at(row).begin());
  }
  return result;
}
void blend_local(formats::SkeletonTransform& destination,
                 const formats::SkeletonTransform& source, float weight,
                 bool first) {
  if (first && weight == 1) {
    destination = source;
    return;
  }
  destination.rotation =
      first ? source.rotation
            : rotation_blend(destination.rotation, source.rotation, weight);
  for (std::size_t axis = 0; axis < 3; ++axis) {
    if (first) {
      // A masked-out older member can leave the first admitted weight below
      // one. Preserve the source's weighted-add branch, including scale.
      destination.translation.at(axis) += source.translation.at(axis) * weight;
      destination.scale.at(axis) += source.scale.at(axis) * weight;
    } else {
      destination.translation.at(axis) +=
          (source.translation.at(axis) - destination.translation.at(axis)) *
          weight;
      destination.scale.at(axis) +=
          (source.scale.at(axis) - destination.scale.at(axis)) * weight;
    }
  }
}
void compose_delta(formats::SkeletonTransform& destination,
                   const formats::SkeletonTransform& delta) {
  destination.rotation =
      math::quaternion_product(math::QuaternionProduct<float>{
          .left = delta.rotation, .right = destination.rotation});
  for (std::size_t axis = 0; axis < 3; ++axis) {
    destination.translation.at(axis) += delta.translation.at(axis);
    destination.scale.at(axis) *= delta.scale.at(axis);
  }
}
void validate_layers(std::span<const PoseLayer> layers, std::size_t bones) {
  constexpr std::uint32_t kChannelCount = 32;
  std::optional<std::uint32_t> previous;
  for (const auto& layer : layers) {
    if (layer.channel >= kChannelCount ||
        (previous && layer.channel <= *previous) ||
        layer.contributions.empty() ||
        layer.contributions.size() > kChannelCount) {
      throw std::invalid_argument("Invalid skeletal layer ordering or chain");
    }
    previous = layer.channel;
    for (const auto& contribution : layer.contributions) {
      if (!contribution.clip || contribution.clip->bones() != bones ||
          !std::isfinite(contribution.seconds) || contribution.seconds < 0 ||
          !std::isfinite(contribution.weight) || contribution.weight < 0 ||
          contribution.weight > 1) {
        throw std::invalid_argument("Invalid skeletal layer contribution");
      }
    }
  }
}
float sample_frame(const PreparedClip& clip, double elapsed_seconds) {
  if (!std::isfinite(elapsed_seconds) || elapsed_seconds < 0) {
    throw std::invalid_argument("Invalid skeletal sample time");
  }
  const double duration = clip.duration_seconds();
  const double time = clip.repeating() ? std::fmod(elapsed_seconds, duration)
                                       : std::min(elapsed_seconds, duration);
  return static_cast<float>(time * clip.frames_per_second());
}
}  // namespace

PreparedClip::PreparedClip(const std::shared_ptr<memory::Domain>& domain)
    : tracks_(memory::Allocator<Track>(domain)),
      times_(memory::Allocator<float>(domain)),
      values_(memory::Allocator<std::array<float, 4>>(domain)) {}

PreparedClip PreparedClip::prepare(
    const formats::MotionDefinition& motion,
    const formats::SkeletonDefinition& skeleton,
    const std::shared_ptr<memory::Domain>& domain,
    std::optional<bool> repeating) {
  if (motion.tracks().size() != skeleton.bones().size()) {
    throw std::invalid_argument("Motion and skeleton bone counts differ");
  }
  PreparedClip result(domain);
  const auto root = std::ranges::find_if(
      skeleton.bones(), [](const auto& bone) { return bone.parent < 0; });
  if (root == skeleton.bones().end()) {
    throw std::invalid_argument("Skeleton has no root");
  }
  result.root_bone_ = static_cast<std::size_t>(root - skeleton.bones().begin());
  constexpr std::size_t kDurationOffset = 8, kRateOffset = 12;
  constexpr std::size_t kRepeatOffset = 16;
  result.duration_frames_ = scalar(motion.header(), kDurationOffset);
  result.frames_per_second_ = scalar(motion.header(), kRateOffset);
  result.repeating_ =
      repeating.value_or(word(motion.header(), kRepeatOffset) != 0);
  if (result.duration_frames_ <= 0 || result.frames_per_second_ <= 0) {
    throw resources::Error(resources::ErrorCode::malformed,
                           "Animation clock must have positive duration/rate");
  }
  result.tracks_.reserve(motion.tracks().size());
  for (std::size_t bone = 0; bone < motion.tracks().size(); ++bone) {
    const auto& source = motion.tracks().at(bone);
    Track track{.constant = skeleton.bones().at(bone).relative,
                .channels = {},
                .bind_default = source.record.empty(),
                .channel_mask = source.channel_mask};
    // An absent entire track leaves bind data; present absent channels use
    // that track's authored constants, not the skeleton defaults.
    if (!source.record.empty()) {
      if (motion.whole_transform_channels()) {
        const auto& whole_channel = source.channels.at(0);
        if (!whole_channel) {
          throw std::invalid_argument("Missing whole-transform channel");
        }
        const auto& channel = *whole_channel;
        constexpr std::size_t kWholeTranslationComponent = 7;
        result.prepare_channel(track.channels.at(0), channel, 3,
                               kWholeTranslationComponent, true);
        result.prepare_channel(track.channels.at(1), channel, 4, 0, true);
        result.prepare_channel(track.channels.at(2), channel, 3, 4, true);
      } else {
        constexpr std::array<std::size_t, 3> kOffsets{32, 16, 4};
        constexpr std::array<std::size_t, 3> kComponents{3, 4, 3};
        constexpr std::array<std::size_t, 3> kSourceChannels{2, 1, 0};
        for (std::size_t component = 0; component < 3; ++component) {
          track.constant.translation.at(component) =
              scalar(source.record, kOffsets[0] + component * kWordBytes);
          track.constant.scale.at(component) =
              scalar(source.record, kOffsets[2] + component * kWordBytes);
        }
        for (std::size_t component = 0; component < 4; ++component) {
          track.constant.rotation.at(component) =
              scalar(source.record, kOffsets[1] + component * kWordBytes);
        }
        for (std::size_t channel = 0; channel < source.channels.size();
             ++channel) {
          const auto& source_channel =
              source.channels.at(kSourceChannels.at(channel));
          if (source_channel) {
            result.prepare_channel(track.channels.at(channel), *source_channel,
                                   kComponents.at(channel), 0, false);
          }
        }
      }
    }
    result.tracks_.push_back(track);
  }
  return result;
}

void PreparedClip::prepare_channel(Channel& destination,
                                   const formats::MotionChannel& source,
                                   std::size_t components,
                                   std::size_t component_offset, bool whole) {
  const bool floats = whole || source.encoding == kFloatEncoding;
  const std::size_t scalar_bytes = floats ? kWordBytes : kWordBytes / 2;
  const auto count = static_cast<std::size_t>(source.key_count);
  if (count == 0 ||
      source.value_stride < (component_offset + components) * scalar_bytes ||
      source.times.size() / kWordBytes < count ||
      source.values.size() / source.value_stride < count) {
    throw resources::Error(resources::ErrorCode::malformed,
                           "Animation channel has invalid key layout");
  }
  destination = {times_.size(), count};
  float previous = -std::numeric_limits<float>::infinity();
  for (std::size_t key = 0; key < count; ++key) {
    const float time = scalar(source.times, key * kWordBytes);
    if (time <= previous) {
      throw resources::Error(resources::ErrorCode::malformed,
                             "Animation times must increase strictly");
    }
    previous = time;
    std::array<float, 4> value{};
    for (std::size_t component = 0; component < components; ++component) {
      const auto offset = key * source.value_stride +
                          (component_offset + component) * scalar_bytes;
      value.at(component) = floats ? scalar(source.values, offset)
                                   : half_scalar(source.values, offset);
    }
    times_.push_back(time);
    values_.push_back(value);
  }
}
double PreparedClip::duration_seconds() const {
  return static_cast<double>(duration_frames_) / frames_per_second_;
}
formats::SkeletonTransform PreparedClip::sample(
    std::size_t bone, float frame,
    const formats::SkeletonTransform* bind_default) const {
  const auto& track = tracks_.at(bone);
  auto result = track.bind_default && bind_default != nullptr ? *bind_default
                                                              : track.constant;
  for (std::size_t property = 0; property < track.channels.size(); ++property) {
    const auto& channel = track.channels.at(property);
    if (channel.count == 0) continue;
    const auto times = std::span(times_).subspan(channel.first, channel.count);
    const auto upper = std::ranges::upper_bound(times, frame);
    const auto end = static_cast<std::size_t>(upper - times.begin());
    const auto lower = end == 0 ? 0 : end - 1;
    const auto next = std::min(end, channel.count - 1);
    const auto& first = values_.at(channel.first + lower);
    const auto& last = values_.at(channel.first + next);
    const float alpha =
        lower == next
            ? 0.0F
            : std::clamp((frame - times[lower]) / (times[next] - times[lower]),
                         0.0F, 1.0F);
    auto value = first;
    if (alpha != 0) {
      if (property == 1) {
        value = rotation_blend(first, last, alpha);
      } else {
        for (std::size_t i = 0; i < 3; ++i) {
          value.at(i) = std::lerp(first.at(i), last.at(i), alpha);
        }
      }
    }
    if (property == 1) {
      result.rotation = value;
    } else {
      auto& destination = property == 0 ? result.translation : result.scale;
      std::copy_n(value.begin(), destination.size(), destination.begin());
    }
  }
  return result;
}

bool PreparedClip::has_constant_local_transform(std::size_t bone) const {
  const auto& track = tracks_.at(bone);
  for (std::size_t property = 0; property < track.channels.size(); ++property) {
    const auto& channel = track.channels.at(property);
    if (channel.count < 2) continue;
    const std::size_t components = property == 1 ? 4 : 3;
    const auto& first = values_.at(channel.first);
    for (std::size_t key = 1; key < channel.count; ++key) {
      const auto& value = values_.at(channel.first + key);
      for (std::size_t component = 0; component < components; ++component) {
        if (value.at(component) != first.at(component)) return false;
      }
    }
  }
  return true;
}
formats::SkeletonTransform PreparedClip::end_transform(std::size_t bone) const {
  return sample(bone, duration_frames_);
}

formats::SkeletonTransform PreparedClip::local_transform(
    std::size_t bone, double sampled_seconds) const {
  if (!std::isfinite(sampled_seconds) || sampled_seconds < 0 ||
      sampled_seconds > duration_seconds()) {
    throw std::invalid_argument("Invalid sampled clip clock");
  }
  return sample(bone, static_cast<float>(sampled_seconds * frames_per_second_));
}
Matrix PreparedClip::root_relative_transform(const ClipRootStep& step) const {
  if (!std::isfinite(step.signed_tick)) {
    throw std::invalid_argument("Invalid relative root tick");
  }
  auto previous = local_transform(root_bone_, step.previous_seconds);
  auto current = local_transform(root_bone_, step.seconds);
  previous.scale = {1, 1, 1};
  current.scale = {1, 1, 1};
  // Source compares the observed controller clocks, compensating translation
  // only. Endpoint rotation is deliberately absent from this wrap operation.
  if (step.signed_tick > 0 && step.previous_seconds > step.seconds) {
    current.translation =
        math::add(current.translation, end_transform(root_bone_).translation);
  } else if (step.signed_tick < 0 && step.previous_seconds < step.seconds) {
    previous.translation =
        math::add(previous.translation, end_transform(root_bone_).translation);
  }
  const auto inverse = math::inverse_affine(transform_matrix(previous));
  if (!inverse) throw std::invalid_argument("Singular previous root transform");
  const auto result = multiply(transform_matrix(current), *inverse);
  for (const auto& row : result) {
    if (!math::all_finite(row)) {
      throw std::invalid_argument("Nonfinite relative root transform");
    }
  }
  return result;
}

// Convert the source transform record through the common row-vector math API.
Matrix transform_matrix(const formats::SkeletonTransform& transform) {
  return math::homogeneous_transform(math::TransformComponents<float>{
      transform.rotation, transform.translation, transform.scale});
}

// Snapshot storage is per instance; evaluation commits it with the pose only
// after modifiers and hierarchy have passed finite validation. E.4/MEM.9 apply
// in both phases as recorded in PoseReleaseState's public contract.
PoseReleaseState::PoseReleaseState(
    std::size_t bones, const std::shared_ptr<memory::Domain>& domain)
    : snapshot_(bones, {},
                memory::Allocator<formats::SkeletonTransform>(domain)),
      pending_snapshot_(bones, {},
                        memory::Allocator<formats::SkeletonTransform>(domain)),
      mask_(bones, 0, memory::Allocator<std::uint8_t>(domain)),
      pending_mask_(bones, 0, memory::Allocator<std::uint8_t>(domain)) {}
void PoseReleaseState::copy_from(const PoseReleaseState& source) {
  if (mask_.size() != source.mask_.size()) {
    throw std::invalid_argument("Pose release snapshot capacity differs");
  }
  if (this == &source) return;
  // All stored values already passed pose validation; fixed-size copies cannot
  // fail after the capacity guard. Pending scratch belongs to the destination.
  std::ranges::copy(source.snapshot_, snapshot_.begin());
  std::ranges::copy(source.mask_, mask_.begin());
  weight_ = source.weight_;
}
void PoseReleaseState::clear() {
  weight_ = 0;
  std::ranges::fill(mask_, 0);
}
SkeletonPose::SkeletonPose(const formats::SkeletonDefinition& skeleton,
                           const std::shared_ptr<memory::Domain>& domain)
    : skeleton_(&skeleton),
      order_(memory::Allocator<std::size_t>(domain)),
      override_indices_(skeleton.bones().size(), 0,
                        memory::Allocator<std::size_t>(domain)),
      local_(skeleton.bones().size(), {},
             memory::Allocator<formats::SkeletonTransform>(domain)),
      world_(skeleton.bones().size(), {}, memory::Allocator<Matrix>(domain)),
      skin_(skeleton.bones().size(), {}, memory::Allocator<Matrix>(domain)),
      local_matrices_(skeleton.bones().size(), {},
                      memory::Allocator<Matrix>(domain)),
      pending_local_matrices_(skeleton.bones().size(), {},
                              memory::Allocator<Matrix>(domain)),
      pending_local_(skeleton.bones().size(), {},
                     memory::Allocator<formats::SkeletonTransform>(domain)),
      additive_local_(skeleton.bones().size(), {},
                      memory::Allocator<formats::SkeletonTransform>(domain)),
      pending_world_(skeleton.bones().size(), {},
                     memory::Allocator<Matrix>(domain)),
      pending_skin_(skeleton.bones().size(), {},
                    memory::Allocator<Matrix>(domain)) {
  const auto root = std::ranges::find_if(
      skeleton.bones(), [](const auto& bone) { return bone.parent < 0; });
  if (root == skeleton.bones().end()) {
    throw std::invalid_argument("Skeleton has no root");
  }
  root_bone_ = static_cast<std::size_t>(root - skeleton.bones().begin());
  order_.reserve(skeleton.bones().size());
  memory::Vector<bool> ready(skeleton.bones().size(), false,
                             memory::Allocator<bool>(domain));
  memory::Vector<std::size_t> chain{memory::Allocator<std::size_t>(domain)};
  chain.reserve(skeleton.bones().size());
  // The validated source graph need not be stored topologically. Prepare a
  // stable parent-first walk once, without recursive per-frame ancestry work.
  for (std::size_t bone = 0; bone < skeleton.bones().size(); ++bone) {
    auto current = static_cast<std::int64_t>(bone);
    while (current >= 0 && !ready.at(static_cast<std::size_t>(current))) {
      const auto index = static_cast<std::size_t>(current);
      chain.push_back(index);
      ready.at(index) = true;
      current = skeleton.bones().at(index).parent;
    }
    for (auto index = chain.rbegin(); index != chain.rend(); ++index) {
      order_.push_back(*index);
    }
    chain.clear();
  }
  bind_pose();
}
std::array<float, 3> PreparedClip::root_translation_delta(
    double from_seconds, double to_seconds) const {
  if (tracks_.empty() || !std::isfinite(from_seconds) ||
      !std::isfinite(to_seconds) || from_seconds < 0 ||
      to_seconds < from_seconds) {
    throw std::invalid_argument("Invalid root motion interval");
  }
  const auto start = sample(root_bone_, 0).translation;
  const auto end = sample(root_bone_, duration_frames_).translation;
  const auto accumulated = [&](double seconds) {
    const double duration = duration_seconds();
    const double cycles =
        repeating_ && duration > 0 ? std::floor(seconds / duration) : 0;
    const double local = repeating_ && duration > 0
                             ? std::fmod(seconds, duration)
                             : std::min(seconds, duration);
    auto result =
        sample(root_bone_, static_cast<float>(local * frames_per_second_))
            .translation;
    for (std::size_t axis = 0; axis < result.size(); ++axis) {
      result.at(axis) +=
          static_cast<float>(cycles * (end.at(axis) - start.at(axis)));
    }
    return result;
  };
  const auto first = accumulated(from_seconds);
  auto result = accumulated(to_seconds);
  for (std::size_t axis = 0; axis < result.size(); ++axis) {
    result.at(axis) -= first.at(axis);
  }
  if (!std::ranges::all_of(result,
                           [](float value) { return std::isfinite(value); })) {
    throw std::invalid_argument("Unrepresentable root motion displacement");
  }
  return result;
}
void SkeletonPose::evaluate(
    const PreparedClip& clip, double elapsed_seconds, const Matrix& actor_world,
    bool lock_root_translation,
    std::span<const BoneLocalBasisOverride> basis_overrides) {
  if (clip.bones() != local_.size() || !std::isfinite(elapsed_seconds) ||
      elapsed_seconds < 0 || !math::all_finite(actor_world)) {
    throw std::invalid_argument("Invalid skeletal sample inputs");
  }
  const float frame = sample_frame(clip, elapsed_seconds);
  for (std::size_t bone = 0; bone < local_.size(); ++bone) {
    pending_local_.at(bone) =
        clip.sample(bone, frame, &skeleton_->bones().at(bone).relative);
  }
  // The movement owner consumes the extracted displacement exactly once.
  if (lock_root_translation && !pending_local_.empty()) {
    pending_local_.at(clip.root_bone_).translation =
        clip.sample(clip.root_bone_, 0,
                    &skeleton_->bones().at(clip.root_bone_).relative)
            .translation;
  }
  override_local_basis(basis_overrides);
  compose(actor_world);
}
void SkeletonPose::blend(
    float alpha, const PreparedClip& from, double from_seconds,
    const PreparedClip& to, double to_seconds, const Matrix& actor_world,
    bool lock_root_translation,
    std::span<const BoneLocalBasisOverride> basis_overrides) {
  if (from.bones() != local_.size() || to.bones() != local_.size() ||
      !std::isfinite(alpha) || alpha < 0 || alpha > 1 ||
      !math::all_finite(actor_world)) {
    throw std::invalid_argument("Invalid skeletal blend inputs");
  }
  const float from_frame = sample_frame(from, from_seconds);
  const float to_frame = sample_frame(to, to_seconds);
  for (std::size_t bone = 0; bone < local_.size(); ++bone) {
    auto first =
        from.sample(bone, from_frame, &skeleton_->bones().at(bone).relative);
    auto second =
        to.sample(bone, to_frame, &skeleton_->bones().at(bone).relative);
    if (lock_root_translation && bone == from.root_bone_) {
      first.translation =
          from.sample(from.root_bone_, 0,
                      &skeleton_->bones().at(from.root_bone_).relative)
              .translation;
      second.translation =
          to.sample(to.root_bone_, 0,
                    &skeleton_->bones().at(to.root_bone_).relative)
              .translation;
    }
    auto& result = pending_local_.at(bone);
    result.rotation =
        alpha == 0
            ? first.rotation
            : (alpha == 1
                   ? second.rotation
                   : rotation_blend(first.rotation, second.rotation, alpha));
    for (std::size_t axis = 0; axis < 3; ++axis) {
      result.translation.at(axis) = std::lerp(
          first.translation.at(axis), second.translation.at(axis), alpha);
      result.scale.at(axis) =
          std::lerp(first.scale.at(axis), second.scale.at(axis), alpha);
    }
  }
  override_local_basis(basis_overrides);
  compose(actor_world);
}
formats::SkeletonTransform SkeletonPose::layer_sample(
    const PreparedClip& clip, std::size_t bone, float frame,
    bool lock_root_translation) const {
  const auto& bind = skeleton_->bones().at(bone).relative;
  auto sample = clip.sample(bone, frame, &bind);
  if (lock_root_translation && bone == clip.root_bone_) {
    sample.translation = clip.sample(bone, 0, &bind).translation;
  }
  return sample;
}
void SkeletonPose::evaluate_ordinary_layers(std::span<const PoseLayer> layers,
                                            bool lock_root_translation,
                                            PoseReleaseState* release) {
  for (const auto& layer : layers) {
    std::ranges::fill(override_indices_, 0);
    const auto bit = std::uint32_t{1} << layer.channel;
    bool oldest = true;
    for (const auto& contribution : layer.contributions) {
      const auto& clip = *contribution.clip;
      const auto frame = sample_frame(clip, contribution.seconds);
      const float weight = oldest ? 1 : contribution.weight;
      oldest = false;
      for (std::size_t bone = 0; bone < local_.size(); ++bone) {
        if ((clip.channel_mask(bone) & bit) == 0) continue;
        const auto sample =
            layer_sample(clip, bone, frame, lock_root_translation);
        blend_local(pending_local_.at(bone), sample, weight,
                    override_indices_.at(bone) == 0);
        override_indices_.at(bone) = 1;
        if (release && layer.channel > 0) release->pending_mask_.at(bone) = 1;
      }
    }
  }
}
void SkeletonPose::evaluate_additive_layers(std::span<const PoseLayer> layers,
                                            bool lock_root_translation) {
  // Admission is accumulated across all additive channels before applying the
  // resulting delta to the ordinary pose. Missing tracks add no bind offset.
  std::ranges::fill(override_indices_, 0);
  for (const auto& layer : layers) {
    const auto bit = std::uint32_t{1} << layer.channel;
    for (const auto& contribution : layer.contributions) {
      const auto& clip = *contribution.clip;
      const auto frame = sample_frame(clip, contribution.seconds);
      for (std::size_t bone = 0; bone < local_.size(); ++bone) {
        if ((clip.channel_mask(bone) & bit) == 0 ||
            clip.tracks_.at(bone).bind_default) {
          continue;
        }
        const auto sample =
            layer_sample(clip, bone, frame, lock_root_translation);
        auto& result = additive_local_.at(bone);
        if (override_indices_.at(bone) == 0) {
          result = sample;
        } else {
          compose_delta(result, sample);
        }
        override_indices_.at(bone) = 1;
      }
    }
  }
  for (std::size_t bone = 0; bone < local_.size(); ++bone) {
    if (override_indices_.at(bone) != 0) {
      compose_delta(pending_local_.at(bone), additive_local_.at(bone));
    }
  }
}
void SkeletonPose::evaluate_layers(
    const PoseLayers& layers, const Matrix& actor_world,
    RootPosePolicy root_policy,
    std::span<const BoneLocalBasisOverride> basis_overrides,
    const PoseReleaseStep& release, const AimChainStep& aim) {
  if (!math::all_finite(actor_world)) {
    throw std::invalid_argument("Invalid skeletal layer world transform");
  }
  if (root_policy != RootPosePolicy::animated &&
      root_policy != RootPosePolicy::lock_translation &&
      root_policy != RootPosePolicy::identity) {
    throw std::invalid_argument("Invalid skeletal root pose policy");
  }
  const bool lock_root_translation =
      root_policy == RootPosePolicy::lock_translation;
  validate_layers(layers.ordinary, local_.size());
  validate_layers(layers.additive, local_.size());
  for (std::size_t bone = 0; bone < local_.size(); ++bone) {
    pending_local_.at(bone) = skeleton_->bones().at(bone).relative;
  }
  auto* snapshot = release.state;
  if (snapshot && (snapshot->mask_.size() != local_.size() ||
                   !std::isfinite(release.seconds) || release.seconds < 0 ||
                   !std::isfinite(release.decay_per_second) ||
                   release.decay_per_second <= 0)) {
    throw std::invalid_argument("Invalid local pose release step");
  }
  if (snapshot) std::ranges::fill(snapshot->pending_mask_, 0);
  evaluate_ordinary_layers(layers.ordinary, lock_root_translation, snapshot);
  evaluate_additive_layers(layers.additive, lock_root_translation);
  const bool higher_channel = std::ranges::any_of(
      layers.ordinary, [](const auto& layer) { return layer.channel > 0; });
  float release_weight = 0;
  if (snapshot) {
    if (higher_channel) {
      // The snapshot includes additive composition, but excludes subsequent
      // direction/pose modifiers. Only higher-channel admitted bones release.
      std::ranges::copy(pending_local_, snapshot->pending_snapshot_.begin());
      release_weight = 1;
    } else {
      release_weight = static_cast<float>(std::max(
          0.0, snapshot->weight_ - release.seconds * release.decay_per_second));
      if (release_weight > 0) {
        for (std::size_t bone = 0; bone < local_.size(); ++bone) {
          if (snapshot->mask_.at(bone) == 0) continue;
          blend_local(pending_local_.at(bone), snapshot->snapshot_.at(bone),
                      release_weight, false);
        }
      }
    }
  }
  override_local_basis(basis_overrides);
  // Root motion has already changed the actor. Remove the entire local root
  // once, after blending and snapshot capture, before building descendants.
  if (root_policy == RootPosePolicy::identity) {
    pending_local_.at(root_bone_) = {
        .rotation = {0, 0, 0, 1}, .translation = {}, .scale = {1, 1, 1}};
  }
  compose(actor_world, aim);
  if (snapshot) {
    if (higher_channel) {
      snapshot->snapshot_.swap(snapshot->pending_snapshot_);
      snapshot->mask_.swap(snapshot->pending_mask_);
    }
    snapshot->weight_ = release_weight;
  }
}
void SkeletonPose::bind_pose(const Matrix& actor_world) {
  if (!math::all_finite(actor_world)) {
    throw std::invalid_argument("Invalid actor transform");
  }
  for (std::size_t bone = 0; bone < local_.size(); ++bone) {
    pending_local_.at(bone) = skeleton_->bones().at(bone).relative;
  }
  compose(actor_world);
}
void SkeletonPose::override_local_basis(
    std::span<const BoneLocalBasisOverride> overrides) {
  std::ranges::fill(override_indices_, local_.size());
  for (const auto& override : overrides) {
    if (override.bone >= local_.size() ||
        override_indices_.at(override.bone) != local_.size() ||
        !math::all_finite(override.rotation) ||
        !math::all_finite(override.scale)) {
      throw std::invalid_argument(
          "Invalid or duplicate local bone basis override");
    }
    override_indices_.at(override.bone) = override.bone;
    auto& local = pending_local_.at(override.bone);
    local.rotation = override.rotation;
    local.scale = override.scale;
  }
}
void SkeletonPose::apply_world_overrides(
    std::span<const BoneWorldOverride> overrides) {
  if (overrides.empty()) return;
  std::ranges::fill(override_indices_, overrides.size());
  for (std::size_t index = 0; index < overrides.size(); ++index) {
    const auto& override = overrides[index];
    if (override.bone >= local_.size() ||
        override_indices_.at(override.bone) != overrides.size() ||
        !math::all_finite(override.world)) {
      throw std::invalid_argument("Invalid or duplicate world bone override");
    }
    override_indices_.at(override.bone) = index;
  }
  // Absolute targets win; untouched descendants keep their sampled local pose.
  // Prepared parent order makes independent solver output order irrelevant.
  for (const auto bone : order_) {
    const auto& definition = skeleton_->bones().at(bone);
    const auto index = override_indices_.at(bone);
    if (index != overrides.size()) {
      pending_world_.at(bone) = overrides[index].world;
    } else {
      const auto& parent =
          definition.parent < 0
              ? actor_world_
              : pending_world_.at(static_cast<std::size_t>(definition.parent));
      pending_world_.at(bone) = multiply(local_matrices_.at(bone), parent);
    }
    pending_skin_.at(bone) = multiply(
        from_affine(definition.inverse_bind_matrix), pending_world_.at(bone));
    if (!math::all_finite(pending_world_.at(bone)) ||
        !math::all_finite(pending_skin_.at(bone))) {
      throw std::overflow_error("Bone override exceeds finite range");
    }
  }
  world_.swap(pending_world_);
  skin_.swap(pending_skin_);
}
void SkeletonPose::apply_aim_chain(const AimChainStep& aim) {
  if (aim.bones.empty()) return;
  constexpr std::size_t kMaximumChain = 8;
  constexpr float kUnitTolerance = .001F;
  if (aim.bones.size() < 2 || aim.bones.size() > kMaximumChain ||
      !std::isfinite(aim.weight) || aim.weight < 0 || aim.weight > 1 ||
      !math::all_finite(aim.target) ||
      std::abs(math::dot(aim.target, aim.target) - 1) > kUnitTolerance) {
    throw std::invalid_argument("Invalid aim chain request");
  }
  std::array<Matrix, kMaximumChain> frames{};
  std::size_t previous = root_bone_;
  Matrix accumulated = kIdentityMatrix;
  for (std::size_t index = 0; index < aim.bones.size(); ++index) {
    const auto bone = aim.bones[index];
    if (bone >= local_.size() || bone == previous || bone == root_bone_) {
      throw std::invalid_argument("Invalid aim chain bone");
    }
    auto frame = pending_local_matrices_.at(bone);
    auto parent = skeleton_->bones().at(bone).parent;
    while (parent >= 0 && static_cast<std::size_t>(parent) != previous) {
      const auto parent_index = static_cast<std::size_t>(parent);
      frame = multiply(frame, pending_local_matrices_.at(parent_index));
      parent = skeleton_->bones().at(parent_index).parent;
    }
    if (parent < 0) {
      throw std::invalid_argument("Aim chain is not ancestor ordered");
    }
    frame = multiply(frame, accumulated);
    frames.at(index) = frame;
    accumulated = frame;
    previous = bone;
  }
  if (aim.weight == 0) return;
  // Cache original component frames before writing any local correction.
  const auto& terminal = frames.at(aim.bones.size() - 1);
  const math::Vector3<float> forward{terminal[2][0], terminal[2][1],
                                     terminal[2][2]};
  if (!math::all_finite(forward) || math::dot(forward, forward) <= 0) {
    throw std::invalid_argument("Invalid aim marker direction");
  }
  // Source407c90 preserves the authored marker magnitude; only its sum with
  // the target is normalized. Rebuilding a unit marker changes scaled poses.
  math::Vector3<float> axis{};
  const auto sum = math::add(forward, aim.target);
  if (math::dot(sum, sum) <= 0) {
    // Exact antipodes use the negative up row from source409b70. Its parallel
    // branch selects -Z above the source vertical threshold, otherwise +Y;
    // the degenerate right vector falls back to +X before reconstructing up.
    constexpr float kVerticalThreshold = .9990000128746033F;
    constexpr float kBasisEpsilon = 1.1920928955078125e-7F;
    const math::Vector3<float> up{0, 1, 0};
    auto right = math::cross(up, forward);
    if (math::dot(right, right) <= 0) {
      right = math::cross(
          forward[1] > kVerticalThreshold ? math::Vector3<float>{0, 0, -1} : up,
          forward);
    }
    right = math::normalize_or(right, {1, 0, 0}, kBasisEpsilon);
    axis = math::scale(
        math::normalize_or(math::cross(forward, right), {0, 1, 0}, 0.F), -1.F);
  }
  const auto rotation = math::rotation_between(
      math::DirectionRotation<float>{forward, aim.target, axis});
  constexpr float kLinearThreshold = .9999899864F;
  const auto delta = math::quaternion_slerp(
      math::Vector<float, 4>{0, 0, 0, 1}, rotation,
      math::SphericalBlend<float>{
          aim.weight / static_cast<float>(aim.bones.size() - 1),
          kLinearThreshold});
  const auto correction = math::homogeneous_transform(
      math::TransformComponents<float>{delta, {}, {1, 1, 1}});
  for (std::size_t index = 0; index + 1 < aim.bones.size(); ++index) {
    auto basis = frames.at(index);
    math::set_translation(basis, math::Vector3<float>{});
    const auto inverse = math::inverse_affine(basis);
    if (!inverse) throw std::invalid_argument("Singular aim chain frame");
    const auto local_correction =
        multiply(multiply(basis, correction), *inverse);
    auto& local = pending_local_matrices_.at(aim.bones[index]);
    local = multiply(local_correction, local);
  }
}
void SkeletonPose::compose(const Matrix& actor_world, const AimChainStep& aim) {
  for (std::size_t bone = 0; bone < pending_local_.size(); ++bone) {
    pending_local_matrices_.at(bone) =
        transform_matrix(pending_local_.at(bone));
  }
  apply_aim_chain(aim);
  // Unskinned component bones serve attachments. The separate skin output
  // applies inverse bind before the same bone/world transform.
  for (const auto bone : order_) {
    const auto& definition = skeleton_->bones().at(bone);
    const auto& parent =
        definition.parent < 0
            ? actor_world
            : pending_world_.at(static_cast<std::size_t>(definition.parent));
    pending_world_.at(bone) =
        multiply(pending_local_matrices_.at(bone), parent);
    pending_skin_.at(bone) = multiply(
        from_affine(definition.inverse_bind_matrix), pending_world_.at(bone));
    if (!math::all_finite(pending_world_.at(bone)) ||
        !math::all_finite(pending_skin_.at(bone))) {
      throw std::overflow_error("Skeletal transform exceeds finite range");
    }
  }
  actor_world_ = actor_world;
  local_.swap(pending_local_);
  local_matrices_.swap(pending_local_matrices_);
  world_.swap(pending_world_);
  skin_.swap(pending_skin_);
}
}  // namespace bw::animation
