// Bounded shape-offset decoding; ownership and costs are in model_shapes.hpp.
// Preserve file name order and borrowed vertex-major storage. Core SL.con.3
// (#rsl-bounds) and Performance CDSA.32 (guidelines/cpu-dsa/CDSA.32-transform-
// static-data-into-the-consumer-layout.md) apply to validate-before-publish
// and load-time metadata preparation; both implementation corpora consulted.
#include "bw/content/formats/model_shapes.hpp"

#include "bw/content/formats/binary_reader.hpp"
namespace bw::formats {
ModelShapeOffsets ModelShapeOffsets::parse(
    std::span<const std::uint8_t> bytes,
    const std::shared_ptr<memory::Domain>& domain) {
  const resources::detail::Reader reader{bytes};
  constexpr std::uint64_t kHeaderBytes = 80, kByteOrderOffset = 4,
                          kVersionOffset = 8, kVertexCountOffset = 12,
                          kChannelCountOffset = 16, kNamesOffset = 72,
                          kDeltasOffset = 76, kNameRecordBytes = 16,
                          kNameBytes = 32, kDeltaBytes = 8;
  constexpr std::uint32_t kByteOrder = 0x11223344, kVersion = 1,
                          kMaximumChannels = 255;
  reader.range(0, kHeaderBytes);
  if (reader.tag_view(0) != "TS2V" ||
      reader.u32(kByteOrderOffset) != kByteOrder ||
      reader.u32(kVersionOffset) != kVersion) {
    resources::detail::unsupported("Unsupported model shape offsets");
  }
  ModelShapeOffsets result(domain);
  result.vertex_count_ = reader.u32(kVertexCountOffset);
  const auto count = reader.u32(kChannelCountOffset);
  if (count > kMaximumChannels) {
    resources::detail::malformed("Shape channel count exceeds source name map");
  }
  const auto names = reader.u32(kNamesOffset),
             deltas = reader.u32(kDeltasOffset);
  reader.range(names, static_cast<std::uint64_t>(count) * kNameRecordBytes);
  const auto delta_size =
      static_cast<std::uint64_t>(result.vertex_count_) * count * kDeltaBytes;
  reader.range(deltas, delta_size);
  result.names_.reserve(count);
  for (std::uint32_t i = 0; i < count; ++i) {
    const auto offset =
        reader.u32(names + static_cast<std::uint64_t>(i) * kNameRecordBytes);
    const auto name = reader.text_view({offset, kNameBytes}, 0);
    if (name.empty()) resources::detail::malformed("Empty model shape name");
    result.names_.push_back(name);
  }
  result.deltas_ = bytes.subspan(deltas, static_cast<std::size_t>(delta_size));
  return result;
}
}  // namespace bw::formats
