// Grant only the common receivers traced for the admitted EA0 encounter. This
// policy never classifies by a filename suffix or aliases an unknown actor.
// I.11/CDSA.32 were retrieved for implementation; prepared names remain shared.
#include "npc_pose_semantics.hpp"

#include <algorithm>
#include <stdexcept>

#include "game/actors/animation/source_motion_bindings.hpp"
#include "game/content/original/motion_symbols.hpp"
namespace bw::game {
void npc_pose_semantics(const animation::PreparedClipCatalog& catalog,
                        std::string_view owner,
                        std::span<MotionPoseSemantics> output) {
  if (output.size() != catalog.size()) {
    throw std::invalid_argument("NPC pose semantics size differs from catalog");
  }
  if (owner == "wg1") {
    constexpr std::array<std::string_view, 7> names{
        "defaultmotion", "stand_l",   "shoot_l",    "stand_reload",
        "shoot_reload",  "shoot_end", "shoot_after"};
    for (const auto name : names) {
      if (const auto clip = catalog.find(name)) {
        output[*clip] = MotionPoseSemantics::ordinary;
      }
    }
    return;
  }
  if (owner != "ea0") return;
  // Complete named-reaction union in the admitted behavior resources. The
  // StateReaction receiver samples these through the ordinary body bank;
  // unrelated symbols/clips do not acquire an inferred representation.
  constexpr std::array named_reactions{"RUNS_AWAY_A_L",
                                       "RUNS_AWAY_B_L",
                                       "DRAW_AWAY_L",
                                       "RELOAD_A",
                                       "STOOP",
                                       "FEAR_E",
                                       "FEAR_M_L",
                                       "BEG_LIFE_A_L",
                                       "BEG_LIFE_B_L",
                                       "SEARCH_L",
                                       "SPIT_SALIVA",
                                       "FEAR_S",
                                       "SURPRISE",
                                       "CALL",
                                       "FIND",
                                       "STAY",
                                       "DM_TORNADO_BA_L",
                                       "DM_TORNADO_FR_L",
                                       "DIE_BURN_L",
                                       "DM_TORNADO_START_BA",
                                       "BEG_LIFE_C_L",
                                       "DIE_CROW_L",
                                       "NECK",
                                       "DM_TORNADO_START_FR",
                                       "CALL_TRANCEIVER_E",
                                       "CALL_TRANCEIVER_M_L",
                                       "FALLS_DOWN",
                                       "CALL_TRANCEIVER_S"};
  for (const auto& alias : soldier_motion_aliases()) {
    // Common movement families, Shooter ordinary/additive layers, StateStart,
    // close/evade/fall, ordinary/death Damager and sustained-hit receivers.
    const auto slot = alias.slot;
    MotionPoseSemantics mode = MotionPoseSemantics::unknown;
    constexpr std::uint32_t shoot_begin = 208, shoot_end = 304,
                            movement_end = 655, action_begin = 656,
                            action_end = 704, reload_additive = 705,
                            contact_begin = 706, contact_end = 727,
                            death_begin = 730, death_end = 740,
                            melee_death_begin = 745, melee_death_end = 748,
                            melee_blast_begin = 839, melee_blast_end = 850,
                            blast_begin = 749, blast_end = 760,
                            grapple_alternate = 808, grapple_special = 830,
                            sustained_additive = 916, sustained_front = 918,
                            sustained_back = 919;
    if (slot < shoot_begin || (slot >= shoot_end && slot <= movement_end) ||
        (slot >= action_begin && slot <= action_end) ||
        (slot >= contact_begin && slot <= contact_end) ||
        (slot >= death_begin && slot <= death_end) ||
        (slot >= melee_death_begin && slot <= melee_death_end) ||
        (slot >= melee_blast_begin && slot <= melee_blast_end) ||
        (slot >= blast_begin && slot <= blast_end) ||
        slot == grapple_alternate || slot == grapple_special ||
        slot == sustained_front || slot == sustained_back) {
      mode = MotionPoseSemantics::ordinary;
    }
    if ((slot >= shoot_begin && slot < shoot_end) || slot == reload_additive ||
        slot == sustained_additive) {
      mode = MotionPoseSemantics::additive;
    }
    if (std::ranges::any_of(named_reactions, [slot](std::string_view name) {
          return content::original_motion_symbol(name) == slot;
        })) {
      mode = MotionPoseSemantics::ordinary;
    }
    if (mode == MotionPoseSemantics::unknown || slot >= catalog.slot_count()) {
      continue;
    }
    const auto clip = catalog.alias(slot).clip;
    if (!clip || catalog.name(*clip) != alias.name) continue;
    auto& value = output[*clip];
    if (value != MotionPoseSemantics::unknown && value != mode) {
      throw std::invalid_argument(
          "Conflicting NPC source pose representations");
    }
    value = mode;
  }
}
}  // namespace bw::game
