// In-memory effect schema rejection and borrowed-view contracts. Synthetic
// bytes do not establish original visual fidelity. Core SL.con.3 (#rsl-bounds)
// covers invalid extents/references; I.11 (#ri-raw) keeps payload ownership in
// the fixture. Performance MEM.9 (guidelines/memory/
// MEM.9-embedded-allocate-at-init.md) is verified by allocation-free production
// views; fixture vectors use the existing test-only heap exception.
#include "bw/content/formats/effect_pack.hpp"

#include <bit>
#include <stdexcept>

#include "../../../../fixtures/model_fixture.hpp"

namespace {
using model_fixture::word;
constexpr std::array<std::size_t, 5> kOffsets{64, 256, 800, 1028, 1176};
bw::resources::Bytes fixture() {
  bw::resources::Bytes bytes(1588);
  bytes[0] = 'K';
  bytes[1] = 'P';
  bytes[2] = 'K';
  bytes[3] = 'd';
  word(bytes, 4, 7);
  word(bytes, 8, 0x9e);
  for (std::size_t i = 0; i < kOffsets.size(); ++i) {
    word(bytes, 12 + 4 * i, static_cast<std::uint32_t>(kOffsets.at(i)));
    word(bytes, kOffsets.at(i) + 8, 1);
  }
  word(bytes, 36, 1576);  // Native empty nested section has only 12 bytes.
  word(bytes, 80, 42);
  for (std::size_t i = 1; i < 16; ++i) word(bytes, 84 + 4 * i, 0xffffffff);
  word(bytes, 816 + 0xa4, 0);
  word(bytes, 816 + 0xa8, 0);
  word(bytes, 816 + 0xac, 0xffffffff);
  return bytes;
}
void require(bool value, const char* message) {
  if (!value) throw std::runtime_error(message);
}
template <class Function>
void rejects(Function function) {
  bool caught = false;
  try {
    function();
  } catch (const bw::resources::Error&) {
    caught = true;
  }
  require(caught, "Effect schema must reject malformed input");
}
}  // namespace

void effect_pack_tests() {
  const auto bytes = fixture();
  const bw::formats::EffectPackView pack(bytes);
  require(pack.effect_count() == 1 && pack.part_count() == 1 &&
              pack.definition_count() == 1 && pack.scalar_curve_count() == 1 &&
              pack.vector_curve_count() == 1,
          "Effect table counts");
  require(pack.effect(0).id == 42 && pack.effect(0).parts[0] == 0 &&
              pack.effect(0).parts[15] == -1,
          "Original ordered part slots survive decoding");
  require(pack.find_effect(42) == 0 && !pack.find_effect(99),
          "Effect lookup distinguishes missing IDs");
  require(pack.part(0).bytes().data() == std::span(bytes).subspan(272).data(),
          "Effect records borrow the original payload");
  rejects([&] { (void)pack.part(1); });
  rejects([&] { (void)pack.part(0).word(2); });
  rejects([&] { (void)pack.part(0).word(0x210); });
  auto invalid = bytes;
  word(invalid, 8, 0);
  rejects([&] { (void)bw::formats::EffectPackView(invalid); });
  invalid = bytes;
  word(invalid, 72, 0xffffffff);
  rejects([&] { (void)bw::formats::EffectPackView(invalid); });
  invalid = bytes;
  word(invalid, 84, 1);
  rejects([&] { (void)bw::formats::EffectPackView(invalid); });
  invalid = bytes;
  word(invalid, 276, 0xffffffff);
  rejects([&] { (void)bw::formats::EffectPackView(invalid); });
  invalid = bytes;
  word(invalid, 816 + 0xa4, 1);
  rejects([&] { (void)bw::formats::EffectPackView(invalid); });
  invalid = bytes;
  word(invalid, 1044, 17);
  rejects([&] { (void)bw::formats::EffectPackView(invalid); });
  invalid = bytes;
  word(invalid, 1044, 2);
  // Duplicate positive-key times must reject before sampling can divide by 0.
  rejects([&] { (void)bw::formats::EffectPackView(invalid); });
  invalid = bytes;
  invalid.pop_back();
  rejects([&] { (void)bw::formats::EffectPackView(invalid); });
}
