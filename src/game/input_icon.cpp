// Original physical-code lookup and atlas packing shared by controller setup
// and Help. Both-phase Core I.1/C.9 and MEM.9 apply as in input_icon.hpp.
#include "input_icon.hpp"

#include <algorithm>
#include <stdexcept>

#include "../formats/reader.hpp"
namespace bw::game {
namespace {
constexpr unsigned kIconColumns = 12, kIconRows = 13,
                   kAtlasWidth = kIconColumns * kInputIconPixels,
                   kAtlasHeight = kIconRows * kInputIconPixels;
constexpr std::array<unsigned, kInputIconCount> kIconCodes{
    0,   1,   2,   3,   4,   5,   6,   7,   8,   9,   10,  11,  12,  14,  15,
    16,  17,  18,  19,  20,  21,  22,  23,  24,  25,  26,  27,  28,  29,  30,
    31,  32,  33,  34,  35,  36,  37,  38,  39,  42,  43,  44,  45,  46,  47,
    48,  49,  50,  51,  52,  53,  54,  55,  56,  57,  58,  59,  60,  61,  62,
    63,  64,  65,  66,  67,  71,  72,  73,  74,  75,  76,  77,  78,  79,  80,
    81,  82,  83,  125, 144, 145, 146, 157, 181, 184, 199, 200, 201, 203, 205,
    207, 208, 209, 211, 300, 301, 302, 308, 309, 310, 320, 321, 322, 323, 330,
    331, 332, 333, 350, 351, 352, 353, 354, 355, 356, 357, 358, 359, 360, 361,
    380, 381, 382, 383, 384, 385, 386, 387, 388, 389, 390, 391, 400, 401, 402,
    403, 404, 405, 406, 407, 420, 421, 422, 440, 441, 442, 443, 444, 450};
constexpr std::array<unsigned, 16> kPadIcons{126, 127, 128, 129, 132, 133,
                                             134, 135, 124, 125, 130, 131,
                                             120, 121, 122, 123};
}  // namespace
unsigned input_key_icon(unsigned code) noexcept {
  const auto found = std::ranges::find(kIconCodes, code);
  return found == kIconCodes.end()
             ? 0
             : static_cast<unsigned>(found - kIconCodes.begin());
}
unsigned input_pad_icon(unsigned code) noexcept {
  constexpr unsigned kAxisFlag = 128, kAxisMask = 3, kAxisFirst = 104;
  if ((code & kAxisFlag) != 0) return kAxisFirst + (code & kAxisMask);
  return code < kPadIcons.size() ? std::span(kPadIcons)[code] : 0;
}
render::Rect input_icon_uv(unsigned index) {
  if (index >= kInputIconCount) {
    throw std::invalid_argument("Input icon index invalid");
  }
  const auto row = index / kIconColumns;
  const auto column = index % kIconColumns;
  return {static_cast<float>(column * kInputIconPixels) / kAtlasWidth,
          static_cast<float>(row * kInputIconPixels) / kAtlasHeight,
          static_cast<float>(kInputIconPixels) / kAtlasWidth,
          static_cast<float>(kInputIconPixels) / kAtlasHeight};
}
formats::Image decode_input_icon_atlas(
    std::span<const std::uint8_t> bytes,
    const memory::Allocator<std::uint8_t>& allocator) {
  const resources::detail::Reader reader{bytes};
  constexpr std::size_t kOffsets = 8, kWord = 4, kChannels = 4;
  if (reader.u32(0) != kInputIconCount) {
    throw std::runtime_error("Input icon table invalid");
  }
  formats::Image atlas{
      kAtlasWidth, kAtlasHeight,
      memory::Vector<std::uint8_t>(
          static_cast<std::size_t>(kAtlasWidth) * kAtlasHeight * kChannels, 0,
          allocator)};
  for (unsigned i = 0; i < kInputIconCount; ++i) {
    const auto begin = reader.u32(kOffsets + i * kWord),
               end = reader.u32(kOffsets + (i + 1) * kWord);
    if (begin < kOffsets + (kInputIconCount + 1) * kWord || end < begin) {
      throw std::runtime_error("Input icon range invalid");
    }
    reader.range(begin, end - begin);
    const auto decoded =
        formats::decode_png(bytes.subspan(begin, end - begin), {}, allocator);
    if (decoded.width != kInputIconPixels ||
        decoded.height != kInputIconPixels) {
      throw std::runtime_error("Input icon dimensions differ");
    }
    const auto x = (i % kIconColumns) * kInputIconPixels,
               y = (i / kIconColumns) * kInputIconPixels;
    for (unsigned row = 0; row < kInputIconPixels; ++row) {
      const auto source = std::span(decoded.rgba)
                              .subspan(static_cast<std::size_t>(row) *
                                           kInputIconPixels * kChannels,
                                       kInputIconPixels * kChannels);
      std::ranges::copy(
          source,
          atlas.rgba.begin() + static_cast<std::ptrdiff_t>(
                                   ((y + row) * kAtlasWidth + x) * kChannels));
    }
  }
  return atlas;
}
}  // namespace bw::game
