// Bounded source selection contracts, independent of authored assets.
#include "game/actors/npc/contact_action_policy.hpp"

#include "../../../../fixtures/camera_fixture.hpp"
void contact_action_policy_tests() {
  using bw::game::npc_dodge_order;
  using bw::game::npc_dodge_slot;
  using bw::game::npc_grapple_direction;
  using bw::game::npc_grapple_motion;
  using camera_fixture::require;
  require(npc_dodge_order(0) == std::array<unsigned, 3>{3, 1, 2},
          "Front threat uses original nonpositive side order");
  require(npc_dodge_order(.1F) == std::array<unsigned, 3>{2, 1, 3},
          "Front positive threat reverses first flank");
  require(npc_dodge_order(-1.F) == std::array<unsigned, 3>{0, 3, 1},
          "Near left threat tries forward first");
  require(npc_dodge_order(-2.F) == std::array<unsigned, 3>{1, 3, 0},
          "Far left threat tries back first");
  require(npc_dodge_order(3.F) == std::array<unsigned, 3>{2, 0, 3},
          "Rear positive threat uses original flank order");
  require(npc_dodge_slot({3, 1, 2}, {false, false, true, false}) == 727,
          "Dodge tests only ordered admitted sensors");
  require(npc_dodge_slot({3, 1, 2}, {}) == 724,
          "Blocked dodge retains original back fallback");
  const auto ordinary = npc_grapple_motion(6);
  require(ordinary && ordinary->slot == 722 && ordinary->fresh &&
              ordinary->blend_seconds == .3,
          "AI ordinary grapple selects its own source family");
  const auto directed = npc_grapple_motion(7);
  const auto special = npc_grapple_motion(10);
  const auto alternate = npc_grapple_motion(11);
  require(directed && special && alternate && directed->slot == 723 &&
              special->slot == 830 && alternate->slot == 808,
          "AI grapple maps all original capacity substates");
  require(!npc_grapple_motion(0),
          "Other capacity attack selectors do not fabricate a motion");
  const auto limited =
      npc_grapple_direction({0, 0, 1}, {10, 0, 0}, 15.707963943481445F / 60);
  require(std::abs(limited[0] - .2588190436F) < .000001F &&
              std::abs(limited[2] - .9659257531F) < .000001F,
          "Grapple side turn matches original machine output");
  require(npc_grapple_direction({0, 0, 1}, {1, 0, 1}, .1F) ==
              std::array<float, 3>{1, 0, 1},
          "Original raw displacement dot controls early snap");
  const auto antipode = npc_grapple_direction({0, 0, 1}, {0, 0, -10}, .1F);
  require(bw::math::all_finite(antipode) && antipode[0] < 0,
          "Undefined source antipode follows pinned FixedTurn tie");
}
