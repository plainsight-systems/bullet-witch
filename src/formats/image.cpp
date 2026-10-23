// PNG decoding implementation; contract/design in bw/formats/image.hpp.
// Validate signature/dimension/output budgets before libpng allocation; use its
// simplified read API to own library state through a scope guard and normalize
// to RGBA8. Linear decode work, one output vector; library owns transient
// state. Design: cpp-guidelines SL.con.3 (#rsl-bounds), cpp-performance COPY.4
// (guidelines/copy-move/COPY.4-rule-of-five-rule-of-zero.md). Implementation:
// E.6 (#re-raii) pairs png_image_free with every exit; COPY.4 returns owned
// pixels by value without suppressing moves. Reference: libpng 1.6.59
// example.c, png_image_begin_read_from_memory / png_image_finish_read; no codec
// copied.
#include "bw/formats/image.hpp"

#include <png.h>

#include <algorithm>
#include <array>
#include <memory>
#include <string>

#include "bw/formats/common.hpp"
namespace bw::formats {
namespace {
constexpr unsigned kByteBits = 8;
constexpr std::size_t kChunkLengthOffset = 8;
constexpr std::size_t kChunkTypeOffset = 12;
constexpr std::uint32_t kHeaderLength = 13;
std::uint32_t big_word(std::span<const std::uint8_t> bytes) {
  std::uint32_t result{};
  for (auto byte : bytes) {
    result = (result << kByteBits) | byte;
  }
  return result;
}
}  // namespace
Image decode_png(std::span<const std::uint8_t> encoded, ImageLimits limits) {
  constexpr std::array<std::uint8_t, 8> signature{137, 80, 78, 71,
                                                  13,  10, 26, 10};
  constexpr std::array<std::uint8_t, 4> header{'I', 'H', 'D', 'R'};
  constexpr std::size_t minimum_header = 33;
  if (encoded.size() < minimum_header ||
      encoded.size() > limits.maximum_encoded_bytes ||
      !std::ranges::equal(encoded.first(signature.size()), signature) ||
      big_word(encoded.subspan(kChunkLengthOffset, 4)) != kHeaderLength ||
      !std::ranges::equal(encoded.subspan(kChunkTypeOffset, 4), header)) {
    throw resources::Error(resources::ErrorCode::malformed,
                           "Invalid or oversized PNG header");
  }
  const auto width = big_word(encoded.subspan(16, 4));
  const auto height = big_word(encoded.subspan(20, 4));
  const auto pixels = static_cast<std::uint64_t>(width) * height;
  if (!width || !height || width > limits.maximum_dimension ||
      height > limits.maximum_dimension ||
      pixels > limits.maximum_decoded_bytes / 4) {
    throw resources::Error(resources::ErrorCode::malformed,
                           "PNG dimensions exceed decode budget");
  }
  png_image state{};
  state.version = PNG_IMAGE_VERSION;
  // The deleter releases library-owned state; the control structure is local.
  const std::unique_ptr<png_image, decltype(&png_image_free)> cleanup(
      &state, png_image_free);
  if (!png_image_begin_read_from_memory(&state, encoded.data(),
                                        encoded.size())) {
    throw resources::Error(resources::ErrorCode::malformed,
                           std::string("PNG header decode failed: ") +
                               std::span(state.message).data());
  }
  state.format = PNG_FORMAT_RGBA;
  Image output{width, height,
               std::vector<std::uint8_t>(static_cast<std::size_t>(pixels) * 4)};
  if (!png_image_finish_read(&state, nullptr, output.rgba.data(), 0, nullptr)) {
    throw resources::Error(
        resources::ErrorCode::malformed,
        std::string("PNG decode failed: ") + std::span(state.message).data());
  }
  return output;
}
}  // namespace bw::formats
