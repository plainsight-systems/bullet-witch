// Authored per-bone channel masks are retained during bounded parsing for
// prepared pose layers. Both phases Core I.1/E.4 and performance MEM.9/CDSA.32
// keep membership explicit and avoid archive access during evaluation. See
// alicia-animation-layers.md.
// Shared math retains each caller's scalar/accumulator precision; tolerance
// and failure policy remain here. Both-phase I.9/MEM.9 contracts are in
// bw/math/vector.hpp and matrix.hpp.
// Load-time bounded relocation and bind-transform construction; contracts and
// both-phase I.11/MEM.6 guidance are in animation_definition.hpp. Source bytes
// remain immutable; metadata replaces native pointer mutation. No sampling.
// Cancellation validates the authored channel once and reduces its latch to
// the earliest frame. E.4/MEM.9 from the header apply to this fixed-size
// result.
#include "bw/content/formats/animation_definition.hpp"

#include <algorithm>
#include <bit>
#include <cmath>

#include "bw/content/formats/binary_reader.hpp"
#include "bw/math/matrix.hpp"

namespace bw::formats {
namespace {
using resources::detail::malformed;
using resources::detail::Reader;
using resources::detail::unsupported;
constexpr std::uint64_t kMotionHeader = 24, kTrackBytes = 56, kBoneBytes = 104;
constexpr std::uint64_t kChannelBytes = 32, kWholeChannelBytes = 28;
constexpr std::uint64_t kChannelPointer = 44, kTimePointer = 24;
constexpr std::uint64_t kWholeTimePointer = 20;
constexpr std::uint64_t kRelativeOffset = 24, kAbsoluteOffset = 64;
constexpr std::uint64_t kTranslationOffset = 16, kScaleOffset = 28;
constexpr std::uint32_t kWholeTransformMagic = 0x4241425f;
constexpr std::uint32_t kSeparateChannelsMagic = 0x00414237;
float scalar(const Reader& r, std::uint64_t offset) {
  const auto value = std::bit_cast<float>(r.u32(offset));
  if (!std::isfinite(value)) malformed("Nonfinite skeleton transform");
  return value;
}
MotionChannel channel(const Reader& r, std::uint32_t offset, bool whole,
                      std::uint32_t whole_stride) {
  r.range(offset, whole ? kWholeChannelBytes : kChannelBytes);
  MotionChannel result;
  result.source_offset = offset;
  result.encoding = r.u32(offset);
  constexpr std::uint64_t kKeyCountOffset = 8;
  result.key_count = r.u32(offset + kKeyCountOffset);
  // Whole-transform samples retain ten float components; separate channels
  // carry their explicit byte stride. This is construction, not interpolation.
  constexpr std::uint64_t kStrideOffset = 12;
  result.value_stride = whole ? whole_stride : r.u32(offset + kStrideOffset);
  if (result.key_count != 0 && result.value_stride == 0) {
    malformed("Motion channel has no value stride");
  }
  const auto pointers = offset + (whole ? kWholeTimePointer : kTimePointer);
  const auto times = r.u32(pointers), values = r.u32(pointers + 4);
  const auto time_bytes =
      static_cast<std::uint64_t>(result.key_count) * sizeof(float);
  const auto value_bytes =
      static_cast<std::uint64_t>(result.key_count) * result.value_stride;
  r.range(times, time_bytes);
  r.range(values, value_bytes);
  result.times = r.b.subspan(times, static_cast<std::size_t>(time_bytes));
  result.values = r.b.subspan(values, static_cast<std::size_t>(value_bytes));
  return result;
}
SkeletonTransform transform(const Reader& r, std::uint64_t offset) {
  SkeletonTransform value;
  for (std::size_t i = 0; i < value.rotation.size(); ++i) {
    value.rotation.at(i) = scalar(r, offset + i * 4);
  }
  for (std::size_t i = 0; i < value.scale.size(); ++i) {
    value.translation.at(i) = scalar(r, offset + kTranslationOffset + i * 4);
    value.scale.at(i) = scalar(r, offset + kScaleOffset + i * 4);
  }
  return value;
}
using Matrix = std::array<std::array<float, 3>, 4>;
Matrix matrix(const SkeletonTransform& transform) {
  return math::affine_transform(math::TransformComponents<float>{
      transform.rotation, transform.translation, transform.scale});
}
Matrix inverse(const Matrix& matrix) {
  const auto result = math::inverse_affine(matrix);
  if (!result) malformed("Singular skeleton bind transform");
  return *result;
}
}  // namespace
MotionCancellation MotionCancellation::parse(
    std::span<const std::uint8_t> bytes) {
  constexpr std::uint64_t kParameterHeader = 44, kCancelOffset = 20;
  constexpr std::uint32_t kParameterMagic = 0x30415046;
  constexpr std::uint32_t kCancelMagic = 0x43414e30;
  constexpr std::uint64_t kSectionHeader = 12, kRecordBytes = 36;
  constexpr std::uint64_t kFrameOffset = 32;
  const Reader reader{bytes};
  reader.range(0, kParameterHeader);
  if (reader.u32(0) != kParameterMagic) {
    unsupported("Unsupported motion parameter encoding");
  }
  MotionCancellation result;
  const auto section = reader.u32(kCancelOffset);
  if (section == 0) return result;
  reader.range(section, kSectionHeader);
  if (reader.u32(section) != kCancelMagic) {
    unsupported("Unsupported motion cancellation channel");
  }
  const auto count = reader.u32(section + 4);
  const auto records = reader.u32(section + 8);
  reader.range(records, static_cast<std::uint64_t>(count) * kRecordBytes);
  // Every record is checked, even after finding the first cancellation frame.
  for (std::uint64_t index = 0; index < count; ++index) {
    const auto frame = std::bit_cast<std::int32_t>(
        reader.u32(records + index * kRecordBytes + kFrameOffset));
    if (frame < 0) malformed("Negative motion cancellation frame");
    const auto unsigned_frame = static_cast<std::uint32_t>(frame);
    result.first_frame = result.first_frame
                             ? std::min(*result.first_frame, unsigned_frame)
                             : unsigned_frame;
  }
  return result;
}
MotionDefinition MotionDefinition::parse(
    std::span<const std::uint8_t> bytes,
    const std::shared_ptr<memory::Domain>& domain) {
  const Reader r{bytes};
  r.range(0, kMotionHeader);
  constexpr std::uint32_t kBoneTransformBytes = 40;
  MotionDefinition result(domain);
  result.header_ = bytes.first(kMotionHeader);
  const auto magic = r.u32(0);
  if (magic != kWholeTransformMagic && magic != kSeparateChannelsMagic) {
    unsupported("Unsupported motion encoding");
  }
  result.whole_transform_ = magic == kWholeTransformMagic;
  const auto count = r.u32(kMotionHeader - 4);
  r.range(kMotionHeader,
          static_cast<std::uint64_t>(count) * 3 * sizeof(std::uint32_t));
  result.tracks_.reserve(count);
  for (std::uint64_t n = 0; n < count; ++n) {
    MotionTrack track;
    track.channel_mask =
        r.u32(kMotionHeader + static_cast<std::uint64_t>(count) * 4 + n * 4);
    track.source_index =
        std::bit_cast<std::int32_t>(r.u32(kMotionHeader + n * 4));
    if (track.source_index < -1 ||
        track.source_index >= static_cast<std::int64_t>(count)) {
      malformed("Motion track index exceeds directory");
    }
    if (track.source_index >= 0) {
      const auto directory =
          kMotionHeader + static_cast<std::uint64_t>(count) * 8;
      const auto offset =
          r.u32(directory + static_cast<std::uint64_t>(track.source_index) * 4);
      if (offset != 0) {
        const auto size =
            result.whole_transform_ ? kWholeChannelBytes : kTrackBytes;
        r.range(offset, size);
        track.record = bytes.subspan(offset, size);
        if (result.whole_transform_) {
          track.channels.at(0) = channel(r, offset, true, kBoneTransformBytes);
        } else {
          for (std::size_t c = 0; c < track.channels.size(); ++c) {
            const auto nested = r.u32(offset + kChannelPointer + c * 4);
            if (nested != 0) {
              track.channels.at(c) = channel(r, nested, false, 0);
            }
          }
        }
      }
    }
    result.tracks_.push_back(track);
  }
  return result;
}
ShapeAnimationDefinition ShapeAnimationDefinition::parse(
    std::span<const std::uint8_t> bytes,
    const std::shared_ptr<memory::Domain>& domain) {
  const Reader r{bytes};
  if (r.tag_view(0) != "_SAB") {
    unsupported("Unsupported shape animation encoding");
  }
  const auto count = r.u32(kMotionHeader - 4);
  constexpr std::uint64_t kNameBytes = 32;
  r.range(kMotionHeader, static_cast<std::uint64_t>(count) * (kNameBytes + 4));
  ShapeAnimationDefinition result(domain);
  result.header_ = bytes;
  result.channels_.reserve(count);
  for (std::uint64_t index = 0; index < count; ++index) {
    ShapeChannel item;
    item.name =
        r.text_view({kMotionHeader + index * kNameBytes, kNameBytes}, 0);
    const auto offset =
        r.u32(kMotionHeader + static_cast<std::uint64_t>(count) * kNameBytes +
              index * 4);
    if (offset != 0) {
      item.channel = channel(r, offset, true, sizeof(float));
      constexpr std::uint64_t kShapeEndFrameOffset = 16;
      item.end_frame = scalar(r, offset + kShapeEndFrameOffset);
    }
    result.channels_.push_back(item);
  }
  return result;
}
SkeletonDefinition SkeletonDefinition::parse(
    std::span<const std::uint8_t> bytes,
    const std::shared_ptr<memory::Domain>& domain) {
  const Reader r{bytes};
  if (r.tag_view(0) != "_BLB") unsupported("Unsupported skeleton encoding");
  const auto count = r.u32(4);
  constexpr std::uint64_t kSkeletonHeader = 8;
  if (bytes.size() !=
      kSkeletonHeader + static_cast<std::uint64_t>(count) * kBoneBytes) {
    malformed("Skeleton record extent mismatch");
  }
  SkeletonDefinition result(domain);
  result.bones_.reserve(count);
  for (std::uint64_t n = 0; n < count; ++n) {
    const auto offset = kSkeletonHeader + n * kBoneBytes;
    SkeletonBone bone;
    for (std::size_t field = 0; field < bone.source_fields.size(); ++field) {
      bone.source_fields.at(field) = r.u32(offset + field * 4);
    }
    bone.parent = std::bit_cast<std::int32_t>(r.u32(offset + 4));
    if (bone.parent < -1 || bone.parent >= static_cast<std::int64_t>(count)) {
      malformed("Skeleton parent exceeds bone directory");
    }
    bone.relative = transform(r, offset + kRelativeOffset);
    bone.absolute = transform(r, offset + kAbsoluteOffset);
    bone.relative_matrix = matrix(bone.relative);
    bone.inverse_bind_matrix = inverse(matrix(bone.absolute));
    result.bones_.push_back(bone);
  }
  // Mark each parent chain once; completed chains are shared by descendants.
  memory::Vector<std::uint8_t> marks(count, 0,
                                     memory::Allocator<std::uint8_t>(domain));
  for (std::size_t n = 0; n < result.bones_.size(); ++n) {
    auto parent = static_cast<std::int64_t>(n);
    while (parent >= 0 && marks.at(static_cast<std::size_t>(parent)) == 0) {
      marks.at(static_cast<std::size_t>(parent)) = 1;
      parent = result.bones_.at(static_cast<std::size_t>(parent)).parent;
    }
    if (parent >= 0 && marks.at(static_cast<std::size_t>(parent)) == 1) {
      malformed("Cyclic skeleton parent graph");
    }
    parent = static_cast<std::int64_t>(n);
    while (parent >= 0 && marks.at(static_cast<std::size_t>(parent)) == 1) {
      marks.at(static_cast<std::size_t>(parent)) = 2;
      parent = result.bones_.at(static_cast<std::size_t>(parent)).parent;
    }
  }
  return result;
}
}  // namespace bw::formats
