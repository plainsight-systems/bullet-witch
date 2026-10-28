// Shared source facial banks bind explicitly supplied expression publications,
// independent of model availability and skeletal action clocks. Three banks
// retain four controllers each; FAC addresses banks1/2, retires the previous
// controller at the source frame rate, and admits the first inactive slot.
// Prepared clips own converted keys and borrow model name storage; support and
// PreparedModelShapes outlive this owner. Single-thread ownership; callbacks
// command() cannot allocate or throw and report absence/admission explicitly.
// Prepared expressions are immutable and borrowed by every instance.
// Core R.20 (#rr-owner) retains preparation with its model owner; CDSA.32
// shares decoded clips while each actor owns only clocks/weights.
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
#include "game/actors/animation/motion_presentation.hpp"
#include "game/content/original/character_support.hpp"

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
class PreparedFacialExpressions {
 public:
  static constexpr std::size_t kMaximumExpressions = 8;
  PreparedFacialExpressions(const content::CharacterSupport&,
                            const animation::PreparedModelShapes&,
                            std::span<const std::string_view> names,
                            const std::shared_ptr<memory::Domain>&);
  PreparedFacialExpressions(const PreparedFacialExpressions&) = delete;
  PreparedFacialExpressions& operator=(const PreparedFacialExpressions&) =
      delete;
  PreparedFacialExpressions(PreparedFacialExpressions&&) = delete;
  PreparedFacialExpressions& operator=(PreparedFacialExpressions&&) = delete;
  ~PreparedFacialExpressions() = default;
  struct Expression {
    std::optional<animation::PreparedShapeClip> clip;
    bool repeating{};
  };
  std::span<const Expression> expressions() const {
    return std::span(expressions_).first(count_);
  }
  std::size_t channels() const noexcept { return channels_; }

 private:
  std::array<Expression, kMaximumExpressions> expressions_;
  std::size_t count_{}, channels_{};
};
class FacialAnimation {
 public:
  FacialAnimation(const PreparedFacialExpressions& expressions,
                  const std::shared_ptr<memory::Domain>& domain);
  FacialAnimation(const FacialAnimation&) = delete;
  FacialAnimation& operator=(const FacialAnimation&) = delete;
  FacialAnimation(FacialAnimation&&) = delete;
  FacialAnimation& operator=(FacialAnimation&&) = delete;
  ~FacialAnimation() = default;
  FacialAdmission command(FacialMotionCommand command,
                          bool source_admission) noexcept;
  void advance(double seconds);
  std::span<const float> weights() const { return weights_; }
  bool available() const { return !weights_.empty(); }
  void reset() noexcept;
  void reset_motion_banks() noexcept;
  void copy_state_from(const FacialAnimation& source) noexcept;

 private:
  static constexpr std::size_t kBanks = 3, kSlots = 4;
  struct Controller {
    std::size_t expression{};
    float frame{}, rate{1}, weight{}, fade_frame{}, fade_duration{};
    bool active{}, fading{}, instant{};
  };
  struct Bank {
    std::array<Controller, kSlots> controllers{};
    std::optional<std::size_t> current;
  };
  void remove_order(std::size_t identity) noexcept;
  std::reference_wrapper<const PreparedFacialExpressions> prepared_;
  std::array<Bank, kBanks> banks_{};
  std::array<std::size_t, kBanks * kSlots> order_{};
  std::size_t order_count_{};
  memory::Vector<float> weights_;
};
}  // namespace bw::game
