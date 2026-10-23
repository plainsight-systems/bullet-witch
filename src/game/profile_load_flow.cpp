// See profile_load_flow.hpp for ownership, desktop adaptation and cost.
// Implement one lifecycle transition per update; storage results never bypass
// the error choice or fade completion. Retry preserves previously loaded data
// until an atomic Profile::load succeeds. Explicit continue marks storage
// disabled before the result is published. Design MCP I.30 (#ri-encapsulate),
// implementation E.6 (#re-raii), and cpp-performance MEM.9
// (guidelines/memory/MEM.9-embedded-allocate-at-init.md) apply as in the
// header.
#include "profile_load_flow.hpp"

#include <cmath>
#include <stdexcept>

namespace bw::game {
namespace {
constexpr float kRevealSeconds = 0.1F;
constexpr std::uint32_t kVerticalActions = 0x03000000U;
constexpr std::uint32_t kConfirmActions = 0x1010U;
}  // namespace
ProfileLoadFlow::ProfileLoadFlow(Profile& profile,
                                 platform::SaveStorage& storage,
                                 LogoMedia& media)
    : profile_(profile), storage_(storage), media_(media) {}
void ProfileLoadFlow::start() {
  if (progress_.status == FlowStatus::pending) {
    throw std::logic_error("Profile load already active");
  }
  phase_ = Phase::resources;
  progress_ = {FlowStatus::pending, false, 0};
  error_ = {};
  error_choice_ = 0;
  saving_enabled_ = true;
}
bool ProfileLoadFlow::error_pending() const { return phase_ == Phase::error; }
void ProfileLoadFlow::update(LogoUpdate input, bool resources_ready) {
  if (!std::isfinite(input.delta) || input.delta < 0) {
    throw std::invalid_argument(
        "Profile load delta must be finite and nonnegative");
  }
  if (progress_.status != FlowStatus::pending) return;
  switch (phase_) {
    case Phase::resources:
      if (!resources_ready) return;
      media_.get().begin_fade(FadeDirection::reveal, kRevealSeconds);
      progress_.visible = true;
      phase_ = Phase::revealing;
      break;
    case Phase::revealing:
      if (!media_.get().fade_pending()) phase_ = Phase::load;
      break;
    case Phase::load:
      error_ = profile_.get().load(storage_);
      phase_ = error_.status == platform::SaveStatus::ok ? Phase::conceal
                                                         : Phase::error;
      error_choice_ = 0;
      break;
    case Phase::error:
      if ((input.pressed_actions & kVerticalActions) != 0) error_choice_ ^= 1U;
      if ((input.pressed_actions & kConfirmActions) != 0) {
        if (error_choice_ == 0) {
          phase_ = Phase::load;
        } else {
          saving_enabled_ = false;
          phase_ = Phase::conceal;
        }
      }
      break;
    case Phase::conceal:
      media_.get().begin_fade(FadeDirection::conceal, 0);
      phase_ = Phase::closing;
      break;
    case Phase::closing:
      if (!media_.get().fade_pending()) {
        progress_.visible = false;
        phase_ = Phase::finish;
      }
      break;
    case Phase::finish:
      progress_.status = FlowStatus::complete;
      progress_.result = 1;
      break;
  }
}
}  // namespace bw::game
