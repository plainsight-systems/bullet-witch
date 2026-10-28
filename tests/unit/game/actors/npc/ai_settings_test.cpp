// Synthetic settings exercise native table selection and constructor defaults.
// Core E.4 rejects malformed candidates; MEM.9 retains copied fixed data.
#include "game/actors/npc/ai_settings.hpp"

#include <limits>
#include <numbers>

#include "../../../../fixtures/camera_fixture.hpp"
namespace {
bw::resources::Bytes fixture() {
  using camera_fixture::scalar;
  using camera_fixture::word;
  bw::resources::Bytes bytes(368);
  word(bytes, 0, 0x4d545343);
  word(bytes, 4, 0x04030201);
  word(bytes, 12, 32);
  word(bytes, 16, 128);
  const std::array names{std::string_view("witch"),
                         std::string_view("aiparam"),
                         std::string_view("searchTarget_"),
                         std::string_view("shoot"),
                         std::string_view("id0"),
                         std::string_view("id1"),
                         std::string_view("id2"),
                         std::string_view("id3"),
                         std::string_view("id4")};
  const std::array<std::size_t, 9> offsets{0,   16,  32,  64, 80,
                                           112, 144, 176, 208};
  std::size_t name_offset = 0;
  for (std::size_t i = 0; i < names.size(); ++i) {
    const auto base = 128 + offsets.at(i);
    std::uint32_t sibling = 0xffffffffU, child = 0xffffffffU;
    if (i == 0) child = 16;
    if (i == 1) child = 32;
    if (i == 2) sibling = 64;
    if (i == 3) child = 80;
    if (i >= 4 && i < 8) {
      sibling = static_cast<std::uint32_t>(offsets.at(i + 1));
    }
    word(bytes, base, sibling);
    word(bytes, base + 4, child);
    word(bytes, base + 8, static_cast<std::uint32_t>(name_offset));
    for (char c : names.at(i)) {
      bytes.at(32 + name_offset++) = static_cast<std::uint8_t>(c);
    }
    ++name_offset;
    if (i >= 4) {
      scalar(bytes, base + 16, static_cast<float>(i));
      scalar(bytes, base + 20, 1);
      bytes.at(base + 24) = i == 5 ? 1 : 0;
    }
  }
  scalar(bytes, 176, 3);
  scalar(bytes, 180, 70);
  scalar(bytes, 184, 80);
  // Append the original sibling boid group and its five fixed records.
  const std::size_t group = bytes.size();
  bytes.resize(group + 16 + std::size_t{5} * 56);
  word(bytes, 128 + 64, static_cast<std::uint32_t>(group - 128));
  word(bytes, group, 0xffffffffU);
  word(bytes, group + 4, static_cast<std::uint32_t>(group + 16 - 128));
  word(bytes, group + 8, static_cast<std::uint32_t>(name_offset));
  for (char c : std::string_view("boid")) {
    bytes.at(32 + name_offset++) = static_cast<std::uint8_t>(c);
  }
  ++name_offset;
  for (std::size_t i = 0; i < 5; ++i) {
    const auto base = group + 16 + i * 56;
    word(bytes, base,
         i == 4 ? 0xffffffffU : static_cast<std::uint32_t>(base + 56 - 128));
    word(bytes, base + 4, 0xffffffffU);
    word(bytes, base + 8, static_cast<std::uint32_t>(name_offset));
    bytes.at(32 + name_offset++) = 'i';
    bytes.at(32 + name_offset++) = 'd';
    bytes.at(32 + name_offset++) = static_cast<std::uint8_t>('0' + i);
    ++name_offset;
    scalar(bytes, base + 16, -.1F);
    scalar(bytes, base + 16 + 12, static_cast<float>(i + 2));
    scalar(bytes, base + 16 + 20, static_cast<float>(i + 3));
  }
  return bytes;
}
}  // namespace
void ai_settings_tests() {
  using camera_fixture::require;
  require(bw::game::npc_movement_settings(-1).original_posture == 0x10 &&
              bw::game::npc_movement_settings(13).radius == .3F &&
              bw::game::npc_movement_settings(1).original_posture == 0 &&
              bw::game::npc_movement_settings(1).radius == .1F &&
              bw::game::npc_movement_settings(12).original_posture == 0xc0 &&
              bw::game::npc_movement_settings(12).radius == .3F,
          "Original movement records preserve posture and radius fallbacks");
  auto domain =
      bw::memory::Domain::create(std::make_shared<camera_fixture::Backing>());
  auto bytes = fixture();
  const auto tree = bw::formats::CustomizationDefinition::parse(bytes, domain);
  const auto settings = bw::game::NpcAiSettings::parse(tree);
  require(settings.steering(0).cohesion_cosine == -.1F,
          "Source signed steering cone is preserved");
  require(settings.navigation(2, true).retreat_arrival_squared == 4 &&
              settings.navigation(2, true).idle_threshold_adjustment == 7.5F &&
              settings.navigation(2, false).retreat_arrival_squared == 0,
          "Authored movement settings and original disabled BSS fallback");
  const auto player = settings.aim(1, true), npc = settings.aim(1, false);
  require(std::abs(player.cone_radians -
                   5.F * std::numbers::pi_v<float> / 180.F) < .000001F &&
              std::abs(npc.cone_radians -
                       8.F * std::numbers::pi_v<float> / 180.F) < .000001F &&
              player.predict_target && npc.predict_target,
          "NPC spread uses fifth table while prediction retains actor mode");
  require(settings.aim(-1, true).cone_radians ==
                  settings.aim(99, true).cone_radians &&
              !settings.aim(0, true).predict_target,
          "Invalid native mode selects mode zero");
  bw::game::NpcActorAiSettings actor;
  const auto view = settings.perception(actor);
  require(actor.script.group_number == -1 && actor.script.move_range == 100 &&
              actor.script.attack_range_near == 10 &&
              actor.script.attack_range_middle == 50 &&
              actor.script.attack_range_far == 100 && view.close_radius == 10 &&
              view.far_radius == 60 && view.target_change_delay == 3 &&
              view.lost_target_seconds == 70 && view.nearby_radius == 80,
          "Actor constructor defaults remain distinct from Lua zero fallback "
          "values");
  actor.view_degrees = 200;
  require(settings.perception(actor).view_angle_radians ==
              std::numbers::pi_v<float>,
          "Native angle clamp preserves authored radii");
  camera_fixture::scalar(bytes, 224, std::numeric_limits<float>::infinity());
  bool failed = false;
  try {
    (void)bw::game::NpcAiSettings::parse(tree);
  } catch (const bw::resources::Error&) {
    failed = true;
  }
  require(failed, "Nonfinite original aim records reject at admission");
}
