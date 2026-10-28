// Native action chains preserve source companion order, primary completion,
// shared RNG and suspended state. Synthetic records are invented; original
// corpus/source witnesses remain separate. Core SL.con.3 checks public bounds;
// MEM.9 keeps production action storage admitted before updates.
// Core I.4 and performance CDSA.32, reread for implementation, motivate the
// producer-to-consumer regression: authored indices are prepared as motion
// families, without changing source wait timing or random-stream consumption.
// E.4/MEM.9 reread: registered action zero has an inert admitted lifecycle,
// while negative tree controls cannot become native action owners.
#include "game/actors/npc/native_actions.hpp"

#include <bit>
#include <cmath>
#include <limits>

#include "../../../../fixtures/behavior_fixture.hpp"
#include "game/actors/npc/locomotion_policy.hpp"
#include "game/actors/npc/reaction_admission.hpp"
#include "game/content/original/motion_symbols.hpp"
using camera_fixture::require;
namespace {
using bw::game::NativeActionLibrary;
using bw::game::NativeActions;
using bw::game::NpcActionCommand;
using bw::game::NpcActionPose;
using bw::game::NpcActionQueries;
using bw::game::NpcActionReceiver;
using bw::game::NpcActionTarget;
using bw::game::NpcAimCommand;
using bw::game::NpcAimSettings;
using bw::game::NpcFireCommand;
using bw::game::NpcNavigationRequest;
using bw::game::NpcPostureCommand;
using bw::game::NpcReadyWeaponCommand;
using bw::game::SourceRandom;
class Queries final : public NpcActionQueries,
                      public bw::game::NpcRouteQueries {
 public:
  bw::math::Vector3<float> initial_position() const override { return {}; }
  const bw::game::NpcRouteQueries& navigation_queries() const override {
    return *this;
  }
  bw::game::NpcNavigationObservation navigation_state() const override {
    return {};
  }
  bool traversable(const bw::math::Vector3<float>&,
                   const bw::math::Vector3<float>&) const override {
    return true;
  }
  bool occluded(const bw::math::Vector3<float>&,
                const bw::math::Vector3<float>&) const override {
    return false;
  }
  std::optional<bw::math::Vector3<float>> ground(
      const bw::math::Vector3<float>& point) const override {
    return point;
  }
  std::span<const bw::game::NpcCoverPoint> cover_points() const override {
    return {};
  }
  std::span<const bw::game::NpcCoverObstacle> cover_obstacles() const override {
    return {};
  }
  bool can_see(const bw::math::Vector3<float>&) const override { return clear; }
  NpcActionPose pose() const override {
    return {.position = {0, 0, 0},
            .forward = {0, 0, 1},
            .weapon_origin = {0, 1, 0},
            .projectile_speed = 20,
            .reaction_active = reaction_active,
            .motion_looping = motion_looping,
            .motion_completed = motion_completed,
            .target_knowledge = 3};
  }
  std::optional<NpcActionTarget> target(std::int32_t) const override {
    return NpcActionTarget{{0, 0, 10}, {0, 1, 10}, 1, true};
  }
  bw::math::Vector3<float> reaction_facing(
      std::int32_t selector) const override {
    const auto selected = target(selector);
    if (!selected) throw std::logic_error("Fixture reaction target absent");
    return bw::math::subtract(selected->position, pose().position);
  }
  std::optional<NpcActionTarget> movement_target(std::int32_t selector,
                                                 bool) const override {
    return target(selector);
  }
  std::optional<bw::math::Vector3<float>> last_message_position()
      const override {
    return message_position;
  }
  bw::math::Vector3<float> ground_destination(
      const bw::math::Vector3<float>& point, float, float) const override {
    return point;
  }
  std::span<const bw::math::Vector3<float>> patrol_points(
      std::int32_t) const override {
    return {};
  }
  float movement_speed(float speed) const override { return speed; }
  float movement_range(std::uint32_t) const override { return 0; }
  bool can_shoot(const bw::math::Vector3<float>&) const override {
    return clear;
  }
  NpcAimSettings aim_settings(std::int32_t, bool) const override {
    return {0, 0.5F, false};
  }
  bool dodge_running() const override { return false; }
  bool melee_idle() const override { return true; }
  bool voice_finished() const override { return false; }
  bool reaction_voice_finished() const override { return false; }
  std::string_view return_behavior() const override { return "return_fixture"; }
  std::optional<bw::math::Vector3<float>> message_position{
      bw::math::Vector3<float>{0, 0, 10}};
  bool clear{true};
  bool reaction_active{}, motion_looping{}, motion_completed{};
};
class Output final : public NpcActionReceiver {
 public:
  bool begin_reaction(const bw::game::NpcReactionCommand& command) override {
    commands.push_back(command);
    return true;
  }
  void submit(const NpcActionCommand& command) override {
    commands.push_back(command);
  }
  bool navigate(const NpcNavigationRequest& request) override {
    navigation = request;
    return arrived;
  }
  void reset_navigation() override { ++resets; }
  void set_navigation_side(std::int32_t) override {}
  NpcNavigationRequest navigation;
  bool arrived{};
  std::size_t resets{};
  std::vector<NpcActionCommand> commands;
};
std::uint32_t real(float value) { return std::bit_cast<std::uint32_t>(value); }
void wait_posture_tests(const std::shared_ptr<bw::memory::Domain>& domain) {
  struct PostureCase {
    std::uint32_t authored;
    std::int32_t family;
  };
  constexpr std::array cases{PostureCase{0, 0x10}, PostureCase{2, 0x90},
                             PostureCase{3, 0x20}};
  constexpr float kBaseTimeout = .5F, kTimeoutRange = .25F;
  for (const auto& posture : cases) {
    for (const bool randomized : {false, true}) {
      const std::array nodes{
          behavior_fixture::Node{.extent = 20,
                                 .parent = 0xffffffffU,
                                 .mode = 1,
                                 .action = 0,
                                 .name = "AI",
                                 .parameters = {real(1), 0}},
          behavior_fixture::Node{
              .extent = 8,
              .parent = 12,
              .mode = 1,
              .name = randomized ? "waitrandom" : "wait",
              .parameters =
                  randomized ? std::vector<std::uint32_t>{real(kBaseTimeout),
                                                          real(kTimeoutRange),
                                                          posture.authored}
                             : std::vector<std::uint32_t>{real(kBaseTimeout),
                                                          posture.authored}}};
      const auto bytes = behavior_fixture::fixture(nodes);
      const auto tree =
          bw::formats::BehaviorTreeDefinition::parse(bytes, domain);
      const std::array definitions{&tree};
      NativeActionLibrary library(definitions, domain);
      Queries queries;
      Output output;
      SourceRandom random, expected_random;
      NativeActions actions(library, queries, output, random, domain);
      const float timeout =
          kBaseTimeout +
          (randomized ? expected_random.unit() * kTimeoutRange : 0);
      actions.enter(0, {&tree, 1});
      require(output.commands.size() == 1,
              "Wait entry did not submit one posture command");
      const auto* command =
          std::get_if<NpcPostureCommand>(&output.commands.front());
      require(command != nullptr, "Wait entry did not submit posture");
      if (command == nullptr) return;
      require(command->posture == posture.family && command->speed_scale == 1 &&
                  command->stop,
              "Wait lost source posture translation, speed or stop");
      const auto selection = bw::game::select_npc_locomotion(
          {.posture = static_cast<std::uint32_t>(command->posture),
           .playback_rate = command->speed_scale,
           .stopped = command->stop});
      require(selection.motion.slot ==
                      (static_cast<std::uint32_t>(posture.family) | 1U) &&
                  selection.motion.rate == 1,
              "Wait command did not select the source stopped motion family");
      actions.advance(0, {.1F, timeout});
      require(!actions.state(0).completed,
              "Wait completed at the strict source timeout boundary");
      actions.advance(
          0, {.1F,
              std::nextafter(timeout, std::numeric_limits<float>::infinity())});
      require(actions.state(0).completion_code == 255 &&
                  random.draws() == expected_random.draws(),
              "Wait timeout or source random consumption changed");
      actions.exit(0);
    }
  }
}
void reaction_tests(const std::shared_ptr<bw::memory::Domain>& domain) {
  using bw::game::content::original_motion_symbol;
  require(original_motion_symbol("RELOAD_A") == 995 &&
              original_motion_symbol("NECK") == 993 &&
              !original_motion_symbol("unknown_fixture"),
          "Action symbols resolve original alias slots, not clip basenames");
  bw::game::NpcReactionAdmission admission;
  require(admission.admit(995, false) && !admission.admit(995, false),
          "Different actors share the same reaction cooldown key");
  admission.advance(9);
  require(admission.admit(995, true), "Authored bypass admits an occupied key");
  admission.advance(1);
  require(admission.admit(995, false),
          "Bypass does not refresh source retention");
  for (std::uint32_t key = 0; key < 10; ++key) {
    require(admission.admit(key, false), "Bounded cooldown admits new keys");
  }
  require(admission.admit(995, false), "Full table replaces a retained entry");
  const std::array nodes{
      behavior_fixture::Node{.extent = 20,
                             .parent = 0xffffffffU,
                             .mode = 1,
                             .action = 0,
                             .name = "AI",
                             .parameters = {real(1), 0}},
      behavior_fixture::Node{
          .extent = 8,
          .parent = 12,
          .mode = 1,
          .name = "reaction",
          .parameters = {0, 0, 1, 0xfffffffbU, 0, 0xffffffffU, 0}}};
  const auto bytes = behavior_fixture::fixture(nodes);
  const auto tree = bw::formats::BehaviorTreeDefinition::parse(bytes, domain);
  const std::array definitions{&tree};
  NativeActionLibrary library(definitions, domain);
  Queries queries;
  queries.reaction_active = true;
  queries.motion_completed = true;
  Output output;
  SourceRandom random;
  NativeActions action(library, queries, output, random, domain);
  action.enter(0, {&tree, 1});
  action.advance(0, {.1F, .1F});
  action.advance(0, {.1F, .2F});
  require(
      action.state(0).completion_code == 1,
      "Completed nonlooping reaction exits while actor still owns that state");
  action.exit(0);
  queries.motion_looping = true;
  action.enter(0, {&tree, 1});
  action.advance(0, {.1F, .1F});
  action.advance(0, {.1F, .2F});
  require(!action.state(0).completed,
          "Loop completion latch does not finish a repeating reaction");
  queries.reaction_active = false;
  action.advance(0, {.1F, .3F});
  action.advance(0, {.1F, .4F});
  require(action.state(0).completion_code == 1,
          "Interrupted regular reaction releases its action owner");
}
}  // namespace
void native_actions_tests() {
  auto domain =
      bw::memory::Domain::create(std::make_shared<camera_fixture::Backing>());
  wait_posture_tests(domain);
  reaction_tests(domain);
  const std::array nodes{
      behavior_fixture::Node{.extent = 20,
                             .parent = 0xffffffffU,
                             .mode = 1,
                             .action = 0,
                             .name = "AI",
                             .parameters = {real(1), 0}},
      behavior_fixture::Node{
          .extent = 8,
          .parent = 12,
          .mode = 1,
          .attached = {{.name = "readygun", .parameters = {0, 0xfffffffeU}}},
          .parameters = {real(1), 0}}};
  const auto bytes = behavior_fixture::fixture(nodes);
  const auto tree = bw::formats::BehaviorTreeDefinition::parse(bytes, domain);
  const std::array definitions{&tree};
  NativeActionLibrary library(definitions, domain);
  Queries queries;
  Output output;
  SourceRandom random;
  NativeActions actions(library, queries, output, random, domain);
  actions.enter(0, {&tree, 0});
  actions.advance(0, {.1F, 10});
  require(output.commands.empty() && !actions.state(0).completed &&
              actions.state(0).can_reconsider,
          "Registered action zero must retain an inert reconsiderable owner");
  actions.exit(0);
  auto control_nodes = nodes;
  control_nodes[0].action = -1;
  const auto control_bytes = behavior_fixture::fixture(control_nodes);
  const auto control_tree =
      bw::formats::BehaviorTreeDefinition::parse(control_bytes, domain);
  const std::array control_definitions{&control_tree};
  NativeActionLibrary control_library(control_definitions, domain);
  NativeActions control_actions(control_library, queries, output, random,
                                domain);
  bool control_rejected = false;
  try {
    control_actions.enter(0, {&control_tree, 0});
  } catch (const std::invalid_argument&) {
    control_rejected = true;
  }
  require(control_rejected, "Label control admitted as native action");
  actions.enter(0, {&tree, 1});
  require(
      output.commands.size() == 1 &&
          std::holds_alternative<NpcPostureCommand>(output.commands.front()),
      "Primary entry did not precede source companions");
  actions.advance(0, {0.1F, 1});
  require(!actions.state(0).completed && output.commands.size() == 3 &&
              std::holds_alternative<NpcAimCommand>(output.commands[1]) &&
              std::holds_alternative<NpcReadyWeaponCommand>(output.commands[2]),
          "Ready-gun companion failed or wait timeout lost strict boundary");
  actions.advance(0, {0.1F, 1.1F});
  require(actions.state(0).completion_code == 255,
          "Primary timeout outcome byte changed");
  actions.suspend(0);
  bool rejected = false;
  try {
    actions.advance(0, {0.1F, 1.2F});
  } catch (const std::logic_error&) {
    rejected = true;
  }
  require(rejected, "Suspended action advanced");
  actions.resume(0);
  actions.exit(0);
  require(!actions.state(0).completed, "Exited action retained completion");

  const std::array dodge_nodes{
      behavior_fixture::Node{.extent = 20,
                             .parent = 0xffffffffU,
                             .mode = 1,
                             .parameters = {real(1), 0}},
      behavior_fixture::Node{.extent = 8,
                             .parent = 12,
                             .mode = 1,
                             .action = 1,
                             .name = "dodge",
                             .parameters = {0xfffffffdU, 0}}};
  const auto dodge_bytes = behavior_fixture::fixture(dodge_nodes);
  const auto dodge_tree =
      bw::formats::BehaviorTreeDefinition::parse(dodge_bytes, domain);
  const std::array dodge_definitions{&dodge_tree};
  NativeActionLibrary dodge_library(dodge_definitions, domain);
  Output dodge_output;
  NativeActions dodger(dodge_library, queries, dodge_output, random, domain);
  queries.message_position = bw::math::Vector3<float>{3, 4, 5};
  dodger.enter(0, {&dodge_tree, 1});
  require(dodge_output.commands.size() == 1 &&
              std::get<bw::game::NpcDodgeCommand>(dodge_output.commands[0])
                      .target == *queries.message_position,
          "Dodge -3 consumes the retained message point, not a target actor");
  dodger.exit(0);
  queries.message_position.reset();
  bool missing_message_rejected = false;
  try {
    dodger.enter(0, {&dodge_tree, 1});
  } catch (const std::logic_error&) {
    missing_message_rejected = true;
  }
  require(missing_message_rejected,
          "Missing source message cannot be replaced by a fabricated point");
  queries.message_position = bw::math::Vector3<float>{0, 0, 10};

  const std::array shooting{
      behavior_fixture::Node{.extent = 20,
                             .parent = 0xffffffffU,
                             .mode = 1,
                             .parameters = {real(10), 0}},
      behavior_fixture::Node{
          .extent = 8,
          .parent = 12,
          .mode = 1,
          .attached = {{.name = "lookshootrandom",
                        .parameters = {0, 0, 0xfffffffeU, 0xffffffffU, real(2),
                                       0, real(1), 0}}},
          .parameters = {real(10), 0}}};
  const auto shooting_bytes = behavior_fixture::fixture(shooting);
  const auto shooting_tree =
      bw::formats::BehaviorTreeDefinition::parse(shooting_bytes, domain);
  const std::array shooting_definitions{&shooting_tree};
  NativeActionLibrary shooting_library(shooting_definitions, domain);
  Output shot_output;
  SourceRandom shot_random;
  NativeActions shooter(shooting_library, queries, shot_output, shot_random,
                        domain);
  shooter.enter(0, {&shooting_tree, 1});
  require(shot_random.draws() == 4,
          "Aim initialization and burst draws changed");
  shot_output.commands.clear();
  shooter.advance(0, {0.1F, 0.1F});
  require(shot_random.draws() == 7 && shot_output.commands.size() == 3 &&
              std::holds_alternative<NpcAimCommand>(shot_output.commands[0]) &&
              std::holds_alternative<NpcFireCommand>(shot_output.commands[1]) &&
              std::holds_alternative<NpcReadyWeaponCommand>(
                  shot_output.commands[2]) &&
              !shooter.state(0).completed,
          "Source shooting companion did not emit ordered aim/fire/ready");
  const auto target = std::get<NpcFireCommand>(shot_output.commands[1]).target;
  require(std::abs(target[0]) < 0.0001F && std::abs(target[1] - 1) < 0.0001F &&
              std::abs(target[2] - 10) < 0.0001F,
          "Zero-cone shot target changed");
  queries.clear = false;
  shot_output.commands.clear();
  shooter.advance(0, {0.1F, 0.2F});
  require(std::none_of(shot_output.commands.begin(), shot_output.commands.end(),
                       [](const auto& command) {
                         return std::holds_alternative<NpcFireCommand>(command);
                       }),
          "Occluded ordinary shot was emitted");
  const std::array follow{
      behavior_fixture::Node{.extent = 20,
                             .parent = 0xffffffffU,
                             .mode = 1,
                             .parameters = {real(10), 0}},
      behavior_fixture::Node{.extent = 8,
                             .parent = 12,
                             .mode = 1,
                             .name = "Followtocharacter",
                             .parameters = {real(10), 0, 0, real(1),
                                            0xfffffffeU, real(5), real(2)}}};
  const auto follow_bytes = behavior_fixture::fixture(follow);
  const auto follow_tree =
      bw::formats::BehaviorTreeDefinition::parse(follow_bytes, domain);
  const std::array follow_definitions{&follow_tree};
  NativeActionLibrary follow_library(follow_definitions, domain);
  Output follow_output;
  SourceRandom follow_random;
  NativeActions follower(follow_library, queries, follow_output, follow_random,
                         domain);
  follower.enter(0, {&follow_tree, 1});
  follower.advance(0, {0.1F, 0.1F});
  require(
      follow_output.resets == 1 && follow_random.draws() == 0 &&
          std::abs(follow_output.navigation.approach_start_squared - 29.36F) <
              0.0001F &&
          std::abs(follow_output.navigation.approach_stop_squared - 25.09F) <
              0.0001F &&
          follow_output.navigation.retreat_start_squared == 4 &&
          std::abs(follow_output.navigation.retreat_distance - 6.6F) < 0.0001F,
      "Follow movement lost source posture radius/hysteresis or consumed "
      "random");
  follow_output.arrived = true;
  follower.advance(0, {0.1F, 0.2F});
  require(follower.state(0).completion_code == 1,
          "Source follow completion not published");
  auto message_nodes = follow;
  message_nodes[1].name = "flocktomessageB";
  message_nodes[1].parameters = {real(10), 0, real(1), real(3), 0, 0, 0, 0};
  const auto message_bytes = behavior_fixture::fixture(message_nodes);
  const auto message_tree =
      bw::formats::BehaviorTreeDefinition::parse(message_bytes, domain);
  const std::array message_definitions{&message_tree};
  NativeActionLibrary message_library(message_definitions, domain);
  Output message_output;
  SourceRandom message_random;
  NativeActions message_movement(message_library, queries, message_output,
                                 message_random, domain);
  message_movement.enter(0, {&message_tree, 1});
  message_movement.advance(0, {.1F, .1F});
  require(message_output.navigation.destination[1] == .5F &&
              message_output.navigation.destination[2] == 3.F,
          "Message flock must decode eight words and use message position");
  auto cover_nodes = follow;
  cover_nodes[1].name = "hideaway";
  cover_nodes[1].parameters = {real(10), 0, real(1), 0xfffffffeU, real(3)};
  const auto cover_bytes = behavior_fixture::fixture(cover_nodes);
  const auto cover_tree =
      bw::formats::BehaviorTreeDefinition::parse(cover_bytes, domain);
  const std::array cover_definitions{&cover_tree};
  NativeActionLibrary cover_library(cover_definitions, domain);
  Output cover_output;
  SourceRandom cover_random;
  NativeActions hiding(cover_library, queries, cover_output, cover_random,
                       domain);
  hiding.enter(0, {&cover_tree, 1});
  hiding.advance(0, {.1F, .1F});
  require(
      cover_output.navigation.destination[2] == -4 && cover_random.draws() == 0,
      "Hideaway must begin with source distance-plus-one retreat");
  hiding.advance(0, {.1F, .2F});
  hiding.advance(0, {.1F, .3F});
  hiding.advance(0, {.1F, .4F});
  require(hiding.state(0).completion_code == 2,
          "Unsuccessful retained cover request must publish source unavailable "
          "outcome");
}
