// Decode immutable weighted frames; contracts and both-phase I.1/SL.con.3/MEM.9
// are in control_points.hpp. Pointer fixups become checked offsets, and the
// animation owner receives only validated values with explicit absent slots.
#include "bw/formats/control_points.hpp"

#include <bit>
#include <cmath>

#include "reader.hpp"
namespace bw::formats {
namespace {
using resources::detail::malformed;
using resources::detail::Reader;
using resources::detail::unsupported;
constexpr std::uint64_t kWord = 4, kHeader = 12, kRecordHeader = 8;
constexpr std::uint32_t kMagic = 0x4250435f;
float number(const Reader& reader, std::uint64_t offset) {
  const auto value = std::bit_cast<float>(reader.u32(offset));
  if (!std::isfinite(value)) malformed("Nonfinite control-point value");
  return value;
}
}  // namespace
ControlPointDefinition ControlPointDefinition::parse(
    std::span<const std::uint8_t> bytes, std::size_t bones,
    const std::shared_ptr<memory::Domain>& domain) {
  const Reader reader{bytes};
  reader.range(0, kHeader);
  if (reader.u32(0) != kMagic) {
    unsupported("Unsupported control-point encoding");
  }
  const auto present_count = reader.u32(kWord), slots = reader.u32(2 * kWord);
  reader.range(kHeader, static_cast<std::uint64_t>(slots) * kWord);
  ControlPointDefinition result(domain);
  result.frames_.resize(slots);
  std::size_t present = 0;
  for (std::size_t slot = 0; slot < slots; ++slot) {
    const std::uint64_t offset = reader.u32(kHeader + slot * kWord);
    if (offset == 0) continue;
    if (offset < kHeader + static_cast<std::uint64_t>(slots) * kWord) {
      malformed("Control-point record overlaps directory");
    }
    reader.range(offset, kRecordHeader);
    const auto points = reader.u32(offset), count = reader.u32(offset + kWord);
    if (points != 3) unsupported("Control-point frame requires three points");
    constexpr std::uint64_t kPointBytes = std::uint64_t{3} * 3 * kWord;
    const auto indices = offset + kRecordHeader + kPointBytes;
    reader.range(offset + kRecordHeader,
                 kPointBytes + static_cast<std::uint64_t>(count) * 2 * kWord);
    auto& frame = result.frames_.at(slot);
    frame.present = true;
    frame.first_influence = result.influences_.size();
    frame.influence_count = count;
    for (std::size_t point = 0; point < frame.points.size(); ++point) {
      for (std::size_t axis = 0; axis < 3; ++axis) {
        frame.points.at(point).at(axis) =
            number(reader, offset + kRecordHeader + (point * 3 + axis) * kWord);
      }
    }
    for (std::size_t index = 0; index < count; ++index) {
      const auto bone = reader.u32(indices + index * kWord);
      const auto weight = number(reader, indices + (count + index) * kWord);
      if (bone >= bones || weight < 0) {
        malformed("Invalid control-point influence");
      }
      result.influences_.push_back({bone, weight});
    }
    ++present;
  }
  if (present != present_count) {
    malformed("Control-point populated count differs");
  }
  return result;
}
std::span<const ControlPointInfluence> ControlPointDefinition::influences(
    const ControlPointFrame& frame) const {
  if (frame.first_influence > influences_.size() ||
      frame.influence_count > influences_.size() - frame.first_influence) {
    throw std::out_of_range("Control-point influence range");
  }
  return std::span(influences_)
      .subspan(frame.first_influence, frame.influence_count);
}
}  // namespace bw::formats
