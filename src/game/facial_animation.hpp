// Alicia's source facial banks bind seven named expression slots (one absent),
// independent of model availability and skeletal action clocks. Three banks
// retain four controllers each; FAC addresses banks1/2, retires the previous
// controller at the source frame rate, and admits the first inactive slot.
// Prepared clips own converted keys and borrow model name storage; support and
// PreparedModelShapes outlive this owner. Single-thread ownership; callbacks
// command() cannot allocate or throw and report absence/admission explicitly.
// advance() alone advances clocks and sums reusable weights; paused evaluation
// can use advance(0). Source nonrepeat expression clocks clamp at their end;
// replacement fades use frame units, not seconds. No automatic blinking or
// audio-driven bank0 is invented here. reset_motion_banks preserves bank0;
// reset clears all. State transfer preserves clocks during costume publication,
// rebinding immutable prepared clips to the new model. Absent costumes retain
// logical clocks but publish empty weights, an in-session switching
// improvement. Setup O(keys); updates O(12*channels), fixed storage/no frame
// allocations. Core E.12 (#re-noexcept) gives callbacks explicit outcomes;
// Performance MEM.9 (guidelines/memory/MEM.9-embedded-allocate-at-init.md)
// allocates only at setup. Both design corpora consulted. Source/reference:
// facial-animation.md.
#pragma once

#include "bw/animation/shape_pose.hpp"
#include "bw/content/character_support.hpp"
#include "motion_presentation.hpp"

namespace bw::game {
enum class FacialAdmission : std::uint8_t {
  applied,
  retired,
  absent_shapes,
  missing_expression,
  suppressed,
  exhausted,
  invalid
};
class FacialAnimation {
 public:
  FacialAnimation(const content::CharacterSupport& support,
                  const animation::PreparedModelShapes& model,
                  const std::shared_ptr<memory::Domain>& domain);
  FacialAdmission command(FacialMotionCommand command,
                          bool source_admission) noexcept;
  void advance(double seconds);
  std::span<const float> weights() const { return weights_; }
  bool available() const { return !weights_.empty(); }
  void reset() noexcept;
  void reset_motion_banks() noexcept;
  void copy_state_from(const FacialAnimation& source) noexcept;

 private:
  static constexpr std::size_t kExpressions = 7, kBanks = 3, kSlots = 4;
  struct Controller {
    std::size_t expression{};
    float frame{}, rate{1}, weight{}, fade_frame{}, fade_duration{};
    bool active{}, fading{};
  };
  struct Bank {
    std::array<Controller, kSlots> controllers{};
    std::optional<std::size_t> current;
  };
  void remove_order(std::size_t identity) noexcept;
  std::array<std::optional<animation::PreparedShapeClip>, kExpressions> clips_;
  std::array<Bank, kBanks> banks_{};
  std::array<std::size_t, kBanks * kSlots> order_{};
  std::size_t order_count_{};
  memory::Vector<float> weights_;
};
}  // namespace bw::game
