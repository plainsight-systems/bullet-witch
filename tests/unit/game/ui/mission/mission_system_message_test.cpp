// Exercise original system-text timing through its public owner. The fixture
// deliberately gives page and non-English records conflicting properties so
// language-owned timing cannot silently become a global/page default.
// Core I.1 (#ri-explicit) makes intervals and borrowed content explicit;
// performance MEM.9 (guidelines/memory/MEM.9-embedded-allocate-at-init.md)
// keeps all fixture construction before bounded state advancement.
#include "game/ui/hud/mission_system_message.hpp"

#include <cmath>
#include <limits>

#include "../../../../fixtures/camera_fixture.hpp"
namespace {
using camera_fixture::require;
using camera_fixture::word;
struct Timing {
  std::string_view play{"3"}, start{"0"}, begin{"0"}, end{"0"};
};
bw::resources::Bytes fixture(Timing timing = {}) {
  constexpr std::uint32_t kRoot = 32, kEntry = 60, kPage = 80, kLanguage = 100,
                          kProperties = 132, kStrings = 180;
  constexpr std::size_t kCapacity = 512;
  bw::resources::Bytes bytes(kCapacity);
  const std::string_view signature = "STMANAGE";
  std::copy(signature.begin(), signature.end(), bytes.begin());
  constexpr std::array offsets{kRoot,     kEntry,      kPage,
                               kLanguage, kProperties, kStrings};
  for (std::size_t i = 0; i < offsets.size(); ++i) {
    word(bytes, 8 + i * 4, offsets.at(i));
  }
  std::uint32_t cursor = kStrings + 1;
  const auto text = [&](std::string_view value) {
    const auto result = cursor - kStrings;
    for (const char ch : value) {
      bytes.at(cursor++) = static_cast<std::uint8_t>(ch);
    }
    ++cursor;
    return result;
  };
  word(bytes, kRoot, text("sys"));
  word(bytes, kRoot + 12, 1);
  word(bytes, kEntry, text("0050"));
  word(bytes, kEntry + 4, 1);
  word(bytes, kPage + 4, 2);
  word(bytes, kPage + 12, 1);
  word(bytes, kLanguage, 100);
  word(bytes, kLanguage + 4, text("foreign"));
  word(bytes, kLanguage + 8, 1);
  word(bytes, kLanguage + 12, 8);
  constexpr unsigned kEnglish = 200;
  word(bytes, kLanguage + 16, kEnglish);
  word(bytes, kLanguage + 20, text("checkpoint fixture"));
  word(bytes, kLanguage + 24, 4);
  word(bytes, kLanguage + 28, 16);
  const std::array<std::string_view, 6> names{
      "playtime", "playtime", "playtime", "starttime", "beginwait", "endwait"};
  const std::array<std::string_view, 6> values{
      "-1", "-1", timing.play, timing.start, timing.begin, timing.end};
  for (std::size_t i = 0; i < names.size(); ++i) {
    word(bytes, kProperties + i * 8, text(names.at(i)));
    word(bytes, kProperties + i * 8 + 4, text(values.at(i)));
  }
  return bytes;
}
void advance_frames(bw::game::MissionSystemMessage& message, unsigned count) {
  constexpr float kFrame = 1.F / 60.F;
  for (unsigned i = 0; i < count; ++i) message.advance(kFrame);
}
}  // namespace
void mission_system_message_tests() {
  const auto bytes = fixture();
  const auto table = bw::formats::TextTable::parse(bytes);
  bw::game::MissionSystemMessage message(table);
  require(!message.view().active, "system message begins inactive");
  message.start(0);
  require(message.view().text == "checkpoint fixture" &&
              message.view().opacity == 0,
          "original English page is prepared before opacity advances");
  message.advance(0);
  require(message.view().active && message.view().opacity == 0,
          "zero admitted time does not fade text");
  advance_frames(message, 20);
  require(message.view().active && message.view().opacity == 1,
          "language playtime overrides page and foreign skip properties");
  advance_frames(message, 160);
  require(
      message.view().active && std::abs(message.view().opacity - .05F) < 1e-5F,
      "native three-second page fades over final third-second");
  advance_frames(message, 3);
  require(message.view().active && message.view().opacity == 0,
          "native completed page retains end wait through frame182");
  advance_frames(message, 1);
  require(!message.view().active, "native page ends on frame183");
  message.start(99);
  require(message.view().index == 0, "unknown system index selects first page");
  advance_frames(message, 20);
  message.cancel();
  advance_frames(message, 1);
  require(message.view().active && message.view().opacity == 1,
          "cancel enters end wait without inventing a fade");
  advance_frames(message, 1);
  require(!message.view().active, "cancel completes on second update");

  const auto delayed_bytes = fixture({"0", ".25", ".25", ".25"});
  const auto delayed_table = bw::formats::TextTable::parse(delayed_bytes);
  bw::game::MissionSystemMessage delayed(delayed_table);
  delayed.start(0);
  delayed.advance(.25F);
  require(!delayed.view().active, "start time waits against accumulated time");
  delayed.advance(.25F);
  require(!delayed.view().active, "begin wait yields its own update");
  delayed.advance(.25F);
  require(delayed.view().active && delayed.view().opacity == .75F,
          "zero playtime is an indefinite page after both waits");
  delayed.advance(60);
  require(delayed.view().active && delayed.view().opacity == 1,
          "indefinite page remains active until cancellation");
  delayed.cancel();
  delayed.advance(.25F);
  delayed.advance(.25F);
  require(delayed.view().active, "end wait checks negative before decrement");
  delayed.advance(.25F);
  require(!delayed.view().active, "authored end wait eventually retires");
  bool rejected = false;
  try {
    delayed.advance(std::numeric_limits<float>::quiet_NaN());
  } catch (const std::invalid_argument&) {
    rejected = true;
  }
  require(rejected, "invalid control time is rejected even when inactive");
}
