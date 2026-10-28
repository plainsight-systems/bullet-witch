// Source mask ownership regressions: reset clears acquired groups rather than
// restoring initial appearance; independent motion banks are not ref-counted.
#include "game/actors/animation/model_visibility.hpp"

#include <limits>
#include <stdexcept>

namespace {
void require(bool value, const char* message) {
  if (!value) throw std::runtime_error(message);
}
}  // namespace
void model_visibility_tests() {
  using bw::game::ModelVisibility;
  constexpr std::uint32_t kAuthoredGroup = 57, kOtherGroup = 9;
  std::array<std::uint32_t, ModelVisibility::kWords> initial{};
  initial[0] = 1;
  ModelVisibility owner(initial);
  require(owner.visible(0) && !owner.visible(kAuthoredGroup),
          "Visibility copies initial appearance");
  owner.command(0, kAuthoredGroup, true);
  owner.command(0, kAuthoredGroup, true);
  require(owner.visible(kAuthoredGroup), "Positive fallback enables group57");
  owner.command(1, kOtherGroup, true);
  owner.reset(0);
  require(!owner.visible(kAuthoredGroup) && owner.visible(kOtherGroup),
          "Motion reset clears only its acquired groups");
  owner.command(0, kOtherGroup, false);
  require(owner.visible(kOtherGroup),
          "Unowned release does not clear another bank");
  owner.command(0, kOtherGroup, true);
  owner.reset(0);
  require(!owner.visible(kOtherGroup),
          "Original shared bit clearing is not cross-bank reference counting");
  owner.command(1, kOtherGroup, true);
  require(!owner.visible(kOtherGroup),
          "Retained duplicate does not reacquire bit");
  owner.command(1, kOtherGroup, false);
  owner.command(1, kOtherGroup, true);
  require(owner.visible(kOtherGroup), "Released entry may reacquire");
  owner.set_group(kAuthoredGroup, true);
  owner.command(0, kAuthoredGroup, true);
  owner.reset(0);
  require(!owner.visible(kAuthoredGroup),
          "Reset does not restore initial bits");
  constexpr std::array<std::int32_t, 4> kOutside{
      256, 998, 999, std::numeric_limits<std::int32_t>::max()};
  for (const auto target : kOutside) {
    owner.command(2, target, true);
    owner.command(2, target, false);
  }
  owner.command(2, 0, true);
  owner.command(2, 0, false);
  owner.reset(2);
  require(owner.visible(0) && owner.visible(kOtherGroup) && !owner.visible(998),
          "Zero and bounded-out fallback targets preserve model bits");
  bool rejected = false;
  try {
    owner.command(ModelVisibility::kBanks, 1, true);
  } catch (const std::out_of_range&) {
    rejected = true;
  }
  require(rejected, "Invalid bank rejects before mutation");
}
