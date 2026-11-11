// Unit test: the PNG writer and the readback row packer
// (bw/capture/png.hpp), one table scenario per contract item.
#include "bw/capture/png.hpp"

#include "bw/testing/harness.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace {

using bw::capture::PngError;
using bw::testing::require;

std::vector<std::byte> bytes_of(std::span<const std::uint8_t> values) {
  std::vector<std::byte> out;
  out.reserve(values.size());
  for (const std::uint8_t value : values) {
    out.push_back(static_cast<std::byte>(value));
  }
  return out;
}

std::uint32_t u32_be(const std::vector<std::byte>& bytes, std::size_t at) {
  return (static_cast<std::uint32_t>(bytes.at(at)) << 24U) |
         (static_cast<std::uint32_t>(bytes.at(at + 1U)) << 16U) |
         (static_cast<std::uint32_t>(bytes.at(at + 2U)) << 8U) |
         static_cast<std::uint32_t>(bytes.at(at + 3U));
}

std::uint32_t u16_le(const std::vector<std::byte>& bytes, std::size_t at) {
  return static_cast<std::uint32_t>(bytes.at(at)) |
         (static_cast<std::uint32_t>(bytes.at(at + 1U)) << 8U);
}

// The 2x2 image: row 0 (10 20 30 FF) (40 50 60 80), row 1 (70 80 90 00)
// (A0 B0 C0 FF), as R, G, B, A.
constexpr std::array<std::uint8_t, 16> kPixels{0x10, 0x20, 0x30, 0xFF, 0x40, 0x50, 0x60, 0x80,
                                               0x70, 0x80, 0x90, 0x00, 0xA0, 0xB0, 0xC0, 0xFF};

// Its file, worked from contract 1. The five CRC-32 values and the Adler-32
// were computed once with Python's zlib.crc32 and zlib.adler32 over the
// bytes each covers, and the zlib stream was checked to inflate to the
// filtered image with zlib.decompress.
constexpr std::array<std::uint8_t, 159> kFile{
    // Signature.
    0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A,
    // IHDR: length 13, type, width 2, height 2, depth 8, colour type 6,
    // compression, filter, interlace 0; CRC-32 of "IHDR" and the 13 bytes.
    0x00, 0x00, 0x00, 0x0D, 0x49, 0x48, 0x44, 0x52, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x02,
    0x08, 0x06, 0x00, 0x00, 0x00, 0x72, 0xB6, 0x0D, 0x24,
    // sRGB: length 1, type, intent 0; CRC-32 of "sRGB" and the byte.
    0x00, 0x00, 0x00, 0x01, 0x73, 0x52, 0x47, 0x42, 0x00, 0xAE, 0xCE, 0x1C, 0xE9,
    // gAMA: length 4, type, 45455; CRC-32 of "gAMA" and the 4 bytes.
    0x00, 0x00, 0x00, 0x04, 0x67, 0x41, 0x4D, 0x41, 0x00, 0x00, 0xB1, 0x8F, 0x0B, 0xFC, 0x61, 0x05,
    // cHRM: length 32, type, 31270 32900 64000 33000 30000 60000 15000 6000;
    // CRC-32 of "cHRM" and the 32 bytes.
    0x00, 0x00, 0x00, 0x20, 0x63, 0x48, 0x52, 0x4D, 0x00, 0x00, 0x7A, 0x26, 0x00, 0x00, 0x80, 0x84,
    0x00, 0x00, 0xFA, 0x00, 0x00, 0x00, 0x80, 0xE8, 0x00, 0x00, 0x75, 0x30, 0x00, 0x00, 0xEA, 0x60,
    0x00, 0x00, 0x3A, 0x98, 0x00, 0x00, 0x17, 0x70, 0x9C, 0xBA, 0x51, 0x3C,
    // IDAT: length 29, type; zlib header 78 01; one final stored block:
    // header 01, LEN 18, NLEN ~18, little-endian; the two scanlines, each a
    // filter byte 0 and the row; Adler-32 of the 18 filtered bytes; CRC-32 of
    // "IDAT" and the 29 bytes.
    0x00, 0x00, 0x00, 0x1D, 0x49, 0x44, 0x41, 0x54, 0x78, 0x01, 0x01, 0x12, 0x00, 0xED, 0xFF, 0x00,
    0x10, 0x20, 0x30, 0xFF, 0x40, 0x50, 0x60, 0x80, 0x00, 0x70, 0x80, 0x90, 0x00, 0xA0, 0xB0, 0xC0,
    0xFF, 0x35, 0x73, 0x07, 0x5F, 0xA6, 0xB8, 0x59, 0x63,
    // IEND: length 0, type; CRC-32 of "IEND".
    0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4E, 0x44, 0xAE, 0x42, 0x60, 0x82};

// (png.hpp, encode_png_rgba8, contract 1 and 2): a 2x2 image's bytes are the
// sequence above, signature to IEND; each failure name with `out` untouched.
void encodes_known_image() {
  const std::vector<std::byte> pixels = bytes_of(kPixels);
  std::vector<std::byte> out;
  require(bw::capture::encode_png_rgba8(2, 2, pixels, out) == PngError::None,
          "a 2x2 image encodes");
  require(out == bytes_of(kFile), "the 2x2 file is the worked byte sequence");

  struct Row {
    std::uint32_t width, height;
    std::size_t bytes;
    PngError error;
  };
  const std::array rows{
      Row{0, 2, 0, PngError::ZeroDimension},
      Row{bw::capture::kPngMaxDimension + 1U, 1, 0, PngError::DimensionTooLarge},
      // Each side is within kPngMaxDimension but the IDAT data is not, and
      // the empty span would otherwise be a ByteCountMismatch.
      Row{23171, 23171, 0, PngError::DimensionTooLarge},
      Row{2, 2, 15, PngError::ByteCountMismatch},
  };
  const std::vector<std::byte> sentinel{std::byte{0xAB}};
  for (std::size_t i = 0; i < rows.size(); ++i) {
    std::vector<std::byte> untouched = sentinel;
    const std::span<const std::byte> rgba(pixels.data(), rows[i].bytes);
    require(bw::capture::encode_png_rgba8(rows[i].width, rows[i].height, rgba, untouched) ==
                rows[i].error,
            i, "the failure is named");
    require(untouched == sentinel, i, "out is untouched on a failure");
  }
}

// (png.hpp, encode_png_rgba8, contract 1f and the size formula): filtered
// bytes past one block are split into blocks of kStoredBlockBytes, the last
// shorter; only the last header is 0x01; every NLEN is LEN's complement; the
// file's size is the formula's.
void splits_stored_blocks() {
  constexpr std::uint32_t kWidth = 2000;
  constexpr std::uint32_t kHeight = 9;
  constexpr std::size_t kFiltered = std::size_t{kHeight} * (kWidth * 4U + 1U); // 72009
  const std::vector<std::byte> pixels(std::size_t{kWidth} * kHeight * 4U, std::byte{0x5A});
  std::vector<std::byte> out;
  require(bw::capture::encode_png_rgba8(kWidth, kHeight, pixels, out) == PngError::None,
          "the image encodes");
  constexpr std::size_t kBlocks = 2;
  require(out.size() ==
              8U + 25U + 13U + 16U + 44U + 12U + (2U + 5U * kBlocks + kFiltered + 4U) + 12U,
          "the file's size is the formula's: " + std::to_string(out.size()));
  // IDAT follows the signature and the IHDR, sRGB, gAMA and cHRM chunks.
  constexpr std::size_t kIdat = 8U + 25U + 13U + 16U + 44U;
  require(u32_be(out, kIdat) == 2U + 5U * kBlocks + kFiltered + 4U,
          "the IDAT length covers the zlib stream");
  std::size_t at = kIdat + 8U + 2U;
  std::size_t total = 0;
  std::size_t blocks = 0;
  for (bool last = false; !last; ++blocks) {
    const auto header = static_cast<std::uint8_t>(out.at(at));
    const std::uint32_t length = u16_le(out, at + 1U);
    const std::uint32_t complement = u16_le(out, at + 3U);
    last = header == 0x01U;
    require(header == 0x00U || last, blocks, "a block header is 0x00 or 0x01");
    require((length ^ complement) == 0xFFFFU, blocks, "NLEN is LEN's complement");
    require(last || length == bw::capture::kStoredBlockBytes, blocks,
            "a block before the last is full");
    total += length;
    at += 5U + length;
  }
  require(blocks == kBlocks, "the filtered image takes two blocks");
  require(total == kFiltered, "the blocks hold the filtered image");
  require(at + 4U + 4U + 12U == out.size(), "the Adler-32, the IDAT CRC and IEND follow");
}

// (png.hpp, pack_bgra8_rows, contract 3): rows read at the pitch become
// tightly packed R, G, B, 255; StagingTooSmall and ZeroDimension with the
// output untouched.
void packs_bgra_rows() {
  constexpr std::uint32_t kPitch = 256;
  std::vector<std::byte> staged(std::size_t{kPitch} + 12U, std::byte{0xEE});
  for (std::size_t row = 0; row < 2U; ++row) {
    for (std::size_t texel = 0; texel < 3U; ++texel) {
      const std::size_t at = row * kPitch + texel * 4U;
      const auto base = static_cast<std::uint8_t>(16U * (row * 3U + texel));
      staged[at] = static_cast<std::byte>(base + 1U);      // B
      staged[at + 1U] = static_cast<std::byte>(base + 2U); // G
      staged[at + 2U] = static_cast<std::byte>(base + 3U); // R
      staged[at + 3U] = static_cast<std::byte>(base + 4U); // A, replaced
    }
  }
  std::vector<std::byte> rgba;
  require(bw::capture::pack_bgra8_rows(staged, kPitch, 3, 2, rgba) == PngError::None,
          "two padded rows pack");
  constexpr std::array<std::uint8_t, 24> kPacked{0x03, 0x02, 0x01, 0xFF, 0x13, 0x12, 0x11, 0xFF,
                                                 0x23, 0x22, 0x21, 0xFF, 0x33, 0x32, 0x31, 0xFF,
                                                 0x43, 0x42, 0x41, 0xFF, 0x53, 0x52, 0x51, 0xFF};
  require(rgba == bytes_of(kPacked), "the rows are R, G, B, 255 without the padding");

  struct Row {
    std::size_t staged_bytes;
    std::uint32_t width;
    PngError error;
  };
  const std::array rows{Row{kPitch + 11U, 3, PngError::StagingTooSmall},
                        Row{staged.size(), 0, PngError::ZeroDimension}};
  const std::vector<std::byte> sentinel{std::byte{0xAB}};
  for (std::size_t i = 0; i < rows.size(); ++i) {
    std::vector<std::byte> untouched = sentinel;
    const std::span<const std::byte> bytes(staged.data(), rows[i].staged_bytes);
    require(bw::capture::pack_bgra8_rows(bytes, kPitch, rows[i].width, 2, untouched) ==
                rows[i].error,
            i, "the failure is named");
    require(untouched == sentinel, i, "the output is untouched on a failure");
  }
}

using Scenario = bw::testing::Scenario<void>;

constexpr std::array kScenarios{
    Scenario{"encodes_known_image", &encodes_known_image},
    Scenario{"splits_stored_blocks", &splits_stored_blocks},
    Scenario{"packs_bgra_rows", &packs_bgra_rows},
};

} // namespace

int main(int argc, char** argv) { return bw::testing::run_scenario(argc, argv, kScenarios); }
