#include "bw/capture/png.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

// Responsibility: the out-of-line part of bw/capture/png.hpp: the chunk
// writer, the CRC-32 and Adler-32 sums and the stored deflate framing.
namespace bw::capture {
namespace {

// CRC-32 with the reflected polynomial 0xEDB88320; the table holds the remainder of each
// byte value, built at compile time.
constexpr std::array<std::uint32_t, 256> make_crc_table() {
  std::array<std::uint32_t, 256> table{};
  for (std::uint32_t n = 0; n < 256U; ++n) {
    std::uint32_t c = n;
    for (int k = 0; k < 8; ++k) {
      c = (c & 1U) != 0U ? 0xEDB88320U ^ (c >> 1U) : c >> 1U;
    }
    table[n] = c;
  }
  return table;
}
constexpr std::array<std::uint32_t, 256> kCrcTable = make_crc_table();

// The CRC-32 register starts at all ones and is complemented at the end; this is the update step
// over the bytes, without either.
std::uint32_t crc_update(std::uint32_t crc, std::span<const std::byte> bytes) {
  for (const std::byte b : bytes) {
    crc = kCrcTable[(crc ^ static_cast<std::uint32_t>(b)) & 0xFFU] ^ (crc >> 8U);
  }
  return crc;
}

// Adler-32: s1 from 1 and s2 from 0, each
// modulo 65521. The sums are reduced once per byte; nothing here is fast
// enough to matter for a dump.
constexpr std::uint32_t kAdlerModulus = 65521;
struct Adler {
  std::uint32_t s1{1};
  std::uint32_t s2{0};
  void update(std::span<const std::byte> bytes) {
    for (const std::byte b : bytes) {
      s1 = (s1 + static_cast<std::uint32_t>(b)) % kAdlerModulus;
      s2 = (s2 + s1) % kAdlerModulus;
    }
  }
  [[nodiscard]] std::uint32_t value() const { return (s2 << 16U) | s1; }
};

// Every PNG integer is stored most
// significant byte first.
void put_u32_be(std::vector<std::byte>& out, std::uint32_t value) {
  out.push_back(static_cast<std::byte>((value >> 24U) & 0xFFU));
  out.push_back(static_cast<std::byte>((value >> 16U) & 0xFFU));
  out.push_back(static_cast<std::byte>((value >> 8U) & 0xFFU));
  out.push_back(static_cast<std::byte>(value & 0xFFU));
}

// A stored deflate block's LEN and NLEN are least significant byte first.
void put_u16_le(std::vector<std::byte>& out, std::uint16_t value) {
  out.push_back(static_cast<std::byte>(value & 0xFFU));
  out.push_back(static_cast<std::byte>((value >> 8U) & 0xFFU));
}

// A chunk is opened by writing its length and type and closed by writing
// the CRC of everything from the type to the end of the data; the length
// counts the data only.
std::size_t open_chunk(std::vector<std::byte>& out, std::uint32_t length, std::string_view type) {
  put_u32_be(out, length);
  const std::size_t type_at = out.size();
  for (const char c : type) {
    out.push_back(static_cast<std::byte>(c));
  }
  return type_at;
}
void close_chunk(std::vector<std::byte>& out, std::size_t type_at) {
  const std::span<const std::byte> covered(out.data() + type_at, out.size() - type_at);
  put_u32_be(out, crc_update(0xFFFFFFFFU, covered) ^ 0xFFFFFFFFU);
}

void put_chunk(std::vector<std::byte>& out, std::string_view type,
               std::span<const std::uint32_t> words) {
  const std::size_t type_at = open_chunk(out, static_cast<std::uint32_t>(words.size() * 4U), type);
  for (const std::uint32_t word : words) {
    put_u32_be(out, word);
  }
  close_chunk(out, type_at);
}

// Everything of contract 1's size but the IDAT data: the signature, IHDR,
// sRGB, gAMA and cHRM chunks, the IDAT chunk's length, type and CRC, IEND.
constexpr std::uint64_t kFixedBytes = 8 + 25 + 13 + 16 + 44 + 12 + 12;
constexpr std::uint64_t kStoredBlockHeaderBytes = 5;

} // namespace

// The signature, IHDR first, then sRGB (intent 0), gAMA (45455) and cHRM
// (the sRGB primaries and white point) before IDAT, one IDAT with filter
// type 0 on every scanline, and an empty IEND last. The IDAT data is the
// zlib header 0x78 0x01, stored blocks (a header byte with BFINAL in bit 0
// and BTYPE 00, then LEN and NLEN, then LEN bytes) and the big-endian
// Adler-32.
PngError encode_png_rgba8(std::uint32_t width, std::uint32_t height,
                          std::span<const std::byte> rgba, std::vector<std::byte>& out) {
  if (width == 0U || height == 0U) {
    return PngError::ZeroDimension;
  }
  if (width > kPngMaxDimension || height > kPngMaxDimension) {
    return PngError::DimensionTooLarge;
  }
  // The IDAT data length is a 4-byte chunk length capped at
  // kPngMaxDimension, so it is sized in 64 bits before anything is
  // allocated. Both sides are at most 2^31 - 1, so the filtered size fits in
  // 64 bits; it is checked alone first so that the block headers and the
  // zlib framing are only added to a size that cannot wrap.
  const std::uint64_t row_bytes = static_cast<std::uint64_t>(width) * 4U;
  const std::uint64_t filtered = (row_bytes + 1U) * height;
  if (filtered > kPngMaxDimension) {
    return PngError::DimensionTooLarge;
  }
  const std::uint64_t blocks = (filtered + kStoredBlockBytes - 1U) / kStoredBlockBytes;
  const std::uint64_t idat_data = 2U + kStoredBlockHeaderBytes * blocks + filtered + 4U;
  if (idat_data > kPngMaxDimension) {
    return PngError::DimensionTooLarge;
  }
  if (rgba.size() != row_bytes * height) {
    return PngError::ByteCountMismatch;
  }

  out.clear();
  out.reserve(static_cast<std::size_t>(kFixedBytes + idat_data));
  out.insert(out.end(), kPngSignature.begin(), kPngSignature.end());

  std::size_t type_at = open_chunk(out, 13, "IHDR");
  put_u32_be(out, width);
  put_u32_be(out, height);
  // Bit depth 8, colour type 6, compression, filter and interlace 0.
  constexpr std::array<std::byte, 5> kHeaderFields{std::byte{8}, std::byte{6}, std::byte{0},
                                                   std::byte{0}, std::byte{0}};
  out.insert(out.end(), kHeaderFields.begin(), kHeaderFields.end());
  close_chunk(out, type_at);

  type_at = open_chunk(out, 1, "sRGB");
  out.push_back(std::byte{0});
  close_chunk(out, type_at);

  constexpr std::array<std::uint32_t, 1> kGamma{45455};
  put_chunk(out, "gAMA", kGamma);
  constexpr std::array<std::uint32_t, 8> kChromaticities{31270, 32900, 64000, 33000,
                                                         30000, 60000, 15000, 6000};
  put_chunk(out, "cHRM", kChromaticities);

  // The filtered image is walked as one stream of scanlines (a filter byte,
  // then the row) cut into stored blocks wherever a block fills.
  type_at = open_chunk(out, static_cast<std::uint32_t>(idat_data), "IDAT");
  out.push_back(std::byte{0x78});
  out.push_back(std::byte{0x01});
  Adler adler{};
  std::uint64_t remaining = filtered;
  std::uint64_t row = 0;
  std::uint64_t in_row = 0; // 0 is the filter byte, 1..row_bytes the row
  while (remaining != 0U) {
    const auto piece =
        static_cast<std::uint16_t>(remaining < kStoredBlockBytes ? remaining : kStoredBlockBytes);
    remaining -= piece;
    out.push_back(remaining == 0U ? std::byte{0x01} : std::byte{0x00});
    put_u16_le(out, piece);
    put_u16_le(out, static_cast<std::uint16_t>(~piece));
    std::uint64_t left = piece;
    while (left != 0U) {
      if (in_row == 0U) {
        constexpr std::array<std::byte, 1> kFilterNone{std::byte{0}};
        out.push_back(kFilterNone[0]);
        adler.update(kFilterNone);
        in_row = 1;
        --left;
        continue;
      }
      const std::uint64_t take =
          row_bytes - (in_row - 1U) < left ? row_bytes - (in_row - 1U) : left;
      const std::span<const std::byte> bytes =
          rgba.subspan(static_cast<std::size_t>(row * row_bytes + (in_row - 1U)),
                       static_cast<std::size_t>(take));
      out.insert(out.end(), bytes.begin(), bytes.end());
      adler.update(bytes);
      left -= take;
      in_row += take;
      if (in_row == row_bytes + 1U) {
        in_row = 0;
        ++row;
      }
    }
  }
  put_u32_be(out, adler.value());
  close_chunk(out, type_at);

  type_at = open_chunk(out, 0, "IEND");
  close_chunk(out, type_at);
  return PngError::None;
}

// The pitch, width and height in the order the header documents.
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
PngError pack_bgra8_rows(std::span<const std::byte> staged, std::uint32_t row_pitch,
                         std::uint32_t width, std::uint32_t height,
                         std::vector<std::byte>& rgba_out) {
  if (width == 0U || height == 0U) {
    return PngError::ZeroDimension;
  }
  const std::uint64_t row_bytes = static_cast<std::uint64_t>(width) * 4U;
  const std::uint64_t needed = static_cast<std::uint64_t>(height - 1U) * row_pitch + row_bytes;
  if (staged.size() < needed) {
    return PngError::StagingTooSmall;
  }
  rgba_out.resize(static_cast<std::size_t>(row_bytes * height));
  std::size_t at = 0;
  for (std::uint64_t row = 0; row < height; ++row) {
    const std::span<const std::byte> source = staged.subspan(
        static_cast<std::size_t>(row * row_pitch), static_cast<std::size_t>(row_bytes));
    for (std::size_t texel = 0; texel < source.size(); texel += 4U) {
      rgba_out[at++] = source[texel + 2U];
      rgba_out[at++] = source[texel + 1U];
      rgba_out[at++] = source[texel];
      rgba_out[at++] = std::byte{0xFF};
    }
  }
  return PngError::None;
}

} // namespace bw::capture
