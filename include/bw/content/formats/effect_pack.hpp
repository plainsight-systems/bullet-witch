// Original effect-pack schema boundary. The view borrows immutable caller-owned
// bytes; every returned record borrows that same owner. Keep the payload alive
// and unchanged until all views retire. No file I/O, allocations or mutation.
// Construction validates section framing, record extents and effect/part/curve
// references; malformed input throws resources::Error. Unknown semantic fields
// remain bounded original words, not fabricated runtime behavior. Algorithm:
// validate header -> five fixed-stride tables -> cross references -> expose
// indexed records. O(records + references) construction, O(1) record access.
// Design Core I.11 (#ri-raw) makes borrowing explicit; performance MEM.9
// (guidelines/memory/MEM.9-embedded-allocate-at-init.md) leaves allocation with
// the resource owner. Implementation Core SL.con.3 (#rsl-bounds) checks every
// extent; performance CDSA.32 (guidelines/cpu-dsa/
// CDSA.32-transform-static-data-into-the-consumer-layout.md) retains one shared
// immutable source view for load-time preparation. See first-combat-effects.md.
#pragma once

#include "bw/content/formats/common.hpp"

namespace bw::formats {
inline constexpr std::size_t kEffectPartSlots = 16;
inline constexpr std::size_t kEffectCurveSamples = 32;

// Offset access exists only at this original-format compatibility boundary.
// It permits traced consumers to retain uninterpreted fields without assigning
// guessed engine semantics. Offset must be word aligned and within this record.
class EffectRecordView {
 public:
  std::uint32_t word(std::size_t byte_offset) const;
  std::int32_t signed_word(std::size_t byte_offset) const;
  float scalar(std::size_t byte_offset) const;
  std::span<const std::uint8_t> bytes() const noexcept { return bytes_; }

 private:
  friend class EffectPackView;
  explicit EffectRecordView(std::span<const std::uint8_t> bytes)
      : bytes_(bytes) {}
  std::span<const std::uint8_t> bytes_;
};

struct EffectEntry {
  std::uint32_t id{};
  std::array<std::int32_t, kEffectPartSlots> parts{};
};
struct EffectScalarCurve {
  // Positive mode counts time/value pairs. Nonpositive mode uses 32 samples.
  std::int32_t mode{};
  std::array<float, kEffectCurveSamples> values{};
};
using EffectVectorCurve = std::array<std::array<float, 3>, kEffectCurveSamples>;

class EffectPackView {
 public:
  explicit EffectPackView(std::span<const std::uint8_t> bytes);
  std::size_t effect_count() const noexcept { return counts_[0]; }
  std::size_t part_count() const noexcept { return counts_[1]; }
  std::size_t definition_count() const noexcept { return counts_[2]; }
  std::size_t scalar_curve_count() const noexcept { return counts_[3]; }
  std::size_t vector_curve_count() const noexcept { return counts_[4]; }
  EffectEntry effect(std::size_t index) const;
  EffectRecordView effect_record(std::size_t index) const;
  EffectRecordView part(std::size_t index) const;
  EffectRecordView definition(std::size_t index) const;
  EffectScalarCurve scalar_curve(std::size_t index) const;
  EffectVectorCurve vector_curve(std::size_t index) const;
  // Original binder writes in entry order; duplicate IDs resolve to the last.
  std::optional<std::size_t> find_effect(std::uint32_t id) const;

 private:
  EffectRecordView record(std::size_t section, std::size_t index) const;
  std::span<const std::uint8_t> bytes_;
  static constexpr std::size_t kFixedSections = 5;
  std::array<std::size_t, kFixedSections> offsets_{}, counts_{};
};
}  // namespace bw::formats
