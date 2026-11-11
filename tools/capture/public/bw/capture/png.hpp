#ifndef BW_CAPTURE_PNG_HPP
#define BW_CAPTURE_PNG_HPP

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

// Responsibility: the PNG file of an 8-bit RGBA image, written by this
// project with no compression library (docs/architecture/source-layout.md,
// tools/capture: the capture tool's output path), and the packing of a
// readback's rows into that image. The character viewer's --dump writes
// it; the headless render tool takes it later. Pure functions: bytes in,
// bytes out, nothing read from a file or the environment.
//
// Contract.
//   1. encode_png_rgba8(width, height, rgba, out) fills `out` with exactly
//      this byte sequence, in order:
//      a. the 8-byte signature kPngSignature;
//      b. IHDR: 13 data bytes: width and height as 4-byte big-endian
//         integers, bit depth 8, colour type 6 (truecolour with alpha),
//         compression 0, filter 0, interlace 0;
//      c. sRGB: 1 data byte, rendering intent 0 (perceptual);
//      d. gAMA: 4 data bytes, 45455 big-endian;
//      e. cHRM: 32 data bytes, the eight big-endian values 31270, 32900,
//         64000, 33000, 30000, 60000, 15000, 6000;
//      f. IDAT: one chunk holding one zlib stream: the header bytes 0x78
//         0x01; the filtered image as stored deflate blocks; the Adler-32
//         of the filtered image, big-endian. The filtered image is height
//         scanlines, each a filter byte 0 (None) followed by the row's
//         width * 4 bytes in the order given. The blocks split it into
//         pieces of kStoredBlockBytes, the last piece shorter or equal;
//         each block is a header byte (0x00, or 0x01 for the last), the
//         piece's length and its one's complement as 2-byte little-endian
//         integers, then the piece. Adler-32: s1 from 1 and s2 from 0,
//         each byte added to s1 then s1 to s2, both modulo 65521, stored
//         as s2 * 65536 + s1.
//      g. IEND: 0 data bytes.
//      Every chunk is its data length as a 4-byte big-endian integer, the
//      4 type bytes, the data, and the CRC-32 of the type and data bytes
//      (reflected polynomial 0xEDB88320, register all ones at start,
//      complemented at the end), big-endian.
//      So out.size() is 8 + 25 + 13 + 16 + 44 + 12 + (2 + 5 * blocks + n
//      + 4) + 12, n = height * (width * 4 + 1) and blocks = ceil(n /
//      kStoredBlockBytes), and the same inputs give the same bytes.
//   2. Failures, the first in this order, with `out` untouched:
//      ZeroDimension when width or height is 0; DimensionTooLarge when
//      either exceeds kPngMaxDimension or when the IDAT data (the zlib
//      header, the stored blocks and the trailer over n bytes) would
//      exceed kPngMaxDimension bytes, the chunk length limit;
//      ByteCountMismatch when rgba.size() is not width * height * 4,
//      counted in 64 bits.
//   3. pack_bgra8_rows(staged, row_pitch, width, height, rgba_out) fills
//      rgba_out with width * height * 4 bytes: for each of height rows,
//      the row's first width * 4 bytes at staged[row * row_pitch] read as
//      B, G, R, A quads and written R, G, B, 255, so a readback's padding
//      and channel order are gone and the alpha is opaque. StagingTooSmall
//      when staged holds fewer than (height - 1) * row_pitch + width * 4
//      bytes, ZeroDimension as in 2, each with rgba_out untouched.
//      Precondition: row_pitch is at least width * 4 (both come from one
//      staging_layout in the caller).
//
// The file is the signature, IHDR for 8-bit colour type 6, an sRGB chunk
// with the gAMA and cHRM chunks that describe sRGB beside it, one IDAT
// holding a single zlib stream with filter type 0 on every scanline, and
// IEND. Interlacing, palettes, the other filter types, 16-bit samples and
// the other ancillary chunks are not written: the dump is an 8-bit sRGB
// frame for looking at and comparing bytes, and nothing reads a PNG here.
// The zlib header for method 8, a 32K window and no dictionary is 0x78
// 0x01, and the Adler-32 trailer is big-endian. The deflate data is stored
// blocks of at most 65535 bytes, BFINAL bit 0 and BTYPE bits 1 and 2 of the
// header byte, LEN and NLEN little-endian: no compression, since the dump
// is compared, not sent.
//
// Core guidelines read: I.1, F.20 and F.16 (bytes in by span, the file by
// value in an out vector the caller may reuse), E.27 (named errors, the
// output untouched on one), Con.5 (the CRC and Adler tables are constexpr),
// Enum.3. Performance guidelines read: EMB.6 (the CRC table is built at
// compile time), MEM.9 (one reservation of the output's exact size, no
// other allocation).
namespace bw::capture {

inline constexpr std::array<std::byte, 8> kPngSignature{
    std::byte{0x89}, std::byte{0x50}, std::byte{0x4E}, std::byte{0x47},
    std::byte{0x0D}, std::byte{0x0A}, std::byte{0x1A}, std::byte{0x0A}};
inline constexpr std::uint32_t kPngMaxDimension = 0x7FFFFFFFU;
inline constexpr std::size_t kStoredBlockBytes = 65535;

enum class PngError : std::uint8_t {
  None,
  ZeroDimension,
  DimensionTooLarge,
  ByteCountMismatch,
  StagingTooSmall,
};

[[nodiscard]] PngError encode_png_rgba8(std::uint32_t width, std::uint32_t height,
                                        std::span<const std::byte> rgba,
                                        std::vector<std::byte>& out);

[[nodiscard]] PngError pack_bgra8_rows(std::span<const std::byte> staged, std::uint32_t row_pitch,
                                       std::uint32_t width, std::uint32_t height,
                                       std::vector<std::byte>& rgba_out);

} // namespace bw::capture

#endif
