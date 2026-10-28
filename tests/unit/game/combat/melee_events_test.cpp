// Contract tests for original event phases and joint-keyed slot lifetimes.
// I.1 checks copied packets; MEM.9 keeps bounded actor storage observable.
#include "game/combat/melee_events.hpp"

#include "../../../fixtures/camera_fixture.hpp"
namespace {
class Emission final : public bw::game::MeleeEmission {
 public:
  std::uint32_t begin_attack() override { return ++serial; }
  void emit(const bw::game::MeleeContactVolume& packet) override {
    last = packet;
    ++emissions;
  }
  std::uint32_t serial{};
  unsigned emissions{};
  bw::game::MeleeContactVolume last;
};
}  // namespace
void melee_events_tests() {
  using bw::animation::MotionEventPhase;
  using camera_fixture::require;
  Emission output;
  bw::game::MeleeMotionEvents events(17, output);
  constexpr bw::game::MeleeMotionCommand hit{77, 7, 1, 300, 300};
  events.consume(hit, MotionEventPhase::enter, {1, 2, 3}, {4, 5, 6});
  require(output.serial == 1 && output.emissions == 0,
          "Entry only owns attack serial and initial point");
  events.consume(hit, MotionEventPhase::update, {1, 2, 3}, {4, 5, 6});
  require(output.emissions == 1 && output.last.source == 17 &&
              output.last.serial == 1 &&
              output.last.previous == bw::math::Vector3<float>{1, 5, 3} &&
              output.last.center == bw::math::Vector3<float>{4, 5, 6} &&
              output.last.damage == 300 && output.last.effect == 7,
          "Active event publishes current joint and original initial height");
  events.consume(hit, MotionEventPhase::update, {1, 2, 3}, {7, 8, 9});
  require(output.last.previous == bw::math::Vector3<float>{4, 5, 6} &&
              output.last.serial == 1,
          "Repeated active samples retain attack identity and previous point");
  events.consume(hit, MotionEventPhase::interrupt, {}, {});
  events.consume(hit, MotionEventPhase::update, {}, {});
  require(output.emissions == 2, "Interrupted hit emits nothing further");
  for (int i = 0; i < 5; ++i) {
    events.consume(hit, MotionEventPhase::enter, {}, {});
  }
  require(output.serial == 5, "Only four original slots admit an attack");
  events.consume(hit, MotionEventPhase::exit, {}, {});
  events.consume(hit, MotionEventPhase::update, {}, {});
  require(output.emissions == 2, "Exit clears all matching joint slots");
  events.consume(hit, MotionEventPhase::enter, {}, {});
  events.reset();
  events.consume(hit, MotionEventPhase::update, {}, {});
  require(output.emissions == 2, "Scoped reset retires all hit slots");
}
