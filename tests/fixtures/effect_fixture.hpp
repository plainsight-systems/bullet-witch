// Synthetic original sprite pack for public admission and lifecycle tests.
// Fixed fixture bytes are not recovered game data. Shared by engine/game tests.
// Core SL.con.3 bounds records; MEM.9 keeps fixtures outside frame work.
#pragma once
#include <bit>

#include "model_fixture.hpp"
namespace effect_fixture {
using model_fixture::word;
inline void number(bw::resources::Bytes& bytes, std::size_t offset,
                   float value) {
  word(bytes, offset, std::bit_cast<std::uint32_t>(value));
}
inline bw::resources::Bytes fixture() {
  bw::resources::Bytes bytes(1588);
  bytes[0] = 'K';
  bytes[1] = 'P';
  bytes[2] = 'K';
  bytes[3] = 'd';
  word(bytes, 4, 7);
  word(bytes, 8, 0x9e);
  constexpr std::array<std::size_t, 5> offsets{64, 256, 800, 1028, 1176};
  for (std::size_t i = 0; i < offsets.size(); ++i) {
    word(bytes, 12 + 4 * i, static_cast<std::uint32_t>(offsets.at(i)));
    word(bytes, offsets.at(i) + 8, 1);
  }
  word(bytes, 36, 1576);
  constexpr std::size_t effect = 80, part = 272, definition = 816;
  word(bytes, effect, 42);
  for (std::size_t i = 1; i < 16; ++i) {
    word(bytes, effect + 4 + 4 * i, 0xffffffff);
  }
  number(bytes, effect + 0x44, 2);
  number(bytes, effect + 0x48, .25F);
  word(bytes, effect + 0x7c, 0xffffffff);
  number(bytes, part + 0x10, .2F);
  number(bytes, part + 0x28, .1F);
  word(bytes, part + 0x1c, 1);
  word(bytes, part + 0x24, 9);
  number(bytes, part + 0x80, 2);
  number(bytes, part + 0x84, 3);
  word(bytes, part + 0x184, 0xffffffff);
  number(bytes, part + 0x188, -1);
  for (std::size_t off = 0x1a0; off <= 0x1c8; off += 4) {
    word(bytes, part + off, 0xffffffff);
  }
  for (std::size_t off = 0x1d0; off <= 0x1e8; off += 4) {
    word(bytes, part + off, 0xffffffff);
  }
  word(bytes, part + 0x1ec, 0xffffffff);
  word(bytes, part + 0x1f0, 0xffffffff);
  word(bytes, definition + 4, 1);
  number(bytes, definition + 0x68, 1);
  word(bytes, definition + 0x88, 0xffffffff);
  for (std::size_t off = 0xa4; off <= 0xcc; off += 4) {
    word(bytes, definition + off, 0xffffffff);
  }
  return bytes;
}
}  // namespace effect_fixture
