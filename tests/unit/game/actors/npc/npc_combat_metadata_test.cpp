// Admission fixtures verify source defaults, typed voice conversion and
// immutable provenance. Core E.4 covers malformed candidates; MEM.9 keeps reads
// at setup.
#include "game/actors/npc/npc_combat_metadata.hpp"

#include <bit>
#include <limits>

#include "../../../../fixtures/camera_fixture.hpp"
namespace {
using camera_fixture::require;
using camera_fixture::word;
void text(bw::resources::Bytes& bytes, std::size_t at, std::string_view value) {
  std::copy(value.begin(), value.end(),
            bytes.begin() + static_cast<std::ptrdiff_t>(at));
}
void cell(bw::resources::Bytes& bytes, std::size_t column,
          std::uint32_t value) {
  word(bytes, 128 + column * 8, 1);
  word(bytes, 132 + column * 8, value);
}
bw::resources::Bytes fixture() {
  bw::resources::Bytes bytes(512);
  text(bytes, 0, "STBL");
  word(bytes, 4, 1);
  word(bytes, 28, 1);
  word(bytes, 32, 64);
  text(bytes, 64, "chara");
  word(bytes, 96, 1);
  word(bytes, 100, 31);
  word(bytes, 104, 128);
  text(bytes, 400, "ea0");
  text(bytes, 410, "leader");
  cell(bytes, 0, 100);
  cell(bytes, 1, 400);
  cell(bytes, 4, 410);
  cell(bytes, 13, std::bit_cast<std::uint32_t>(.75F));
  cell(bytes, 24, std::bit_cast<std::uint32_t>(2.9F));
  cell(bytes, 27, 17);
  return bytes;
}
}  // namespace
void npc_combat_metadata_tests() {
  auto domain =
      bw::memory::Domain::create(std::make_shared<camera_fixture::Backing>());
  auto bytes = fixture();
  const auto definition =
      bw::content::MissionPlacementDefinition::decode(bytes, domain);
  const auto& actor = definition.archetypes()[0];
  require(
      actor.defeat_magic == 17,
      "Death-source magic amount comes from independent authored column 27");
  const auto result =
      bw::game::prepare_npc_combat_metadata(definition, actor, 7);
  require(result.ai.script.group_number == 7 &&
              result.ai.script.move_range == 10 &&
              result.ai.script.attack_range_far == 10 &&
              result.ai.script.loop_flag == 0 && result.ai.view_degrees == 90 &&
              result.ai.far_radius == 60 && result.ai.close_radius == 10,
          "Placement defaults differ from environment constructor defaults");
  require(result.audio.sound_class == 8 && result.audio.voice_group == 2 &&
              result.affiliation == 2 && result.leader &&
              result.movement_rate_scale == .75F,
          "Authored voice truncation and independent movement/leader metadata");
  auto mismatch = actor;
  mismatch.id = 101;
  bool rejected = false;
  try {
    (void)bw::game::prepare_npc_combat_metadata(definition, mismatch, 7);
  } catch (const std::invalid_argument&) {
    rejected = true;
  }
  require(rejected,
          "Foreign archetype cannot silently read another source row");
  auto malformed_bytes = fixture();
  cell(malformed_bytes, 17,
       std::bit_cast<std::uint32_t>(std::numeric_limits<float>::quiet_NaN()));
  const auto malformed =
      bw::content::MissionPlacementDefinition::decode(malformed_bytes, domain);
  rejected = false;
  try {
    (void)bw::game::prepare_npc_combat_metadata(malformed,
                                                malformed.archetypes()[0], 7);
  } catch (const std::invalid_argument&) {
    rejected = true;
  }
  require(rejected, "Nonfinite perception cannot enter live actor state");
  require(bw::game::npc_target_model(1) == 0 &&
              bw::game::npc_target_model(2) == 1 &&
              bw::game::npc_target_model(0x4002) == 8 &&
              bw::game::npc_target_model(0x8000) == 11 &&
              bw::game::npc_target_model(3) == -1,
          "Source model categories retain unknown sentinel");
}
