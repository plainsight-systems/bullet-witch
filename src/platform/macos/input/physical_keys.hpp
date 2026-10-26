// macOS virtual key positions translated to standard USB HID keyboard usages.
// No game bindings or layout-dependent characters: saved bindings consume the
// neutral usage in game code. Unsupported system/media keys return zero. Fixed
// table lookup, no allocation; callers serialize native event sampling. Core
// I.1/E.4 and performance MEM.9 (both phases, physical_state.hpp references)
// keep device translation separate from gameplay and bounded per event.
#pragma once
#include <array>
#include <cstdint>
namespace bw::platform::macos {
inline std::uint16_t keyboard_usage(unsigned short code) {
  constexpr std::array<std::uint16_t, 128> kUsages{
      4,   22,  7,  9,  11, 10, 29,  27,  6,   25,  0,   5,   20,  26,  8,   21,
      28,  23,  30, 31, 32, 33, 35,  34,  46,  38,  36,  45,  37,  39,  48,  18,
      24,  47,  12, 19, 40, 15, 13,  52,  14,  51,  49,  54,  56,  17,  16,  55,
      43,  44,  53, 42, 0,  41, 231, 227, 225, 57,  226, 224, 229, 230, 228, 0,
      108, 99,  0,  85, 0,  87, 0,   83,  0,   0,   0,   84,  88,  0,   86,  0,
      0,   103, 98, 89, 90, 91, 92,  93,  94,  95,  109, 96,  97,  0,   0,   0,
      62,  63,  64, 60, 65, 66, 0,   68,  0,   104, 107, 105, 0,   67,  0,   69,
      0,   106, 73, 74, 75, 76, 61,  77,  59,  78,  58,  80,  79,  81,  82,  0};
  return code < kUsages.size() ? kUsages.at(code) : 0;
}
}  // namespace bw::platform::macos
