// Authored per-bone channel masks are retained during bounded parsing for
// prepared pose layers. Both phases Core I.1/E.4 and performance MEM.9/CDSA.32
// keep membership explicit and avoid archive access during evaluation. See
// alicia-animation-layers.md.
// Load-time animation relocation and skeleton construction. Input bytes are
// borrowed immutable archives and must outlive the returned views. Original
// x86 pointer fixups become bounded offsets; metadata uses the injected domain.
// Motion sampling, pose blending and skinning remain separate consumers.
// Algorithm: validate format/count directories, resolve every present track
// and channel, retain source order; skeletons validate parent indices and
// finite quaternion/translation/scale values, then derive original bind
// matrices. Costs O(tracks + channels + bones), metadata O(tracks + bones), no
// bulk copy. Both MCP phases: I.11 (#ri-raw) keeps borrowed bytes distinct from
// value-owned metadata; MEM.6
// (guidelines/memory/MEM.6-inject-allocators-behind-one-interface.md) routes
// storage through the domain; character-load-constructors.md records source and
// numeric scope.
// MotionCancellation decodes the authored cancel-event channel only. It owns
// the earliest nonnegative frame, or no value when the channel is absent/empty;
// input bytes are borrowed only during parse. Validate header, directory and
// every fixed-stride record before publishing the value. Unsupported tags or
// malformed ranges/negative frames fail. No allocation or event dispatch
// occurs. Both-phase E.4 (#re-design-invariants) preserves validated frame
// state and MEM.9 (guidelines/memory/MEM.9-embedded-allocate-at-init.md) keeps
// the later animation cancellation check constant-size and allocation-free.
#pragma once

#include "bw/formats/common.hpp"
#include "bw/memory/allocator.hpp"

namespace bw::formats {
struct MotionCancellation {
  std::optional<std::uint32_t> first_frame;
  static MotionCancellation parse(std::span<const std::uint8_t> bytes);
};
struct MotionChannel {
  std::uint32_t source_offset{}, encoding{}, key_count{}, value_stride{};
  std::span<const std::uint8_t> times, values;
};
struct MotionTrack {
  std::int32_t source_index{-1};
  // Authored bone membership in the 32 animation channels; not mesh visibility.
  // Design I.1/E.4 and MEM.9: retain validated words for load-time preparation.
  std::uint32_t channel_mask{};
  std::span<const std::uint8_t> record;
  std::array<std::optional<MotionChannel>, 3> channels;
};
class MotionDefinition {
 public:
  static MotionDefinition parse(std::span<const std::uint8_t> bytes,
                                const std::shared_ptr<memory::Domain>& domain);
  const auto& tracks() const { return tracks_; }
  bool whole_transform_channels() const { return whole_transform_; }
  // Retain authored timing for the animation owner; the decoder does not tick.
  std::span<const std::uint8_t> header() const { return header_; }

 private:
  explicit MotionDefinition(const std::shared_ptr<memory::Domain>& domain)
      : tracks_(memory::Allocator<MotionTrack>(domain)) {}
  bool whole_transform_{};
  std::span<const std::uint8_t> header_;
  memory::Vector<MotionTrack> tracks_;
};
struct ShapeChannel {
  std::string_view name;
  std::optional<MotionChannel> channel;
  float end_frame{};
};
class ShapeAnimationDefinition {
 public:
  static ShapeAnimationDefinition parse(
      std::span<const std::uint8_t> bytes,
      const std::shared_ptr<memory::Domain>& domain);
  const auto& channels() const { return channels_; }
  std::span<const std::uint8_t> header() const { return header_; }

 private:
  explicit ShapeAnimationDefinition(
      const std::shared_ptr<memory::Domain>& domain)
      : channels_(memory::Allocator<ShapeChannel>(domain)) {}
  memory::Vector<ShapeChannel> channels_;
  std::span<const std::uint8_t> header_;
};
struct SkeletonTransform {
  std::array<float, 4> rotation{};
  std::array<float, 3> translation{}, scale{};
};
struct SkeletonBone {
  static constexpr std::size_t kSourceFields = 6;
  std::array<std::uint32_t, kSourceFields> source_fields{};
  std::int32_t parent{-1};
  SkeletonTransform relative, absolute;
  // Original row-vector convention: three basis rows followed by translation.
  std::array<std::array<float, 3>, 4> relative_matrix{}, inverse_bind_matrix{};
};
class SkeletonDefinition {
 public:
  static SkeletonDefinition parse(
      std::span<const std::uint8_t> bytes,
      const std::shared_ptr<memory::Domain>& domain);
  const auto& bones() const { return bones_; }

 private:
  explicit SkeletonDefinition(const std::shared_ptr<memory::Domain>& domain)
      : bones_(memory::Allocator<SkeletonBone>(domain)) {}
  memory::Vector<SkeletonBone> bones_;
};
}  // namespace bw::formats
