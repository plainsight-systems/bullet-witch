// Unit test: public contracts of bw/rendering/texture_asset.hpp; each
// scenario names the declaration it asserts. Payloads are built here by
// copying the header's records to the 16-aligned offsets it states.
#include "bw/rendering/rhi/format.hpp"
#include "bw/rendering/rhi/resources.hpp"
#include "bw/rendering/texture_asset.hpp"
#include "bw/testing/harness.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <string_view>
#include <vector>

namespace {

namespace r = bw::rendering;
namespace rhi = bw::rhi;
using bw::testing::require;
using E = r::TextureAssetError;
using F = r::TextureFormat;

using Bytes = std::vector<std::byte>;

void pad16(Bytes& bytes) { bytes.resize((bytes.size() + 15U) & ~std::size_t{15}); }

template <class T> T read_at(const Bytes& bytes, std::size_t offset) {
  T value{};
  std::memcpy(&value, bytes.data() + offset, sizeof(T));
  return value;
}
template <class T> void write_at(Bytes& bytes, std::size_t offset, const T& value) {
  std::memcpy(bytes.data() + offset, &value, sizeof(T));
}

// (texture_asset.hpp, full_mip_count, mip_extent): the chain runs to 1x1,
// floor(log2(max(w, h))) + 1 levels; each level halves and rounds down to
// at least 1, for square, non-square and non-power-of-two bases.
void mip_arithmetic() {
  struct Count {
    rhi::Extent2 base{};
    std::uint32_t levels{};
  };
  const std::array counts{
      Count{{1, 1}, 1},        Count{{2, 1}, 2},
      Count{{1, 2}, 2},        Count{{3, 5}, 3},
      Count{{256, 256}, 9},    Count{{256, 64}, 9},
      Count{{640, 480}, 10},   Count{{1000, 1}, 10},
      Count{{1024, 1023}, 11}, Count{{r::kMaxTextureExtent, r::kMaxTextureExtent}, 15},
  };
  for (std::size_t i = 0; i < counts.size(); ++i) {
    require(r::full_mip_count(counts[i].base) == counts[i].levels, i, "full_mip_count");
  }
  struct Level {
    rhi::Extent2 base{};
    std::uint32_t level{};
    rhi::Extent2 extent{};
  };
  const std::array levels{
      Level{{1, 1}, 0, {1, 1}},         Level{{640, 480}, 0, {640, 480}},
      Level{{640, 480}, 1, {320, 240}}, Level{{640, 480}, 8, {2, 1}},
      Level{{640, 480}, 9, {1, 1}},     Level{{256, 64}, 7, {2, 1}},
      Level{{256, 64}, 8, {1, 1}},      Level{{5, 3}, 1, {2, 1}},
      Level{{5, 3}, 2, {1, 1}},         Level{{3, 5}, 1, {1, 2}},
  };
  for (std::size_t i = 0; i < levels.size(); ++i) {
    require(r::mip_extent(levels[i].base, levels[i].level) == levels[i].extent, i, "mip_extent");
  }
}

// A texture of the given shape with its full chain; subresource i holds
// bytes of value i + 1 so a span over the wrong bytes is seen.
Bytes lay_out(F format, r::TextureAssetDimension dimension, rhi::Extent2 base,
              std::uint32_t faces) {
  const std::uint32_t width = base.width;
  const std::uint32_t height = base.height;
  r::TexturePayloadHeader h{};
  h.width = width;
  h.height = height;
  h.faces = faces;
  h.mip_levels = r::full_mip_count({width, height});
  h.format = static_cast<std::uint32_t>(format);
  h.dimension = static_cast<std::uint32_t>(dimension);
  h.subresource_count = faces * h.mip_levels;
  Bytes bytes(sizeof(r::TexturePayloadHeader));
  pad16(bytes);
  h.subresources_offset = static_cast<std::uint32_t>(bytes.size());
  std::vector<r::SubresourceRecord> records(h.subresource_count);
  bytes.resize(bytes.size() + (records.size() * sizeof(r::SubresourceRecord)));
  for (std::uint32_t i = 0; i < h.subresource_count; ++i) {
    const rhi::Extent2 extent = r::mip_extent({width, height}, i % h.mip_levels);
    pad16(bytes);
    records[i].offset = static_cast<std::uint32_t>(bytes.size());
    records[i].size =
        static_cast<std::uint32_t>(rhi::slice_bytes(r::to_rhi_format(format), extent));
    records[i].row_pitch = rhi::row_bytes(r::to_rhi_format(format), extent.width);
    bytes.resize(bytes.size() + records[i].size, static_cast<std::byte>(i + 1U));
  }
  pad16(bytes);
  for (std::uint32_t i = 0; i < h.subresource_count; ++i) {
    write_at(bytes, h.subresources_offset + (std::size_t{i} * sizeof(r::SubresourceRecord)),
             records[i]);
  }
  write_at(bytes, 0, h);
  return bytes;
}

r::TexturePayloadHeader header_of(const Bytes& b) { return read_at<r::TexturePayloadHeader>(b, 0); }
void patch_header(Bytes& b, const std::function<void(r::TexturePayloadHeader&)>& change) {
  auto h = header_of(b);
  change(h);
  write_at(b, 0, h);
}
void patch_record(Bytes& b, std::uint32_t i,
                  const std::function<void(r::SubresourceRecord&)>& change) {
  const std::size_t offset =
      header_of(b).subresources_offset + (std::size_t{i} * sizeof(r::SubresourceRecord));
  auto record = read_at<r::SubresourceRecord>(b, offset);
  change(record);
  write_at(b, offset, record);
}

// Every subresource's entry views its own bytes in the payload with the
// stated row pitch, in slice-major mip-minor order.
void require_subresources(const r::TextureAssetView& view, const Bytes& bytes,
                          std::string_view note) {
  const r::TexturePayloadHeader& h = view.header();
  const rhi::Format format = r::to_rhi_format(view.format());
  for (std::uint32_t i = 0; i < view.subresource_count(); ++i) {
    const rhi::Extent2 extent = r::mip_extent({h.width, h.height}, i % h.mip_levels);
    const rhi::SubresourceData data = view.subresource(i);
    bool filled = data.bytes.size() == rhi::slice_bytes(format, extent);
    for (const std::byte value : data.bytes) {
      filled = filled && value == static_cast<std::byte>(i + 1U);
    }
    const bool within = data.bytes.data() >= bytes.data() &&
                        data.bytes.data() + data.bytes.size() <= bytes.data() + bytes.size();
    require(filled && within && data.row_pitch == rhi::row_bytes(format, extent.width), i, note);
  }
}

// (texture_asset.hpp, decode_texture_asset, TextureAssetError order,
// TextureAssetView::subresource, texture_description): a non-square 2D
// RGBA8 texture and an 8x8 BC7 cube (mip 0 four blocks, the rest one, so a
// face's mips differ in size) decode, their subresource entries view the
// payload's own bytes, and their RHI descriptions state the shape; each row
// then breaks one rule, or two where the order decides, and names the first
// failure, with the subresource index where the failure is a subresource's
// and 0 for a header, table or count failure (TextureAssetFailure::index).
void rejections_named() {
  const Bytes flat = lay_out(F::R8G8B8A8Unorm, r::TextureAssetDimension::Texture2D, {4, 2}, 1);
  const Bytes cube = lay_out(F::BC7UnormSrgb, r::TextureAssetDimension::Cube, {8, 8}, 6);
  {
    r::TextureAssetView view;
    require(r::decode_texture_asset(flat, view).error == E::None, "the 2D payload decodes");
    require(view.subresource_count() == 3U && view.format() == F::R8G8B8A8Unorm &&
                view.dimension() == r::TextureAssetDimension::Texture2D,
            "2D header reads back");
    require_subresources(view, flat, "2D subresource views its bytes");
    const rhi::TextureDescription d = view.texture_description();
    require(d.extent == rhi::Extent3{4, 2, 1} && d.array_size == 1U && d.mip_levels == 3U &&
                d.sample_count == 1U && d.dimension == rhi::TextureDimension::Texture2D &&
                d.format == rhi::Format::R8G8B8A8Unorm &&
                d.usage == rhi::TextureUsage::ShaderResource && rhi::subresource_count(d) == 3U &&
                d.initial_access == rhi::Access::Unknown && d.debug_name.empty(),
            "2D description, initial access and debug name left to the caller");
  }
  {
    r::TextureAssetView view;
    require(r::decode_texture_asset(cube, view).error == E::None, "the cube payload decodes");
    require(view.subresource_count() == 24U, "cube: six faces of four mips");
    require_subresources(view, cube, "cube subresource views its bytes");
    const rhi::TextureDescription d = view.texture_description();
    require(d.extent == rhi::Extent3{8, 8, 1} && d.array_size == 1U && d.mip_levels == 4U &&
                d.dimension == rhi::TextureDimension::TextureCube &&
                d.format == rhi::Format::BC7UnormSrgb && rhi::subresource_count(d) == 24U &&
                d.initial_access == rhi::Access::Unknown && d.debug_name.empty(),
            "cube description, its subresource count the payload's");
  }
  {
    Bytes normal = flat;
    patch_header(normal, [](r::TexturePayloadHeader& h) { h.flags = r::kTwoChannelNormal; });
    r::TextureAssetView view;
    require(r::decode_texture_asset(normal, view).error == E::None, "a known flag decodes");
  }

  using H = r::TexturePayloadHeader;
  struct Row {
    std::string_view note;
    const Bytes* base;
    std::function<void(Bytes&)> change;
    E expected;
    std::uint32_t index;
  };
  const std::array rows{
      Row{"header one byte short", &flat, [](Bytes& b) { b.resize(sizeof(H) - 1U); },
          E::HeaderTruncated, 0},
      Row{"record table off 16", &flat,
          [](Bytes& b) { patch_header(b, [](H& h) { h.subresources_offset += 4U; }); },
          E::SectionMisaligned, 0},
      Row{"record table past the end", &flat,
          [](Bytes& b) { patch_header(b, [](H& h) { h.subresource_count = 0x10000000U; }); },
          E::SectionOutOfBounds, 0},
      Row{"format 0", &flat, [](Bytes& b) { patch_header(b, [](H& h) { h.format = 0; }); },
          E::FormatUnknown, 0},
      Row{"dimension 3", &flat, [](Bytes& b) { patch_header(b, [](H& h) { h.dimension = 3; }); },
          E::DimensionInvalid, 0},
      Row{"a 2D texture with six faces", &flat,
          [](Bytes& b) { patch_header(b, [](H& h) { h.faces = 6; }); }, E::FaceCountInvalid, 0},
      Row{"width 0", &flat, [](Bytes& b) { patch_header(b, [](H& h) { h.width = 0; }); },
          E::ExtentInvalid, 0},
      Row{"a chain short of 1x1", &flat,
          [](Bytes& b) { patch_header(b, [](H& h) { h.mip_levels = 2; }); }, E::MipCountInvalid, 0},
      Row{"fewer subresources than faces times mips", &flat,
          [](Bytes& b) { patch_header(b, [](H& h) { h.subresource_count = 2; }); },
          E::SubresourceCountMismatch, 0},
      Row{"a cube face's mip of the wrong size", &cube,
          [](Bytes& b) { patch_record(b, 4, [](r::SubresourceRecord& s) { s.size = 32; }); },
          E::SubresourceSizeMismatch, 4},
      Row{"a loose row pitch", &flat,
          [](Bytes& b) { patch_record(b, 2, [](r::SubresourceRecord& s) { s.row_pitch = 8; }); },
          E::RowPitchMismatch, 2},
      Row{"an unknown flag", &flat, [](Bytes& b) { patch_header(b, [](H& h) { h.flags = 2; }); },
          E::FlagsUnknown, 0},
  };
  for (std::size_t i = 0; i < rows.size(); ++i) {
    Bytes bytes = *rows[i].base;
    rows[i].change(bytes);
    r::TextureAssetView view;
    const auto failure = r::decode_texture_asset(bytes, view);
    require(failure.error == rows[i].expected && failure.index == rows[i].index, i, rows[i].note);
  }
}

using Scenario = bw::testing::Scenario<void>;
constexpr std::array kScenarios{
    Scenario{"mip_arithmetic", &mip_arithmetic},
    Scenario{"rejections_named", &rejections_named},
};

} // namespace

int main(int argc, char** argv) { return bw::testing::run_scenario(argc, argv, kScenarios); }
