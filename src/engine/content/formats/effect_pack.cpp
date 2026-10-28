// Validate original fixed-stride effect tables and decode bounded values.
// Borrowed source lifetime, errors and costs are defined in effect_pack.hpp.
// No simulation, texture loading, file access or ownership transfer occurs.
// Core SL.con.3 (#rsl-bounds) validates size before multiplication/slicing;
// I.11 (#ri-raw) preserves const borrowed views. Performance MEM.9
// (guidelines/memory/MEM.9-embedded-allocate-at-init.md) avoids allocation;
// CDSA.32 (guidelines/cpu-dsa/
// CDSA.32-transform-static-data-into-the-consumer-layout.md) validates shared
// static inputs once before their consumers prepare runtime state.
#include "bw/content/formats/effect_pack.hpp"

#include <bit>
#include <cmath>

#include "bw/content/formats/binary_reader.hpp"

namespace bw::formats {
namespace {
using resources::detail::malformed;
constexpr std::size_t kHeaderBytes = 40, kSectionHeaderBytes = 16;
constexpr std::size_t kWordBytes = 4, kSectionCount = 7;
constexpr std::size_t kOptionalSectionHeaderBytes = 12;
constexpr std::array<std::size_t, 5> kStrides{0xb0, 0x210, 0xd4, 0x84, 0x180};
constexpr std::size_t kDefinitionReference = 4;
constexpr std::size_t kVectorReference = 0xa4;
constexpr std::array<std::size_t, 2> kScalarReferences{0xa8, 0xac};
constexpr std::uint32_t kFormatVersion = 0x9e;
constexpr std::size_t kVersionOffset = 8;
constexpr std::int32_t kMaximumKeys = 16;

void reference(std::int32_t value, std::size_t count) {
  if (value >= 0 && static_cast<std::size_t>(value) >= count) {
    malformed("Effect pack reference exceeds table");
  }
}
}  // namespace

std::uint32_t EffectRecordView::word(std::size_t byte_offset) const {
  if (byte_offset % kWordBytes != 0) {
    malformed("Unaligned effect record word");
  }
  return resources::detail::Reader{bytes_}.u32(byte_offset);
}
std::int32_t EffectRecordView::signed_word(std::size_t byte_offset) const {
  return std::bit_cast<std::int32_t>(word(byte_offset));
}
float EffectRecordView::scalar(std::size_t byte_offset) const {
  return std::bit_cast<float>(word(byte_offset));
}

EffectPackView::EffectPackView(std::span<const std::uint8_t> bytes)
    : bytes_(bytes) {
  const resources::detail::Reader reader{bytes};
  reader.range(0, kHeaderBytes);
  if (reader.tag_view(0) != "KPKd" || reader.u32(4) != kSectionCount ||
      reader.u32(kVersionOffset) != kFormatVersion) {
    malformed("Invalid effect pack header");
  }
  // Bound each table by its next physical section, not merely end-of-file.
  std::array<std::size_t, kSectionCount> all_offsets{};
  for (std::size_t section = 0; section < kSectionCount; ++section) {
    const auto offset = reader.u32(12 + section * kWordBytes);
    all_offsets.at(section) = offset;
    if (offset != 0) {
      if (offset < kHeaderBytes) malformed("Effect section overlaps header");
      reader.range(offset, section < offsets_.size()
                               ? kSectionHeaderBytes
                               : kOptionalSectionHeaderBytes);
      for (std::size_t previous = 0; previous < section; ++previous) {
        if (all_offsets.at(previous) == offset) {
          malformed("Effect sections alias");
        }
      }
    }
  }
  for (std::size_t section = 0; section < offsets_.size(); ++section) {
    const auto offset = all_offsets.at(section);
    if (offset == 0) malformed("Missing effect table");
    auto end = bytes.size();
    for (const auto other : all_offsets) {
      if (other > offset && other < end) end = other;
    }
    if (end - offset < kSectionHeaderBytes) {
      malformed("Overlapping effect section headers");
    }
    const auto count = reader.u32(offset + 8);
    const auto available = end - offset - kSectionHeaderBytes;
    if (count > available / kStrides.at(section)) {
      malformed("Effect table exceeds section");
    }
    offsets_.at(section) = offset + kSectionHeaderBytes;
    counts_.at(section) = count;
  }
  for (std::size_t index = 0; index < effect_count(); ++index) {
    for (const auto value : effect(index).parts) reference(value, part_count());
  }
  for (std::size_t index = 0; index < part_count(); ++index) {
    const auto value = part(index).signed_word(kDefinitionReference);
    if (value < 0) malformed("Effect part has no definition");
    reference(value, definition_count());
  }
  for (std::size_t index = 0; index < definition_count(); ++index) {
    const auto value = definition(index);
    reference(value.signed_word(kVectorReference), vector_curve_count());
    for (const auto offset : kScalarReferences) {
      reference(value.signed_word(offset), scalar_curve_count());
    }
  }
  for (std::size_t index = 0; index < scalar_curve_count(); ++index) {
    const auto curve = scalar_curve(index);
    if (curve.mode > kMaximumKeys) malformed("Too many effect curve keys");
    for (const auto value : curve.values) {
      if (!std::isfinite(value)) malformed("Nonfinite effect curve sample");
    }
    // Positive keyed curves require strictly increasing adjacent key times.
    for (std::int32_t key = 1; key < curve.mode; ++key) {
      const auto offset = static_cast<std::size_t>(key) * 2;
      if (curve.values.at(offset) <= curve.values.at(offset - 2)) {
        malformed("Effect curve key times are not increasing");
      }
    }
  }
}

EffectRecordView EffectPackView::record(std::size_t section,
                                        std::size_t index) const {
  if (index >= counts_.at(section)) {
    malformed("Effect record index out of range");
  }
  const auto stride = kStrides.at(section);
  return EffectRecordView(
      bytes_.subspan(offsets_.at(section) + index * stride, stride));
}
EffectRecordView EffectPackView::effect_record(std::size_t index) const {
  return record(0, index);
}
EffectRecordView EffectPackView::part(std::size_t index) const {
  return record(1, index);
}
EffectRecordView EffectPackView::definition(std::size_t index) const {
  return record(2, index);
}
EffectEntry EffectPackView::effect(std::size_t index) const {
  const auto value = effect_record(index);
  EffectEntry result{.id = value.word(0)};
  for (std::size_t slot = 0; slot < result.parts.size(); ++slot) {
    result.parts.at(slot) = value.signed_word(kWordBytes * (slot + 1));
  }
  return result;
}
EffectScalarCurve EffectPackView::scalar_curve(std::size_t index) const {
  const auto value = record(3, index);
  EffectScalarCurve result{.mode = value.signed_word(0)};
  for (std::size_t sample = 0; sample < result.values.size(); ++sample) {
    result.values.at(sample) = value.scalar(kWordBytes * (sample + 1));
  }
  return result;
}
EffectVectorCurve EffectPackView::vector_curve(std::size_t index) const {
  const auto value = record(4, index);
  EffectVectorCurve result{};
  for (std::size_t sample = 0; sample < result.size(); ++sample) {
    for (std::size_t axis = 0; axis < result.at(sample).size(); ++axis) {
      result.at(sample).at(axis) =
          value.scalar((sample * 3 + axis) * kWordBytes);
    }
  }
  return result;
}
std::optional<std::size_t> EffectPackView::find_effect(std::uint32_t id) const {
  for (std::size_t index = effect_count(); index > 0; --index) {
    if (effect_record(index - 1).word(0) == id) return index - 1;
  }
  return std::nullopt;
}
}  // namespace bw::formats
