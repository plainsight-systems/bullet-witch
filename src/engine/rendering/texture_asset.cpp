#include "bw/rendering/texture_asset.hpp"

#include "bw/rendering/rhi/format.hpp"
#include "bw/rendering/rhi/resources.hpp"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>

// Responsibility: the out-of-line part of bw/rendering/texture_asset.hpp:
// the payload decoder, the view's subresource entries and its RHI
// description.
//
// Contract (the header's): the first failure in TextureAssetError order is
// named, each check a pass over every subresource before the next check
// runs. Records are copied out of the bytes; nothing is read before the
// bytes it lives in are known to be within the payload.
//
// Readings of the header that the checks encode:
//   Contract 1 is checked in four passes: the record table's alignment, the
//   table's bounds, then every subresource's alignment, then every
//   subresource's bounds by its stored size. A subresource cannot be read
//   until the table is known to be within the payload, so a table failure
//   is reported with index 0 before any subresource is looked at.
//   The subresource count is checked against faces * mip_levels after the
//   extent and mip checks, as the enumeration orders it; the bounds passes
//   before it use the stored count, which the table's bounds check covers.
//   texture_description leaves debug_name empty and initial_access Unknown,
//   as the header states; the caller sets both on the returned value.
namespace bw::rendering {

namespace {

constexpr std::uint64_t kAlignment = kTextureSectionAlignment;

template <class Record>
Record read_record(std::span<const std::byte> payload, std::uint64_t offset) noexcept {
  Record record{};
  std::memcpy(&record, payload.data() + offset, sizeof(Record));
  return record;
}

bool within(std::uint64_t offset, std::uint64_t size, std::uint64_t limit) noexcept {
  return offset <= limit && size <= limit - offset;
}

SubresourceRecord subresource_record(std::span<const std::byte> payload,
                                     const TexturePayloadHeader& header,
                                     std::uint32_t index) noexcept {
  return read_record<SubresourceRecord>(
      payload,
      header.subresources_offset + (static_cast<std::uint64_t>(index) * sizeof(SubresourceRecord)));
}

} // namespace

rhi::SubresourceData TextureAssetView::subresource(std::uint32_t index) const noexcept {
  const SubresourceRecord record = subresource_record(payload_, header_, index);
  return rhi::SubresourceData{payload_.subspan(record.offset, record.size), record.row_pitch, 0};
}

rhi::TextureDescription TextureAssetView::texture_description() const noexcept {
  rhi::TextureDescription description{};
  description.extent = rhi::Extent3{header_.width, header_.height, 1};
  description.array_size = 1;
  // Validated by decode: at most full_mip_count of kMaxTextureExtent, 15.
  description.mip_levels = static_cast<std::uint8_t>(header_.mip_levels);
  description.sample_count = 1;
  description.dimension = dimension() == TextureAssetDimension::Cube
                              ? rhi::TextureDimension::TextureCube
                              : rhi::TextureDimension::Texture2D;
  description.format = to_rhi_format(format());
  description.usage = rhi::TextureUsage::ShaderResource;
  return description;
}

TextureAssetFailure decode_texture_asset(std::span<const std::byte> payload,
                                         TextureAssetView& view) noexcept {
  using E = TextureAssetError;
  const std::uint64_t size = payload.size();
  if (size < sizeof(TexturePayloadHeader)) {
    return {E::HeaderTruncated, 0};
  }
  const auto header = read_record<TexturePayloadHeader>(payload, 0);
  if (header.subresources_offset % kAlignment != 0U) {
    return {E::SectionMisaligned, 0};
  }
  const std::uint64_t table_bytes =
      static_cast<std::uint64_t>(header.subresource_count) * sizeof(SubresourceRecord);
  if (!within(header.subresources_offset, table_bytes, size)) {
    return {E::SectionOutOfBounds, 0};
  }
  for (std::uint32_t i = 0; i < header.subresource_count; ++i) {
    if (subresource_record(payload, header, i).offset % kAlignment != 0U) {
      return {E::SectionMisaligned, i};
    }
  }
  for (std::uint32_t i = 0; i < header.subresource_count; ++i) {
    const SubresourceRecord record = subresource_record(payload, header, i);
    if (!within(record.offset, record.size, size)) {
      return {E::SectionOutOfBounds, i};
    }
  }
  if (!is_known_texture_format(header.format)) {
    return {E::FormatUnknown, 0};
  }
  const bool is_2d =
      header.dimension == static_cast<std::uint32_t>(TextureAssetDimension::Texture2D);
  const bool is_cube = header.dimension == static_cast<std::uint32_t>(TextureAssetDimension::Cube);
  if (!is_2d && !is_cube) {
    return {E::DimensionInvalid, 0};
  }
  if ((is_2d && header.faces != 1U) ||
      (is_cube && (header.faces != 6U || header.width != header.height))) {
    return {E::FaceCountInvalid, 0};
  }
  if (header.width == 0U || header.height == 0U || header.width > kMaxTextureExtent ||
      header.height > kMaxTextureExtent) {
    return {E::ExtentInvalid, 0};
  }
  const rhi::Extent2 base{header.width, header.height};
  if (header.mip_levels != full_mip_count(base)) {
    return {E::MipCountInvalid, 0};
  }
  if (static_cast<std::uint64_t>(header.subresource_count) !=
      static_cast<std::uint64_t>(header.faces) * header.mip_levels) {
    return {E::SubresourceCountMismatch, 0};
  }
  const rhi::Format format = to_rhi_format(static_cast<TextureFormat>(header.format));
  for (std::uint32_t i = 0; i < header.subresource_count; ++i) {
    const rhi::Extent2 extent = mip_extent(base, i % header.mip_levels);
    if (subresource_record(payload, header, i).size != rhi::slice_bytes(format, extent)) {
      return {E::SubresourceSizeMismatch, i};
    }
  }
  for (std::uint32_t i = 0; i < header.subresource_count; ++i) {
    const rhi::Extent2 extent = mip_extent(base, i % header.mip_levels);
    if (subresource_record(payload, header, i).row_pitch != rhi::row_bytes(format, extent.width)) {
      return {E::RowPitchMismatch, i};
    }
  }
  if ((header.flags & ~kKnownTextureFlags) != 0U) {
    return {E::FlagsUnknown, 0};
  }
  view.payload_ = payload;
  view.header_ = header;
  return {E::None, 0};
}

} // namespace bw::rendering
