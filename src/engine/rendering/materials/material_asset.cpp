#include "bw/rendering/materials/material_asset.hpp"

#include "bw/content/asset_id.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <string_view>

// Responsibility: the out-of-line part of
// bw/rendering/materials/material_asset.hpp: the payload decoder, the view's
// record reads and its lookups by name.
//
// Contract (the header's): the first failure in MaterialAssetError order is
// named, each check a pass over every table (switches, scalars, vectors,
// textures, unmapped, the header's table numbers 0 to 4) before the next
// check runs. A failure of the string table's section or of the template
// name reports table 5. Records are copied out of the bytes; nothing is
// read before the bytes it lives in are known to be within the payload.
//
// Readings of the header that the checks encode:
//   An unmapped field's value slice may be empty (an original field with no
//   text) but must lie within the string table, since string() is promised
//   valid for every slice decode accepted; a value outside the table is
//   NameInvalid for that record, the error for a record's text slices.
//   A texture id slice outside the string table is TextureIdInvalid, like an
//   id that does not validate.
//   Lookups are linear in record order: the tables hold a dozen entries.
namespace bw::materials {

namespace {

constexpr std::uint64_t kAlignment = kMaterialSectionAlignment;

constexpr std::uint32_t kSwitchTable = 0;
constexpr std::uint32_t kScalarTable = 1;
constexpr std::uint32_t kVectorTable = 2;
constexpr std::uint32_t kTextureTable = 3;
constexpr std::uint32_t kUnmappedTable = 4;
constexpr std::uint32_t kTableCount = 5;
// Reported for the string table and the template name; it has no records.
constexpr std::uint32_t kStringTable = 5;

template <class Record>
Record read_record(std::span<const std::byte> payload, std::uint64_t offset) noexcept {
  Record record{};
  std::memcpy(&record, payload.data() + offset, sizeof(Record));
  return record;
}

bool within(std::uint64_t offset, std::uint64_t size, std::uint64_t limit) noexcept {
  return offset <= limit && size <= limit - offset;
}

struct Table {
  std::uint32_t offset{};
  std::uint32_t count{};
  std::uint64_t record_bytes{};
};

std::array<Table, kTableCount> tables(const MaterialPayloadHeader& h) noexcept {
  return {Table{h.switches_offset, h.switch_count, sizeof(SwitchRecord)},
          Table{h.scalars_offset, h.scalar_count, sizeof(ScalarRecord)},
          Table{h.vectors_offset, h.vector_count, sizeof(VectorRecord)},
          Table{h.textures_offset, h.texture_count, sizeof(TextureRecord)},
          Table{h.unmapped_offset, h.unmapped_count, sizeof(UnmappedFieldRecord)}};
}

std::uint64_t record_offset(const Table& table, std::uint32_t index) noexcept {
  return table.offset + (static_cast<std::uint64_t>(index) * table.record_bytes);
}

struct Slice {
  std::uint32_t offset{};
  std::uint32_t length{};
};

// The name slice of record `index` of table `table`, read through the
// table's own record type.
Slice name_slice(std::span<const std::byte> payload, const MaterialPayloadHeader& header,
                 std::uint32_t table, std::uint32_t index) noexcept {
  const std::uint64_t offset = record_offset(tables(header)[table], index);
  switch (table) {
  case kSwitchTable: {
    const auto r = read_record<SwitchRecord>(payload, offset);
    return {r.name_offset, r.name_length};
  }
  case kScalarTable: {
    const auto r = read_record<ScalarRecord>(payload, offset);
    return {r.name_offset, r.name_length};
  }
  case kVectorTable: {
    const auto r = read_record<VectorRecord>(payload, offset);
    return {r.name_offset, r.name_length};
  }
  case kTextureTable: {
    const auto r = read_record<TextureRecord>(payload, offset);
    return {r.name_offset, r.name_length};
  }
  default: {
    const auto r = read_record<UnmappedFieldRecord>(payload, offset);
    return {r.name_offset, r.name_length};
  }
  }
}

std::string_view text(std::span<const std::byte> payload, const MaterialPayloadHeader& header,
                      Slice slice) noexcept {
  const std::span<const std::byte> bytes =
      payload.subspan(static_cast<std::size_t>(header.strings_offset) + slice.offset, slice.length);
  // Viewing bytes as characters is the one cast the language allows for
  // this (char may alias any object).
  return {reinterpret_cast<const char*>(bytes.data()), bytes.size()};
}

bool slice_valid(Slice slice, const MaterialPayloadHeader& header) noexcept {
  return within(slice.offset, slice.length, header.string_bytes);
}

} // namespace

std::string_view MaterialAssetView::template_name() const noexcept {
  return text(payload_, header_, {header_.template_name_offset, header_.template_name_length});
}

SwitchRecord MaterialAssetView::switch_record(std::uint32_t index) const noexcept {
  return read_record<SwitchRecord>(payload_, record_offset(tables(header_)[kSwitchTable], index));
}
ScalarRecord MaterialAssetView::scalar_record(std::uint32_t index) const noexcept {
  return read_record<ScalarRecord>(payload_, record_offset(tables(header_)[kScalarTable], index));
}
VectorRecord MaterialAssetView::vector_record(std::uint32_t index) const noexcept {
  return read_record<VectorRecord>(payload_, record_offset(tables(header_)[kVectorTable], index));
}
TextureRecord MaterialAssetView::texture_record(std::uint32_t index) const noexcept {
  return read_record<TextureRecord>(payload_, record_offset(tables(header_)[kTextureTable], index));
}
UnmappedFieldRecord MaterialAssetView::unmapped_record(std::uint32_t index) const noexcept {
  return read_record<UnmappedFieldRecord>(payload_,
                                          record_offset(tables(header_)[kUnmappedTable], index));
}

std::string_view MaterialAssetView::string(std::uint32_t offset,
                                           std::uint32_t length) const noexcept {
  return text(payload_, header_, {offset, length});
}

MaterialAssetView::TextureLookup
MaterialAssetView::find_texture(std::string_view name) const noexcept {
  for (std::uint32_t i = 0; i < header_.texture_count; ++i) {
    const TextureRecord r = texture_record(i);
    if (string(r.name_offset, r.name_length) == name) {
      return {string(r.id_offset, r.id_length), true};
    }
  }
  return {};
}

MaterialAssetView::ScalarLookup
MaterialAssetView::find_scalar(std::string_view name) const noexcept {
  for (std::uint32_t i = 0; i < header_.scalar_count; ++i) {
    const ScalarRecord r = scalar_record(i);
    if (string(r.name_offset, r.name_length) == name) {
      return {r.value, true};
    }
  }
  return {};
}

MaterialAssetView::VectorLookup
MaterialAssetView::find_vector(std::string_view name) const noexcept {
  for (std::uint32_t i = 0; i < header_.vector_count; ++i) {
    const VectorRecord r = vector_record(i);
    if (string(r.name_offset, r.name_length) == name) {
      return {r.value, true};
    }
  }
  return {};
}

MaterialAssetView::SwitchLookup
MaterialAssetView::find_switch(std::string_view name) const noexcept {
  for (std::uint32_t i = 0; i < header_.switch_count; ++i) {
    const SwitchRecord r = switch_record(i);
    if (string(r.name_offset, r.name_length) == name) {
      return {r.value == 1U, true};
    }
  }
  return {};
}

MaterialAssetFailure decode_material_asset(std::span<const std::byte> payload,
                                           MaterialAssetView& view) noexcept {
  using E = MaterialAssetError;
  const std::uint64_t size = payload.size();
  if (size < sizeof(MaterialPayloadHeader)) {
    return {E::HeaderTruncated, 0, 0};
  }
  const auto header = read_record<MaterialPayloadHeader>(payload, 0);
  const std::array<Table, kTableCount> all = tables(header);
  for (std::uint32_t t = 0; t < kTableCount; ++t) {
    if (all[t].offset % kAlignment != 0U) {
      return {E::SectionMisaligned, t, 0};
    }
  }
  if (header.strings_offset % kAlignment != 0U) {
    return {E::SectionMisaligned, kStringTable, 0};
  }
  for (std::uint32_t t = 0; t < kTableCount; ++t) {
    if (!within(all[t].offset, all[t].count * all[t].record_bytes, size)) {
      return {E::SectionOutOfBounds, t, 0};
    }
  }
  if (!within(header.strings_offset, header.string_bytes, size)) {
    return {E::SectionOutOfBounds, kStringTable, 0};
  }
  const Slice template_name{header.template_name_offset, header.template_name_length};
  if (template_name.length == 0U || !slice_valid(template_name, header)) {
    return {E::TemplateNameInvalid, kStringTable, 0};
  }
  for (std::uint32_t t = 0; t < kTableCount; ++t) {
    for (std::uint32_t i = 0; i < all[t].count; ++i) {
      const Slice name = name_slice(payload, header, t, i);
      if (name.length == 0U || !slice_valid(name, header)) {
        return {E::NameInvalid, t, i};
      }
      if (t == kUnmappedTable) {
        const auto r = read_record<UnmappedFieldRecord>(payload, record_offset(all[t], i));
        if (!slice_valid({r.value_offset, r.value_length}, header)) {
          return {E::NameInvalid, t, i};
        }
      }
    }
  }
  for (std::uint32_t t = 0; t < kTableCount; ++t) {
    for (std::uint32_t i = 1; i < all[t].count; ++i) {
      const std::string_view name = text(payload, header, name_slice(payload, header, t, i));
      for (std::uint32_t j = 0; j < i; ++j) {
        if (text(payload, header, name_slice(payload, header, t, j)) == name) {
          return {E::NameDuplicate, t, i};
        }
      }
    }
  }
  for (std::uint32_t i = 0; i < header.switch_count; ++i) {
    if (read_record<SwitchRecord>(payload, record_offset(all[kSwitchTable], i)).value > 1U) {
      return {E::SwitchValueInvalid, kSwitchTable, i};
    }
  }
  for (std::uint32_t i = 0; i < header.texture_count; ++i) {
    const auto r = read_record<TextureRecord>(payload, record_offset(all[kTextureTable], i));
    const Slice id{r.id_offset, r.id_length};
    if (!slice_valid(id, header) ||
        content::validate_asset_id(text(payload, header, id)) != content::AssetIdError::None) {
      return {E::TextureIdInvalid, kTextureTable, i};
    }
  }
  view.payload_ = payload;
  view.header_ = header;
  return {E::None, 0, 0};
}

} // namespace bw::materials
