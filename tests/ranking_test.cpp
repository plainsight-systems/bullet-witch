// Public absent-provider lifecycle: authored entry, original acknowledgment,
// retained ownership during conceal and validated restart/time. Core I.1 and
// MEM.9 were read in both phases; no files or network substitute for the owner.
#include <limits>
#include <stdexcept>

#include "game/ranking_flow.hpp"
namespace {
void require(bool value, const char* message) {
  if (!value) throw std::runtime_error(message);
}
}  // namespace
void ranking_contracts() {
  using bw::game::MenuSound;
  using bw::game::RankingAvailability;
  using bw::game::RankingFlow;
  using bw::game::RankingPhase;
  RankingFlow flow;
  flow.start(RankingAvailability::unavailable);
  const auto opening = flow.take_sounds();
  require(opening.size == 1 && opening.requests[0].sound == MenuSound::panel &&
              opening.requests[0].delay_seconds == .4F,
          "Ranking source panel cue");
  require(flow.take_sounds().size == 0, "Ranking cues drain once");
  flow.update({0x1000, 0}, .25F);
  require(flow.presentation().phase == RankingPhase::entering &&
              flow.presentation().opacity == 1,
          "Early confirm cannot skip source entry");
  flow.update({}, 1.55F);
  require(flow.presentation().phase == RankingPhase::unavailable,
          "Absent provider selects original failure");
  require(flow.take_sounds().requests[0].sound == MenuSound::dialog_open,
          "Failure dialog owns opening cue");
  flow.update({}, .4F);
  flow.update({0x2000, 0}, 0);
  require(flow.presentation().phase == RankingPhase::unavailable,
          "Acknowledgment requires confirm, not Back");
  flow.update({0x1000, 0}, 0);
  require(flow.take_sounds().requests[0].sound == MenuSound::dialog_close,
          "Acknowledgment owns closing cue");
  flow.update({}, .3F);
  require(flow.presentation().phase == RankingPhase::concealing &&
              !flow.completed(),
          "Dialog completion retains child until conceal");
  flow.update({}, .25F);
  require(flow.completed(), "Source conceal releases child");
  flow.start(RankingAvailability::unavailable);
  const auto before = flow.presentation();
  bool rejected = false;
  try {
    flow.update({}, std::numeric_limits<float>::quiet_NaN());
  } catch (const std::invalid_argument&) {
    rejected = true;
  }
  require(rejected && flow.presentation().elapsed == before.elapsed,
          "Nonfinite time rejected before mutation");
  rejected = false;
  try {
    flow.start(RankingAvailability::unavailable);
  } catch (const std::logic_error&) {
    rejected = true;
  }
  require(rejected, "Active child cannot restart underneath its owner");
}
