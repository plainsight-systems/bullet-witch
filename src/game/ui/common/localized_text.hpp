// Responsibility grouping preserves the existing contract and owner policies.
// Design Core SF.11 (#rs-contained), implementation SF.13
// (#rs-portable-header-id), and both-phase performance COPY.4
// (guidelines/copy-move/COPY.4-rule-of-five-rule-of-zero.md) keep direct
// includes portable and preserve special members without extra payload copies.
// Prepared pause-family localized text helpers. Tree traversal borrows the
// validated TextTable lease; normalization writes caller-owned bounded storage
// once at preparation. Runtime emission owns neither strings nor asset bytes.
// Core R.14 and performance MEM.9 apply: ranges carry bounds and frame paths
// perform no decoding or allocations. Full-width English maps to the existing
// original-font ASCII emitter; unsupported characters fail explicitly.
#pragma once
#include <span>
#include <stdexcept>
#include <string_view>

#include "bw/content/formats/text_table.hpp"
namespace bw::game::localized_text {
// Traverse the validated namespace tree instead of treating repeated numeric
// keys in sys/mem/net as one flat table. The returned text borrows its lease.
enum class TextNamespace : std::uint8_t { system, memory, network };
inline std::string_view localized_text(const formats::TextTable& table,
                                       TextNamespace name_space,
                                       std::string_view key) {
  constexpr unsigned kEnglish = 200;
  const std::string_view group_name =
      name_space == TextNamespace::system   ? std::string_view{"sys"}
      : name_space == TextNamespace::memory ? std::string_view{"mem"}
                                            : std::string_view{"net"};
  for (std::size_t index = 0; index < table.records(0); ++index) {
    const auto group = table.node(0, index);
    if (group.text != group_name) {
      continue;
    }
    for (std::size_t row = 0; row < group.children.count; ++row) {
      const auto entry = table.node(1, group.children.first + row);
      if (entry.text != key) {
        continue;
      }
      for (std::size_t variant = 0; variant < entry.children.count; ++variant) {
        const auto languages = table.node(2, entry.children.first + variant);
        for (std::size_t language = 0; language < languages.children.count;
             ++language) {
          const auto text = table.node(3, languages.children.first + language);
          if (text.identifier == kEnglish) {
            return text.text;
          }
        }
      }
    }
  }
  throw std::runtime_error("Original pause system text missing");
}
inline std::string_view system_text(const formats::TextTable& table,
                                    std::string_view key) {
  return localized_text(table, TextNamespace::system, key);
}
inline void prepare_text(std::string_view source, std::span<char> output) {
  constexpr unsigned kAsciiLimit = 128, kFullWidthLead = 0xef,
                     kContinuationMask = 0xc0, kContinuation = 0x80,
                     kPayload = 0x3f, kSixBits = 6, kFullWidthBase = 0xf000,
                     kFullWidthFirst = 0xff01, kFullWidthLast = 0xff5e,
                     kFullWidthDelta = 0xfee0;
  std::size_t used = 0;
  for (std::size_t i = 0; i < source.size(); ++i) {
    auto codepoint = static_cast<unsigned char>(source[i]);
    unsigned value = codepoint;
    // Original English storage text uses U+2019. The existing bitmap font
    // represents this punctuation with its apostrophe glyph.
    constexpr unsigned kPunctuationLead = 0xe2, kPunctuationSecond = 0x80,
                       kRightApostrophe = 0x99;
    if (value == kPunctuationLead && source.size() - i >= 3 &&
        static_cast<unsigned char>(source[i + 1]) == kPunctuationSecond &&
        static_cast<unsigned char>(source[i + 2]) == kRightApostrophe) {
      value = '\'';
      i += 2;
    } else if (value >= kAsciiLimit) {
      if (value != kFullWidthLead || source.size() - i < 3) {
        throw std::runtime_error("Unsupported pause text encoding");
      }
      const auto second = static_cast<unsigned char>(source[i + 1]);
      const auto third = static_cast<unsigned char>(source[i + 2]);
      if ((second & kContinuationMask) != kContinuation ||
          (third & kContinuationMask) != kContinuation) {
        throw std::runtime_error("Invalid pause text continuation");
      }
      value = kFullWidthBase | ((second & kPayload) << kSixBits) |
              (third & kPayload);
      if (value < kFullWidthFirst || value > kFullWidthLast) {
        throw std::runtime_error("Unsupported pause text character");
      }
      value -= kFullWidthDelta;
      i += 2;
    }
    if (used + 1 >= output.size()) {
      throw std::length_error("Pause text exceeds prepared capacity");
    }
    output[used++] = static_cast<char>(value);
  }
  if (used >= output.size()) {
    throw std::length_error("Pause text has no terminator capacity");
  }
  output[used] = '\0';
}
}  // namespace bw::game::localized_text
