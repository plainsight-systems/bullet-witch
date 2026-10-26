// Load-time event timing preparation; ownership, limits and failure contracts
// are in motion_events.hpp. Preserve source channel/record order and opaque
// payloads. Implementation: Core SL.con.3 (#rsl-bounds) validates directories
// before slicing; Performance CDSA.32 (guidelines/cpu-dsa/
// CDSA.32-transform-static-data-into-the-consumer-layout.md) prepares
// contiguous timing records once for runtime reads. Both corpora consulted in
// each phase.
#include "bw/content/formats/motion_events.hpp"

#include <bit>

#include "bw/content/formats/binary_reader.hpp"

namespace bw::formats {
namespace {
using resources::detail::malformed;
using resources::detail::Reader;
using resources::detail::unsupported;
struct Layout {
  std::uint32_t tag;
  std::uint32_t stride, start, end;
};
constexpr std::uint32_t kNoEnd = 0;
constexpr std::array<Layout, kMotionEventChannels> kLayouts{{
    {0x53455f30, 52, 40, 44},
    {0x564f5f30, 52, 40, 44},
    {0x45464630, 52, 40, 44},
    {0x46414330, 48, 36, 40},
    {kCancellationEventTag, 36, 32, kNoEnd},
    {0x494e5430, 40, 32, 36},
    {0x43415230, 40, 32, 36},
    {0x4b414b30, 52, 44, 48},
    {0x56495330, 40, 32, kNoEnd},
    {0x48495430, 52, 44, 48},
}};
constexpr std::uint64_t kParameterHeader = 44, kSectionHeader = 12;
constexpr std::uint64_t kCountOffset = 4, kRecordsOffset = 8;
constexpr std::uint32_t kParameterMagic = 0x30415046;
}  // namespace
MotionEvents MotionEvents::parse(
    std::span<const std::uint8_t> bytes,
    const std::shared_ptr<memory::Domain>& domain) {
  const Reader reader{bytes};
  reader.range(0, kParameterHeader);
  if (reader.u32(0) != kParameterMagic) {
    unsupported("Unsupported motion event encoding");
  }
  MotionEvents result(domain);
  std::array<std::uint32_t, kMotionEventChannels> tables{};
  std::size_t total = 0;
  // Validate every channel before allocating or publishing timing metadata.
  for (std::size_t channel = 0; channel < kLayouts.size(); ++channel) {
    result.offsets_.at(channel) = total;
    const auto section = reader.u32((channel + 1) * sizeof(std::uint32_t));
    if (section == 0) continue;
    reader.range(section, kSectionHeader);
    const auto& layout = kLayouts.at(channel);
    if (reader.u32(section) != layout.tag) {
      unsupported("Unexpected motion event channel tag");
    }
    const auto count = reader.u32(section + kCountOffset);
    if (count > kMotionEventsPerChannel) {
      malformed("Motion event channel exceeds source playback capacity");
    }
    tables.at(channel) = reader.u32(section + kRecordsOffset);
    reader.range(tables.at(channel),
                 static_cast<std::uint64_t>(count) * layout.stride);
    result.counts_.at(channel) = count;
    total += count;
  }
  result.records_.reserve(total);
  for (std::size_t channel = 0; channel < kLayouts.size(); ++channel) {
    const auto& layout = kLayouts.at(channel);
    for (std::size_t record = 0; record < result.counts_.at(channel);
         ++record) {
      const auto offset = static_cast<std::uint64_t>(tables.at(channel)) +
                          record * layout.stride;
      const auto start =
          std::bit_cast<std::int32_t>(reader.u32(offset + layout.start));
      const auto end =
          layout.end == kNoEnd
              ? -1
              : std::bit_cast<std::int32_t>(reader.u32(offset + layout.end));
      result.records_.push_back(
          {start, end,
           bytes.subspan(static_cast<std::size_t>(offset), layout.stride)});
    }
  }
  return result;
}
MotionEventChannels MotionEvents::channels() const {
  MotionEventChannels result{};
  const std::span<const MotionEvent> records(records_);
  for (std::size_t channel = 0; channel < result.size(); ++channel) {
    result.at(channel) = {
        kLayouts.at(channel).tag,
        records.subspan(offsets_.at(channel), counts_.at(channel))};
  }
  return result;
}
}  // namespace bw::formats
