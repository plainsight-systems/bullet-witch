// Shared scoring routes actor facts only during each synchronous call. This
// synthetic Lua chunk was compiled from the two functions written below; it
// contains no game bytes. Core E.4 tests failed-call cleanup, reentry and
// thread rejection; MEM.9 verifies prepared mission resources serve different
// actors.
#include "game/actors/npc/npc_brain.hpp"

#include <thread>

#include "../../../../fixtures/behavior_fixture.hpp"
namespace {
using bw::game::BehaviorObservation;
using bw::game::BehaviorObservationSource;
using bw::game::BehaviorScriptSettings;
using bw::game::NpcBehaviorDefinition;
using bw::game::NpcBehaviorId;
using bw::game::NpcBehaviorLibrary;
using camera_fixture::require;
// function ok(owner) return getLevelRandom(owner) end
// function failure(owner) return missingFunction(owner) end
constexpr std::array<unsigned char, 260> kScript{
    27,  76,  117, 97,  80,  1,   4,   4,   4,   6,   8,   9,   9,   8,   182,
    9,   147, 104, 231, 245, 125, 65,  8,   0,   0,   0,   61,  40,  110, 111,
    110, 101, 41,  0,   0,   0,   0,   0,   0,   0,   0,   2,   0,   0,   0,
    0,   0,   0,   0,   0,   0,   0,   0,   0,   2,   0,   0,   0,   4,   3,
    0,   0,   0,   111, 107, 0,   4,   8,   0,   0,   0,   102, 97,  105, 108,
    117, 114, 101, 0,   2,   0,   0,   0,   0,   0,   0,   0,   1,   0,   0,
    0,   0,   1,   0,   3,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
    0,   0,   1,   0,   0,   0,   4,   15,  0,   0,   0,   103, 101, 116, 76,
    101, 118, 101, 108, 82,  97,  110, 100, 111, 109, 0,   0,   0,   0,   0,
    5,   0,   0,   0,   5,   0,   0,   1,   0,   0,   0,   2,   26,  0,   1,
    1,   27,  0,   0,   1,   27,  128, 0,   0,   0,   0,   0,   0,   2,   0,
    0,   0,   0,   1,   0,   3,   0,   0,   0,   0,   0,   0,   0,   0,   0,
    0,   0,   0,   1,   0,   0,   0,   4,   16,  0,   0,   0,   109, 105, 115,
    115, 105, 110, 103, 70,  117, 110, 99,  116, 105, 111, 110, 0,   0,   0,
    0,   0,   5,   0,   0,   0,   5,   0,   0,   1,   0,   0,   0,   2,   26,
    0,   1,   1,   27,  0,   0,   1,   27,  128, 0,   0,   5,   0,   0,   0,
    34,  0,   0,   0,   7,   0,   0,   0,   98,  0,   0,   0,   71,  0,   0,
    0,   27,  128, 0,   0};
class Facts final : public BehaviorObservationSource {
 public:
  double scalar(BehaviorObservation) const override {
    if (reenter) library->evaluate(id, *node, 42, *this);
    return value;
  }
  bool callback_on(std::string_view) const override { return false; }
  BehaviorScriptSettings script_settings() const override { return {}; }
  bool previous_labels(std::int32_t, std::int32_t) const override {
    return false;
  }
  double path_distance_squared(std::int32_t) const override { return -1; }
  double path_angle(std::int32_t) const override { return 0; }
  double value{};
  bool reenter{};
  NpcBehaviorLibrary* library{};
  NpcBehaviorId id{};
  const bw::formats::BehaviorNode* node{};
};
// Replace the fixture's intentionally truncated bytecode with our authored
// executable chunk. The schema length remains big endian inside luac.
bw::resources::Bytes source_bytes() {
  auto bytes = behavior_fixture::fixture();
  constexpr std::size_t kFixtureLuaBytes = 19;
  bytes.resize(bytes.size() - kFixtureLuaBytes);
  bw::resources::Bytes payload(4);
  const auto size = static_cast<std::uint32_t>(kScript.size());
  constexpr unsigned kByteBits = 8, kTwoBytes = 16, kThreeBytes = 24;
  payload[0] = static_cast<unsigned char>(size >> kThreeBytes);
  payload[1] = static_cast<unsigned char>(size >> kTwoBytes);
  payload[2] = static_cast<unsigned char>(size >> kByteBits);
  payload[3] = static_cast<unsigned char>(size);
  payload.insert(payload.end(), kScript.begin(), kScript.end());
  behavior_fixture::append(bytes, behavior_fixture::chunk("luac", payload));
  camera_fixture::word(bytes, 4, static_cast<std::uint32_t>(bytes.size() - 8));
  return bytes;
}
}  // namespace
void npc_brain_tests() {
  const auto domain =
      bw::memory::Domain::create(std::make_shared<camera_fixture::Backing>());
  const auto bytes = source_bytes();
  const auto tree = bw::formats::BehaviorTreeDefinition::parse(bytes, domain);
  constexpr std::size_t kHeapBytes = 128UL * 1024;
  const std::array definitions{
      NpcBehaviorDefinition{"fixture", &tree, kHeapBytes},
      NpcBehaviorDefinition{"healthy", &tree, kHeapBytes}};
  NpcBehaviorLibrary library(definitions, domain);
  const auto id = library.resolve("ai.fixture.amb");
  Facts first, second;
  first.value = .25;
  second.value = .75;
  const auto& node = tree.nodes()[1];
  require(library.evaluate(id, node, 42, first) == .25F &&
              library.evaluate(id, node, 43, second) == .75F &&
              library.evaluate(id, node, 42, first) == .25F,
          "Shared scoring leaked another actor's observations");
  first.library = &library;
  first.id = id;
  first.node = &node;
  first.reenter = true;
  bool rejected = false;
  try {
    library.evaluate(id, node, 42, first);
  } catch (const std::runtime_error&) {
    rejected = true;
  }
  require(rejected, "Shared scoring allowed reentrant actor routing");
  first.reenter = false;
  rejected = false;
  try {
    library.evaluate(id, node, 42, first);
  } catch (const bw::resources::Error&) {
    rejected = true;
  }
  require(rejected, "Terminal wrapper failure was silently resumed");
  const auto healthy = library.resolve("healthy");
  require(library.evaluate(healthy, node, 43, second) == .75F,
          "Failed score retained actor scope");
  rejected = false;
  std::thread foreign([&] {
    try {
      library.evaluate(healthy, node, 42, first);
    } catch (const std::logic_error&) {
      rejected = true;
    }
  });
  foreign.join();
  require(rejected, "Shared scoring accepted foreign thread");
  rejected = false;
  try {
    library.evaluate(healthy, node, 0, first);
  } catch (const std::logic_error&) {
    rejected = true;
  }
  require(rejected && library.evaluate(healthy, node, 42, first) == .25F,
          "Invalid identity contaminated shared VM scope");
}
