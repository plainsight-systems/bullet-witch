// Source target observation, bounded ranking and retained knowledge
// transitions. Core I.11 returns copied facts and borrows sight synchronously.
// MEM.9 fixes the native three-candidate and ten-slot storage. Both
// implementation corpora read; full original contract and ownership are in
// perception.hpp.
#include "perception.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <stdexcept>

namespace bw::game {
namespace {
using Vector = math::Vector3<float>;
constexpr std::uint32_t kAcquired = 0x7532, kLost = 0x7534, kForgotten = 0x7535;
constexpr std::uint32_t kKnowledgeSightFlags = 0xf200;
constexpr float kRememberedPlayerRadiusSquared = 100;
constexpr std::uint32_t kAffiliationMask = 0xf, kSpecialAffiliation = 0x8000;
constexpr std::uint32_t kGroupEight = 8, kGroupSix = 6, kGroupTen = 10;
struct ScoreRange {
  float angle{}, close{}, far{};
};
struct Candidate {
  const PerceptionActor* actor{};
  float score{};
};
struct Scan {
  const PerceptionActor* actor{};
  bool visible{}, none_nearby{true};
};
std::array<float, 4> weights(const PerceptionInput& input, unsigned mode) {
  // Original96a800/96a840 rows; first lane is the player target, with the
  // remaining lanes selected by5aa270 from each candidate's affiliation.
  const auto affiliation = input.affiliation;
  const auto kind = affiliation & kAffiliationMask;
  if (kind == 1) return {-1, 1, -1, -1};
  if ((kind == 4 || kind == kGroupEight) &&
      !(affiliation & kSpecialAffiliation)) {
    return {-1, 1, -1, -1};
  }
  constexpr float kPlayerWeight = 3.5F, kSecondaryWeight = 1.5F;
  return {kPlayerWeight, -1, 1, mode == 0 ? kSecondaryWeight : -1};
}
float weight(const PerceptionActor& actor, const std::array<float, 4>& values) {
  if (actor.player) return values[0];
  const auto kind = actor.affiliation & kAffiliationMask;
  if (kind == 1) return values[0];
  if (kind == 2 || kind == kGroupSix || kind == kGroupTen ||
      ((kind == 4 || kind == kGroupEight) &&
       (actor.affiliation & kSpecialAffiliation))) {
    return values[1];
  }
  if (kind == 4) return values[2];
  if (kind == kGroupEight) return values[3];
  return -1;
}
float score(const Vector& displacement, const Vector& forward, float multiplier,
            bool current, ScoreRange range) {
  // 5aa490 preserves a tiny-distance branch instead of normalizing a zero ray.
  constexpr float kMinimumDistance = 1e-6F, kDistanceWeight = 10;
  const auto length = math::length(displacement);
  const auto distance = std::max(kMinimumDistance, length);
  float facing = 1;
  if (length >= kMinimumDistance) {
    facing = math::dot(math::scale(forward, 1 / length), displacement) + 1;
  }
  if (distance >= range.far) return -1;
  if (current) {
    return (kDistanceWeight / distance + std::max(0.F, facing)) * multiplier;
  }
  if (std::cos(range.angle) + 1 <= facing || distance <= range.close) {
    return (kDistanceWeight / distance + facing) * multiplier;
  }
  return -1;
}
Scan scan(const PerceptionInput& input, const PerceptionSettings& settings,
          TargetKnowledge knowledge, std::optional<std::uint64_t> current,
          unsigned mode, const PerceptionSight& sight) {
  const auto priorities = weights(input, mode);
  constexpr float kRetainedAngleScale = 1.5F, kRetainedRadiusScale = 3;
  auto angle = settings.view_angle_radians;
  auto close = settings.close_radius;
  auto far = settings.far_radius;
  if (knowledge != TargetKnowledge::unknown) angle *= kRetainedAngleScale;
  if (knowledge == TargetKnowledge::known) {
    close *= kRetainedRadiusScale;
    far *= kRetainedRadiusScale;
  }
  angle = std::min(angle, std::numbers::pi_v<float>);
  const auto nearby = std::max(settings.nearby_radius, settings.far_radius);
  std::array<Candidate, 3> candidates{};
  std::size_t count = 0;
  Scan result;
  for (const auto& actor : input.actors) {
    if (!actor.player && actor.target_eligibility <= 0) continue;
    const auto multiplier = weight(actor, priorities);
    if (multiplier < 0) continue;
    const auto target = math::add(actor.position, input.target_offset);
    const auto displacement = math::subtract(target, input.eye);
    if (math::dot(displacement, displacement) < nearby * nearby) {
      result.none_nearby = false;
    }
    const auto value =
        score(displacement, input.forward, multiplier,
              current == actor.identity && !actor.dead, {angle, close, far});
    if (value <= 0) continue;
    std::size_t at = 0;
    while (at < count && candidates.at(at).score >= value) ++at;
    if (at == candidates.size()) continue;
    for (auto i = std::min(count, candidates.size() - 1); i > at; --i) {
      candidates.at(i) = candidates.at(i - 1);
    }
    candidates.at(at) = {&actor, value};
    count = std::min(count + 1, candidates.size());
  }
  for (std::size_t i = 0; i < count; ++i) {
    const auto* actor = candidates.at(i).actor;
    if (sight.clear(input.eye, math::add(actor->position, input.target_offset),
                    kKnowledgeSightFlags)) {
      return {actor, true, result.none_nearby};
    }
  }
  if (count) result.actor = candidates[0].actor;
  return result;
}
void validate(const PerceptionInput& input) {
  if (!math::all_finite(input.eye) || !math::all_finite(input.forward) ||
      !math::all_finite(input.target_offset)) {
    throw std::invalid_argument("Invalid NPC perception pose");
  }
  for (const auto& actor : input.actors) {
    if (!actor.identity || !math::all_finite(actor.position) ||
        !std::isfinite(actor.target_eligibility)) {
      throw std::invalid_argument("Invalid NPC perception candidate");
    }
  }
}
}  // namespace
void PerceptionSchedule::advance() {
  for (std::size_t i = 0; i + 1 < counts_.size(); ++i) {
    counts_.at(i) = counts_.at(i + 1);
  }
  counts_.back() = 0;
}
std::uint8_t PerceptionSchedule::next() {
  std::size_t best = counts_.size() - 1;
  for (auto i = best - 1; i > 0; --i) {
    if (counts_.at(i) < counts_.at(best)) best = i;
  }
  if (counts_.at(best) == std::numeric_limits<std::uint32_t>::max()) {
    throw std::overflow_error("NPC perception schedule exhausted");
  }
  ++counts_.at(best);
  return static_cast<std::uint8_t>(best);
}
NpcPerception::NpcPerception(const world::WorldClock& clock,
                             PerceptionSchedule& schedule,
                             PerceptionSettings settings,
                             unsigned original_mode)
    : clock_(clock),
      schedule_(schedule),
      settings_(settings),
      mode_(original_mode) {
  const std::array values{
      settings.view_angle_radians,  settings.close_radius,
      settings.far_radius,          settings.target_change_delay,
      settings.lost_target_seconds, settings.nearby_radius};
  if (original_mode > 1 || !math::all_finite(values) ||
      std::any_of(values.begin(), values.end(),
                  [](float value) { return value < 0; })) {
    throw std::invalid_argument("Invalid original NPC perception settings");
  }
}
void NpcPerception::receive_target(std::optional<std::uint64_t> replacement,
                                   TargetKnowledge knowledge,
                                   std::span<const PerceptionActor> actors) {
  if ((replacement && *replacement == 0) ||
      (knowledge != TargetKnowledge::unknown &&
       knowledge != TargetKnowledge::searching &&
       knowledge != TargetKnowledge::known)) {
    throw std::invalid_argument("Invalid original target request");
  }
  const auto identity = replacement ? replacement : state_.target;
  const auto found = std::ranges::find(actors, identity.value_or(0),
                                       &PerceptionActor::identity);
  if (found != actors.end() && !math::all_finite(found->position)) {
    throw std::invalid_argument("Invalid target request position");
  }
  if (replacement) state_.target = replacement;
  state_.knowledge = knowledge;
  state_.visible = false;
  change_delay_ = settings_.target_change_delay;
  lost_seconds_ = settings_.lost_target_seconds;
  if (found != actors.end()) state_.last_seen_position = found->position;
}
std::optional<PerceptionTransition> NpcPerception::advance(
    const world::WorldFrame& frame, const PerceptionInput& input,
    const PerceptionSight& sight) {
  if (!frame.belongs_to(clock_) || frame.sequence() <= sequence_) {
    throw std::invalid_argument("Invalid NPC perception world frame");
  }
  validate(input);
  sequence_ = frame.sequence();
  if (!frame.admission().simulation) return {};
  change_delay_ -= frame.delta_seconds();
  if (skip_) {
    --skip_;
    const auto dead =
        std::find_if(input.actors.begin(), input.actors.end(),
                     [&](const PerceptionActor& actor) {
                       return state_.target == actor.identity && actor.dead;
                     });
    if (dead != input.actors.end()) {
      state_.target.reset();
      state_.knowledge = TargetKnowledge::searching;
    }
    return {};
  }
  skip_ = schedule_.next();
  if (change_delay_ > 0) return {};
  const auto previous = state_.knowledge;
  const auto selected =
      scan(input, settings_, previous, state_.target, mode_, sight);
  std::uint32_t message = 0;
  if (previous != TargetKnowledge::known) {
    if (selected.actor && selected.visible) {
      state_.visible = true;
      state_.target = selected.actor->identity;
      state_.knowledge = TargetKnowledge::known;
      message = kAcquired;
    } else if (previous == TargetKnowledge::searching && selected.none_nearby) {
      state_.knowledge = TargetKnowledge::unknown;
      message = kForgotten;
    }
    if (selected.actor) state_.visible = selected.visible;
  } else if (selected.actor) {
    if (state_.target != selected.actor->identity) {
      change_delay_ = settings_.target_change_delay;
    }
    state_.visible = selected.visible;
    if (selected.visible) {
      state_.target = selected.actor->identity;
      lost_seconds_ = settings_.lost_target_seconds;
      state_.last_seen_position = selected.actor->position;
    } else {
      lost_seconds_ -= frame.delta_seconds();
      const auto player = std::find_if(
          input.actors.begin(), input.actors.end(),
          [](const PerceptionActor& actor) { return actor.player; });
      const auto away =
          player == input.actors.end() ||
          math::dot(
              math::subtract(state_.last_seen_position, player->position),
              math::subtract(state_.last_seen_position, player->position)) >=
              kRememberedPlayerRadiusSquared;
      if (lost_seconds_ < 0 && away) {
        state_.knowledge = TargetKnowledge::searching;
        message = kLost;
      }
    }
  } else if (selected.none_nearby) {
    state_.knowledge = TargetKnowledge::unknown;
    message = kForgotten;
  }
  if (message) return PerceptionTransition{previous, state_.knowledge, message};
  return {};
}
}  // namespace bw::game
