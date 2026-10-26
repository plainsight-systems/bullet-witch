// Native post-result power-up presentation values and narrow output capability.
// The flow publishes a copied progression and selection; presentation cannot
// purchase or mutate the Record. Resource owners implement begin/ready/show/end
// on the main thread. show borrows values for the call and receives frontend
// presentation time, never a mutable clock. end is idempotent and unwinds
// partial setup. Fixed six-upgrade domain; prepared resources and bounded frame
// commands avoid frame allocation. Design Core F.7 (#rf-smart) keeps nonowning
// inputs as references; MEM.9
// (guidelines/memory/MEM.9-embedded-allocate-at-init.md) retains
// initialization-time storage. See power-up-presentation.md for source mapping.
#pragma once
#include <array>
#include <cstddef>

#include "progression.hpp"
namespace bw::game {
enum class PowerUpInputLayout : std::uint8_t { keyboard, controller };
enum class PowerUpPage : std::uint8_t { categories, upgrades, confirmation };
enum class PowerUpNotice : std::uint8_t {
  none,
  insufficient_points,
  maximum_level
};
struct PowerUpPresentation {
  static constexpr float kOuterDuration = 1.2F, kChildDuration = .7F;
  PowerUpPage page{PowerUpPage::categories};
  PowerUpInputLayout input_layout{PowerUpInputLayout::keyboard};
  UpgradeCategory category{UpgradeCategory::stat};
  std::size_t focused{};
  unsigned upgrade_row{};
  Progression progression;
  PowerUpNotice notice{PowerUpNotice::none};
  // Elapsed values belong to the frontend task. Output only samples the source
  // tracks; it never advances clocks or admits input.
  float outer_seconds{kOuterDuration}, child_seconds{kChildDuration},
      dialog_progress{1.F};
  bool outer_exiting{}, child_exiting{}, dialog_exiting{};
  bool dialog_continue_visible{};
  float wallet_opacity{1}, category_description_opacity{1},
      category_selection_strength{1}, child_highlight_strength{};
  float category_bob{}, outer_prompt_opacity{1}, child_prompt_opacity{1};
  std::array<float, 4> category_offsets{};
};
class PowerUpOutput {
 public:
  PowerUpOutput() = default;
  virtual ~PowerUpOutput() = default;
  PowerUpOutput(const PowerUpOutput&) = delete;
  PowerUpOutput& operator=(const PowerUpOutput&) = delete;
  PowerUpOutput(PowerUpOutput&&) = delete;
  PowerUpOutput& operator=(PowerUpOutput&&) = delete;
  virtual void begin() = 0;
  virtual bool ready() = 0;
  virtual void show(const PowerUpPresentation& presentation) = 0;
  virtual void end() noexcept = 0;
};
}  // namespace bw::game
