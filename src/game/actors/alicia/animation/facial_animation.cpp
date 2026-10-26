// Bind Alicia's source names once, then keep bounded facial controller clocks
// separate from body motion. Source newest-first accumulation and fade clocks
// follow facial-animation.md. Both implementation corpora: E.12 (#re-noexcept)
// keeps callback admission explicit; MEM.9 (guidelines/memory/
// MEM.9-embedded-allocate-at-init.md) keeps all update storage prepared.
#include "facial_animation.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace bw::game {
FacialAnimation::FacialAnimation(const content::CharacterSupport& support,
                                 const animation::PreparedModelShapes& model,
                                 const std::shared_ptr<memory::Domain>& domain)
    : weights_(model.names().size(), 0, memory::Allocator<float>(domain)) {
  // The player factory asks for @pw0_1..@pw0_7; @ forces nonrepeat.
  // Slot3 has no resource. List order alone would shift every later expression.
  constexpr std::array<std::string_view, kExpressions> names{
      "pw0_1", "pw0_2", "pw0_3", "pw0_4", "pw0_5", "pw0_6", "pw0_7"};
  for (std::size_t index = 0; index < names.size(); ++index) {
    for (const auto& source : support.shapes) {
      const std::string_view publication = source.publication.name;
      const auto separator = publication.rfind('.');
      if (publication.substr(separator == std::string_view::npos
                                 ? 0
                                 : separator + 1) == names.at(index)) {
        if (clips_.at(index)) {
          throw std::invalid_argument("Duplicate Alicia expression resource");
        }
        clips_.at(index).emplace(source.definition, model, domain);
      }
    }
  }
}
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
      static_cast<std::size_t>(value.expression) >= kExpressions ||
      !std::isfinite(value.weight) || value.weight < 0 || value.duration < 0 ||
      (value.weight > 0 && value.duration == 0)) {
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
      previous.fading = true;
      previous.fade_frame = 0;
      previous.fade_duration = static_cast<float>(value.duration);
    }
  }
  if (value.weight == 0) return FacialAdmission::retired;
  if (!source_admission) return FacialAdmission::suppressed;
  const auto expression = static_cast<std::size_t>(value.expression);
  if (!std::span{clips_}[expression].has_value()) {
    return FacialAdmission::missing_expression;
  }
  const auto found =
      std::ranges::find(bank.controllers, false, &Controller::active);
  if (found == bank.controllers.end()) return FacialAdmission::exhausted;
  const auto slot = static_cast<std::size_t>(found - bank.controllers.begin());
  const auto identity = static_cast<std::size_t>(value.bank) * kSlots + slot;
  remove_order(identity);
  *found = {.expression = expression,
            .rate = 1.0F / static_cast<float>(value.duration),
            .weight = value.weight,
            .active = true};
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
      const auto& clip = clips_.at(entry.expression);
      if (!clip.has_value() ||
          !std::isfinite(elapsed * (clip->frames_per_second() * entry.rate))) {
        throw std::invalid_argument("Unrepresentable facial animation step");
      }
    }
  }
  std::ranges::fill(weights_, 0);
  // Model shape controllers are visited newest first. Their nonrepeating
  // expression frame holds at the end; only an explicit retirement fades it.
  for (std::size_t i = 0; i < order_count_; ++i) {
    const auto identity = std::span{order_}[i];
    auto& entry =
        banks_.at(identity / kSlots).controllers.at(identity % kSlots);
    if (!entry.active) continue;
    const auto& prepared = clips_.at(entry.expression);
    if (!prepared.has_value()) {
      throw std::logic_error(
          "Active facial controller lost its prepared expression");
    }
    const auto& clip = *prepared;
    const auto delta = elapsed * (clip.frames_per_second() * entry.rate);
    entry.frame = std::min(clip.duration_frames(), entry.frame + delta);
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
      if (!clips_.at(entry.expression).has_value()) entry.active = false;
    }
  }
}
}  // namespace bw::game
