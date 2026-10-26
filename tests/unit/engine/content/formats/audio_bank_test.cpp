// Authored in-memory audio-bank contracts: cue/wave association, borrowed data,
// wrapper defaults, malformed links and transactional domain cleanup. No native
// audio or game files. Fixture-size work only. Both-phase SL.con.3
// (#rsl-bounds) checks offsets; MEM.6 (guidelines/memory/
// MEM.6-inject-allocators-behind-one-interface.md) injects test backing.
#include "bw/content/formats/audio_bank.hpp"

#include <algorithm>
#include <memory_resource>
namespace {
using Bytes = bw::resources::Bytes;
class AudioTestBacking final : public bw::memory::CpuBacking {
 public:
  void* allocate(std::size_t bytes, std::size_t alignment) noexcept override {
    try {
      return std::pmr::new_delete_resource()->allocate(bytes, alignment);
    } catch (const std::bad_alloc&) {
      return nullptr;
    }
  }
  void deallocate(void* pointer, std::size_t bytes,
                  std::size_t alignment) noexcept override {
    std::pmr::new_delete_resource()->deallocate(pointer, bytes, alignment);
  }
};
void require(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}
template <unsigned Width = 4>
void put(Bytes& bytes, std::size_t offset, std::uint32_t value) {
  for (unsigned index = 0; index < Width; ++index) {
    bytes.at(offset + index) = static_cast<std::uint8_t>(value >> (8 * index));
  }
}
void text(Bytes& bytes, std::size_t offset, std::string_view value) {
  for (const char character : value) {
    bytes.at(offset++) = static_cast<std::uint8_t>(character);
  }
}
Bytes wave_fixture() {
  Bytes bytes(176);
  text(bytes, 0, "WBND");
  put(bytes, 4, 46);
  put(bytes, 8, 44);
  put(bytes, 12, 52);
  put(bytes, 16, 96);
  put(bytes, 20, 148);
  put(bytes, 24, 24);
  put(bytes, 44, 172);
  put(bytes, 48, 4);
  put(bytes, 56, 1);
  text(bytes, 60, "sample");
  put(bytes, 124, 24);
  put(bytes, 128, 64);
  put(bytes, 132, 4);
  put(bytes, 148, 2 << 4);
  put(bytes, 152, 0x80000004);
  put(bytes, 160, 4);
  bytes.at(172) = 7;
  return bytes;
}
Bytes sound_fixture() {
  Bytes bytes(254);
  text(bytes, 0, "FBSC");
  put(bytes, 4, 1);
  constexpr std::size_t base = 24;
  text(bytes, base, "SDBK");
  put<2>(bytes, base + 4, 46);
  put<2>(bytes, base + 6, 43);
  bytes.at(base + 18) = 1;
  put<2>(bytes, base + 19, 1);
  put<2>(bytes, base + 25, 1);
  bytes.at(base + 27) = 1;
  put<2>(bytes, base + 28, 1);
  put(bytes, base + 34, 214);
  put(bytes, base + 38, 0xffffffff);
  put(bytes, base + 42, 225);
  put(bytes, base + 46, 0xffffffff);
  put(bytes, base + 50, 0xffffffff);
  put(bytes, base + 54, 0xffffffff);
  put(bytes, base + 58, 138);
  put(bytes, base + 62, 0xffffffff);
  put(bytes, base + 66, 219);
  put(bytes, base + 70, 202);
  text(bytes, base + 74, "cues");
  text(bytes, base + 138, "sample");
  bytes.at(base + 205) = 0xff;
  put<2>(bytes, base + 209, 12);
  bytes.at(base + 214) = 4;
  put(bytes, base + 215, 202);
  put(bytes, base + 219, 225);
  text(bytes, base + 225, "wind");
  return bytes;
}
Bytes complex_fixture() {
  auto bytes = sound_fixture();
  bytes.resize(278);
  constexpr std::size_t base = 24;
  std::fill(bytes.begin() + base + 202, bytes.end(), 0);
  put(bytes, base + 34, 238);
  put(bytes, base + 42, 249);
  put(bytes, base + 66, 243);
  bytes.at(base + 202) = 1;
  put<2>(bytes, base + 203, 2);
  bytes.at(base + 205) = 188;
  put<2>(bytes, base + 206, 0xff88);
  bytes.at(base + 208) = 4;
  put<2>(bytes, base + 209, 36);
  bytes.at(base + 211) = 1;
  bytes.at(base + 212) = 150;
  put(bytes, base + 213, 221);
  put<2>(bytes, base + 217, 0xff00);
  put<2>(bytes, base + 219, 1000);
  bytes.at(base + 221) = 1;
  put(bytes, base + 222, (12 << 5) | 1);
  put<2>(bytes, base + 226, 4);
  bytes.at(base + 228) = 0xff;
  bytes.at(base + 229) = 12;
  bytes.at(base + 233) = 2;
  put<2>(bytes, base + 234, 30);
  put<2>(bytes, base + 236, 60);
  bytes.at(base + 238) = 4;
  put(bytes, base + 239, 202);
  put(bytes, base + 243, 249);
  text(bytes, base + 249, "wind");
  return bytes;
}
void rejects(const Bytes& sound, const Bytes& wave,
             const std::shared_ptr<bw::memory::Domain>& domain) {
  bool failed = false;
  try {
    (void)bw::formats::AudioBankDefinition::parse({sound, wave}, domain);
  } catch (const bw::resources::Error&) {
    failed = true;
  }
  require(failed, "Malformed audio bank must fail");
  require(domain->snapshot().live_bytes == 0,
          "Audio bank failure leaks metadata");
}
}  // namespace
void audio_bank_tests() {
  const auto domain =
      bw::memory::Domain::create(std::make_shared<AudioTestBacking>());
  const auto sound = sound_fixture(), wave = wave_fixture();
  {
    auto bank = bw::formats::AudioBankDefinition::parse({sound, wave}, domain);
    require(bank.name() == "cues" && bank.waves().size() == 1 &&
                bank.cues().size() == 1,
            "Audio bank construction");
    require(bank.find_cue("wind") == 0 && !bank.find_cue("missing"),
            "Audio cue lookup");
    require(bank.cues().front().sound == 0 && !bank.cues().front().parameter,
            "Cue preserves sound association and engine-default sentinel");
    require(bank.waves().front().encoded.data() ==
                    std::span(wave).subspan(172).data() &&
                bank.waves().front().encoded.front() == 7,
            "Wave payload is borrowed");
    require(bank.events().front().type == 1 &&
                bank.wave_choices().front().wave == 0,
            "Simple sound constructs play-wave metadata");
    auto moved = std::move(bank);
    require(moved.find_cue("wind") == 0,
            "Moving metadata retains borrowed names");
  }
  require(domain->snapshot().live_bytes == 0,
          "Audio bank destruction releases metadata");
  const auto complex = complex_fixture();
  {
    const auto bank =
        bw::formats::AudioBankDefinition::parse({complex, wave}, domain);
    require(bank.sounds().front().pitch == -120 &&
                bank.tracks().front().volume == 150,
            "Complex sound retains track parameters");
    const auto& event = bank.events().front();
    require(event.timestamp == 12 && event.random_offset == 4 &&
                event.loops == 2 && event.position == 30 && event.angle == 60 &&
                event.choices.size == 1,
            "Authored play event timing and wave parameters");
  }
  auto invalid_track = complex;
  put(invalid_track, 237, 238);
  rejects(invalid_track, wave, domain);
  auto bad = sound;
  put(bad, 239, 999);
  rejects(bad, wave, domain);
  bad = sound;
  put<2>(bad, 235, 1);
  rejects(bad, wave, domain);
  bad = sound;
  put<2>(bad, 233, 1);
  rejects(bad, wave, domain);
  bad = sound;
  bad.resize(250);
  rejects(bad, wave, domain);
  auto bad_wave = wave;
  put(bad_wave, 160, 5);
  rejects(sound, bad_wave, domain);
  bad_wave = wave;
  put(bad_wave, 168, 3);
  rejects(sound, bad_wave, domain);
  bad = sound;
  put(bad, 4, 2);
  rejects(bad, wave, domain);
}
