// Bind supplied source expression names once, then keep bounded facial
// controller clocks separate from body motion. Source newest-first accumulation
// and fade clocks follow facial-animation.md. Both implementation corpora: E.12
// (#re-noexcept) keeps callback admission explicit; MEM.9 (guidelines/memory/
// MEM.9-embedded-allocate-at-init.md) keeps all update storage prepared.
#include "facial_animation.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace bw::game {
PreparedFacialExpressions::PreparedFacialExpressions(
    const content::CharacterSupport& support,
    const animation::PreparedModelShapes& model,
    std::span<const std::string_view> names,
    const std::shared_ptr<memory::Domain>& domain)
    : count_(names.size()), channels_(model.names().size()) {
  if (names.size() > expressions_.size()) {
    throw std::invalid_argument("Expression publication capacity exceeded");
  }
  for (std::size_t index = 0; index < names.size(); ++index) {
    auto name = names[index];
    auto& expression = expressions_.at(index);
    expression.repeating = !name.starts_with('@');
    if (!expression.repeating) name.remove_prefix(1);
    if (name.empty()) continue;
    for (const auto& source : support.shapes) {
      const std::string_view publication = source.publication.name;
      const auto separator = publication.rfind('.');
      if (publication.substr(separator == std::string_view::npos
                                 ? 0
                                 : separator + 1) == name) {
        if (expression.clip) {
          throw std::invalid_argument("Duplicate expression resource");
        }
        expression.clip.emplace(source.definition, model, domain);
      }
    }
  }
}
FacialAnimation::FacialAnimation(const PreparedFacialExpressions& expressions,
                                 const std::shared_ptr<memory::Domain>& domain)
    : prepared_(expressions),
      weights_(expressions.channels(), 0, memory::Allocator<float>(domain)) {}
void FacialAnimation::remove_order(std::size_t identity) noexcept {
  for (std::size_t i = 0; i < order_count_; ++i) {
    if (std::span{order_}[i] != identity) continue;
    for (std::size_t j = i + 1; j < order_count_; ++j) {
      std::span{order_}[j - 1] = std::span{order_}[j];
    }
    --order_count_;
    return;
  }
}
FacialAdmission FacialAnimation::command(FacialMotionCommand value,
                                         bool source_admission) noexcept {
  if (value.bank < 0 || static_cast<std::size_t>(value.bank) >= kBanks ||
      value.expression < 0 ||
      static_cast<std::size_t>(value.expression) >=
          prepared_.get().expressions().size() ||
      !std::isfinite(value.weight) || value.weight < 0 || value.duration < 0) {
    return FacialAdmission::invalid;
  }
  auto& bank = std::span{banks_}[static_cast<std::size_t>(value.bank)];
  // Retiring the previous expression happens even when the incoming resource
  // is missing, its weight is zero, or source visibility admission suppresses
  // it.
  if (bank.current.has_value()) {
    auto& previous = std::span{bank.controllers}[*bank.current];
    if (previous.active) {
      previous.rate = 1;
      previous.instant = false;
      previous.fading = true;
      previous.fade_frame = 0;
      previous.fade_duration = static_cast<float>(value.duration);
    }
  }
  if (value.weight == 0) return FacialAdmission::retired;
  if (!source_admission) return FacialAdmission::suppressed;
  const auto expression = static_cast<std::size_t>(value.expression);
  if (!prepared_.get().expressions()[expression].clip.has_value()) {
    return FacialAdmission::missing_expression;
  }
  if (value.duration == 0 &&
      prepared_.get().expressions()[expression].repeating) {
    return FacialAdmission::invalid;
  }
  const auto found =
      std::ranges::find(bank.controllers, false, &Controller::active);
  if (found == bank.controllers.end()) return FacialAdmission::exhausted;
  const auto slot = static_cast<std::size_t>(found - bank.controllers.begin());
  const auto identity = static_cast<std::size_t>(value.bank) * kSlots + slot;
  remove_order(identity);
  *found = {.expression = expression,
            .rate = value.duration == 0
                        ? 0.F
                        : 1.0F / static_cast<float>(value.duration),
            .weight = value.weight,
            .active = true,
            .instant = value.duration == 0};
  bank.current = slot;
  for (std::size_t i = order_count_; i > 0; --i) {
    std::span{order_}[i] = std::span{order_}[i - 1];
  }
  order_[0] = identity;
  ++order_count_;
  return available() ? FacialAdmission::applied
                     : FacialAdmission::absent_shapes;
}
void FacialAnimation::advance(double seconds) {
  if (!std::isfinite(seconds) || seconds < 0 ||
      seconds > static_cast<double>(std::numeric_limits<float>::max())) {
    throw std::invalid_argument("Invalid facial animation time step");
  }
  const auto elapsed = static_cast<float>(seconds);
  // The source stores fps*rate first, then multiplies by the float model dt.
  // Validate every active clock before touching the published weights/state.
  for (const auto& bank : banks_) {
    for (const auto& entry : bank.controllers) {
      if (!entry.active) continue;
      const auto& clip = prepared_.get().expressions()[entry.expression].clip;
      if (!clip.has_value() ||
          !std::isfinite(entry.frame +
                         elapsed * (clip->frames_per_second() * entry.rate))) {
        throw std::invalid_argument("Unrepresentable facial animation step");
      }
    }
  }
  std::ranges::fill(weights_, 0);
  // Visit newest first. The resource producer selects wrapping NPC clocks or
  // clamped Alicia clocks; only explicit retirement starts the fade.
  for (std::size_t i = 0; i < order_count_; ++i) {
    const auto identity = std::span{order_}[i];
    auto& entry =
        banks_.at(identity / kSlots).controllers.at(identity % kSlots);
    if (!entry.active) continue;
    const auto& prepared = prepared_.get().expressions()[entry.expression].clip;
    if (!prepared.has_value()) {
      throw std::logic_error(
          "Active facial controller lost its prepared expression");
    }
    const auto& clip = *prepared;
    const auto delta = elapsed * (clip.frames_per_second() * entry.rate);
    // Zero-duration nonrepeat FAC reaches the endpoint on its first live step.
    // Represent that finite result directly instead of storing reciprocal
    // infinity.
    const float advanced = entry.instant && seconds > 0 ? clip.duration_frames()
                                                        : entry.frame + delta;
    entry.frame = prepared_.get().expressions()[entry.expression].repeating &&
                          clip.duration_frames() > 0
                      ? std::fmod(advanced, clip.duration_frames())
                      : std::min(clip.duration_frames(), advanced);
    float weight = entry.weight;
    if (entry.fading) {
      entry.fade_frame =
          std::min(entry.fade_duration, entry.fade_frame + delta);
      if (entry.fade_frame >= entry.fade_duration) {
        entry.active = false;
        continue;
      }
      weight *= 1.0F - entry.fade_frame / entry.fade_duration;
    }
    clip.accumulate(entry.frame, weight, weights_);
  }
}
void FacialAnimation::reset() noexcept {
  banks_ = {};
  order_count_ = 0;
  std::ranges::fill(weights_, 0);
}
void FacialAnimation::reset_motion_banks() noexcept {
  for (std::size_t bank = 1; bank < kBanks; ++bank) {
    banks_.at(bank) = {};
    for (std::size_t slot = 0; slot < kSlots; ++slot) {
      remove_order(bank * kSlots + slot);
    }
  }
  std::ranges::fill(weights_, 0);
}
void FacialAnimation::copy_state_from(const FacialAnimation& source) noexcept {
  banks_ = source.banks_;
  order_ = source.order_;
  order_count_ = source.order_count_;
  for (auto& bank : banks_) {
    for (auto& entry : bank.controllers) {
      if (entry.expression >= prepared_.get().expressions().size() ||
          !prepared_.get().expressions()[entry.expression].clip.has_value()) {
        entry.active = false;
      }
    }
  }
}
}  // namespace bw::game
