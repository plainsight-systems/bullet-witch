// Original Leaderboards entry and provider-failure lifecycle. The session
// passes its real provider availability; this owner neither fabricates scores
// nor represents an absent Steam service as a successful empty table.
// Presentation time drives authored entry/reveal and acknowledgment/conceal
// independently of world time. The copied snapshot and bounded sound batch own
// no resources. Core I.1 (#ri-explicit) and performance MEM.9
// (guidelines/memory/ MEM.9-embedded-allocate-at-init.md), read for design,
// keep availability explicit and updates allocation-free. Source:
// frontend-leaderboards.md.
#pragma once
#include "game/ui/common/menu_dialog.hpp"
#include "game/ui/common/menu_prompt.hpp"
namespace bw::game {
enum class RankingAvailability : std::uint8_t { unavailable };
enum class RankingPhase : std::uint8_t {
  complete,
  entering,
  unavailable,
  concealing
};
struct RankingPresentation {
  RankingPhase phase{RankingPhase::complete};
  float elapsed{}, opacity{}, dialog_progress{};
  MenuPrompts prompts{menu_prompts(true)};
};
class RankingOutput {
 public:
  RankingOutput() = default;
  virtual ~RankingOutput() = default;
  RankingOutput(const RankingOutput&) = delete;
  RankingOutput& operator=(const RankingOutput&) = delete;
  RankingOutput(RankingOutput&&) = delete;
  RankingOutput& operator=(RankingOutput&&) = delete;
  virtual void begin() = 0;
  virtual bool ready() = 0;
  virtual void show(const RankingPresentation& presentation) = 0;
  virtual void end() noexcept = 0;
};
class RankingFlow {
 public:
  void start(RankingAvailability availability);
  void update(MenuInput input, float seconds);
  bool completed() const { return view_.phase == RankingPhase::complete; }
  const RankingPresentation& presentation() const { return view_; }
  MenuSoundRequests take_sounds();

 private:
  RankingPresentation view_;
  MenuDialog dialog_;
  MenuSoundRequests sounds_;
};
}  // namespace bw::game
