// Construct bounded background placements without modifying package storage.
// Decode scalar fields and expand source affine rows once; draw consumers reuse
// retained matrices. Borrowed names require the package owner through last use.
// Header contract and stage-scene-presentation.md give ownership/provenance.
// Both MCP phases C.32 (#rc-dtor-ptr) and MEM.9
// (guidelines/memory/MEM.9-embedded-allocate-at-init.md) keep view ownership
// explicit and all metadata work in loading, never the per-frame path.
#include "bw/content/formats/background_placement.hpp"

#include <bit>
#include <cmath>

#include "bw/content/formats/binary_reader.hpp"
namespace bw::formats {
namespace {
constexpr std::uint64_t kWordBytes = 4;
std::uint16_t half_word(const resources::detail::Reader& reader,
                        std::uint64_t offset) {
  reader.range(offset, 2);
  const auto bytes = reader.b.subspan(static_cast<std::size_t>(offset), 2);
  constexpr unsigned kByteBits = 8;
  return static_cast<std::uint16_t>(bytes[0] | (bytes[1] << kByteBits));
}
float finite_scalar(const resources::detail::Reader& reader,
                    std::uint64_t offset) {
  const auto value = std::bit_cast<float>(reader.u32(offset));
  if (!std::isfinite(value)) {
    resources::detail::malformed("Nonfinite background placement value");
  }
  return value;
}
}  // namespace
BackgroundPlacementDefinition BackgroundPlacementDefinition::parse(
    std::span<const std::uint8_t> bytes,
    const std::shared_ptr<memory::Domain>& domain) {
  const resources::detail::Reader reader{bytes};
  constexpr std::uint64_t kHeaderBytes = 24;
  reader.range(0, kHeaderBytes);
  if (reader.tag_view(0) != "BGOB") {
    resources::detail::unsupported(
        "Unsupported background placement signature");
  }
  constexpr std::uint64_t kMajorOffset = 4, kMinorOffset = 6;
  const auto major = half_word(reader, kMajorOffset);
  const auto minor = half_word(reader, kMinorOffset);
  if (major != 1 || (minor != 1 && minor != 2)) {
    resources::detail::unsupported("Unsupported background placement version");
  }
  constexpr std::uint64_t kNameCountOffset = 8, kNameDirectoryOffset = 12;
  constexpr std::uint64_t kPlacementCountOffset = 16,
                          kPlacementDirectoryOffset = 20;
  const auto name_count = reader.u32(kNameCountOffset);
  const auto name_directory = reader.u32(kNameDirectoryOffset);
  const auto placement_count = reader.u32(kPlacementCountOffset);
  const auto placement_directory = reader.u32(kPlacementDirectoryOffset);
  reader.range(name_directory,
               static_cast<std::uint64_t>(name_count) * kWordBytes);
  reader.range(placement_directory,
               static_cast<std::uint64_t>(placement_count) * kWordBytes);
  BackgroundPlacementDefinition result(domain);
  result.names_.reserve(name_count);
  result.placements_.reserve(placement_count);
  for (std::uint32_t index = 0; index < name_count; ++index) {
    const auto name = reader.text_view(
        {0, bytes.size()}, reader.u32(name_directory + index * kWordBytes));
    if (name.empty()) {
      resources::detail::malformed("Empty background model name");
    }
    result.names_.push_back(name);
  }
  // Validate each prefix before sizing the shared matrix owner. Source order is
  // retained, including repeated model names and a placement's complete
  // palette.
  constexpr std::uint64_t kVectorBytes = 16, kPrefixV1 = 24, kPrefixV2 = 40;
  constexpr std::uint64_t kMatrixBytes = 48;
  const auto prefix = minor == 1 ? kPrefixV1 : kPrefixV2;
  const auto model_offset = minor == 1 ? kVectorBytes : 2 * kVectorBytes;
  std::size_t matrix_count = 0;
  for (std::uint32_t index = 0; index < placement_count; ++index) {
    const auto offset = reader.u32(placement_directory + index * kWordBytes);
    reader.range(offset, prefix);
    BackgroundPlacement placement;
    placement.model = half_word(reader, offset + model_offset);
    placement.group = std::bit_cast<std::int16_t>(
        half_word(reader, offset + model_offset + 2));
    placement.matrix_count = half_word(reader, offset + model_offset + 4);
    if (placement.model >= name_count) {
      resources::detail::malformed(
          "Background placement model index exceeds names");
    }
    reader.range(offset + prefix, placement.matrix_count * kMatrixBytes);
    placement.first_matrix = matrix_count;
    matrix_count += placement.matrix_count;
    for (std::size_t channel = 0; channel < 4; ++channel) {
      placement.primary_parameters.at(channel) =
          reader.u32(offset + channel * kWordBytes);
      if (minor == 2) {
        placement.secondary_parameters.at(channel) =
            reader.u32(offset + kVectorBytes + channel * kWordBytes);
      }
    }
    result.placements_.push_back(placement);
  }
  result.matrices_.reserve(matrix_count);
  for (std::uint32_t index = 0; index < placement_count; ++index) {
    const auto offset = reader.u32(placement_directory + index * kWordBytes);
    for (std::size_t matrix = 0;
         matrix < result.placements_.at(index).matrix_count; ++matrix) {
      PlacementMatrix transform{};
      for (std::size_t row = 0; row < 4; ++row) {
        for (std::size_t column = 0; column < 3; ++column) {
          transform.at(row).at(column) =
              finite_scalar(reader, offset + prefix + matrix * kMatrixBytes +
                                        (row * 3 + column) * kWordBytes);
        }
      }
      transform.back().back() = 1;
      result.matrices_.push_back(transform);
    }
  }
  return result;
}
}  // namespace bw::formats
