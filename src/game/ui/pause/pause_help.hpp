// Original pause Help task: learned-topic selection, explicit reading and
// bounded scrolling. Owns only UI state; the borrowed MissionHelpState can mark
// an available topic read, never unlock or alter gameplay. Mission Help and its
// dependencies outlive this stable main-thread flow. Inputs are neutral menu
// edges/repeats, supplied presentation delta and a source-scaled scroll
// displacement. Opening/closing consume input. Content line counts are prepared
// once by the resource owner. Algorithm: copy topic availability -> animate
// opening -> select -> mark read and scroll -> return to list -> animate
// closing. Invalid timing/content throws before mutation. O(33) setup, O(1)
// updates, no steady-state allocations. Both phases: Core I.1 (#ri-explicit),
// C.9 (#rc-private) restrict authority; performance MEM.9
// (guidelines/memory/MEM.9-embedded-allocate-at-init.md) fixes storage.
// Source/layout evidence: native-pause-help.md.
#pragma once
#include <array>
#include <cstddef>

#include "bw/math/scalar.hpp"
#include "game/mission/mission_help_state.hpp"
#include "game/ui/audio/menu_audio.hpp"
#include "game/ui/common/help_input_icons.hpp"
#include "game/ui/common/menu_prompt.hpp"
#include "game/ui/frontend/menu_flow.hpp"

namespace bw::game {
enum class PauseHelpPhase : std::uint8_t {
  opening,
  topics,
  reading,
  closing,
  complete
};
struct PauseHelpPresentation {
  MenuPrompts prompts{menu_prompts(true)};
  HelpInputIcons input_icons{
      help_input_icons(controller_default_settings(), false)};
  PauseHelpPhase phase{PauseHelpPhase::complete};
  std::array<unsigned, help_topic_count> topics{};
  std::array<bool, help_topic_count> read{};
  std::size_t count{}, selected{};
  float elapsed{}, scroll_pixels{}, maximum_scroll{}, list_offset{},
      reading_strength{}, panel_focus{}, highlight{};
  std::array<float, help_topic_count> topic_strength{};
};
// Presentation capability only: the caller cannot acquire/release resources.
class PauseHelpOutput {
 public:
  PauseHelpOutput() = default;
  virtual ~PauseHelpOutput() = default;
  PauseHelpOutput(const PauseHelpOutput&) = delete;
  PauseHelpOutput& operator=(const PauseHelpOutput&) = delete;
  PauseHelpOutput(PauseHelpOutput&&) = delete;
  PauseHelpOutput& operator=(PauseHelpOutput&&) = delete;
  virtual void show(const PauseHelpPresentation& presentation) = 0;
};
class PauseHelpFlow {
 public:
  explicit PauseHelpFlow(MissionHelpState& control) : control_(control) {}
  ~PauseHelpFlow() = default;
  PauseHelpFlow(const PauseHelpFlow&) = delete;
  PauseHelpFlow& operator=(const PauseHelpFlow&) = delete;
  PauseHelpFlow(PauseHelpFlow&&) = delete;
  PauseHelpFlow& operator=(PauseHelpFlow&&) = delete;
  void start(const std::array<unsigned, help_topic_count>& line_counts);
  void update(MenuInput input, double presentation_delta,
              float scroll_displacement = 0);
  bool completed() const { return view_.phase == PauseHelpPhase::complete; }
  const PauseHelpPresentation& presentation() const { return view_; }

  // Consume only this update's native UI cues; no audio device authority is
  // exposed to navigation. The next update replaces an unconsumed batch.
  MenuSoundRequests take_sounds() {
    const auto result = sounds_;
    sounds_ = {};
    return result;
  }

 private:
  MenuSoundRequests sounds_;
  void select(std::size_t selected);
  void advance_visuals(double seconds);
  math::DampedResponseState<double> list_motion_, scroll_motion_, panel_motion_;
  double scroll_target_{}, pulse_seconds_{};
  MissionHelpState& control_;
  std::array<unsigned, help_topic_count> lines_{};
  PauseHelpPresentation view_;
};
}  // namespace bw::game
