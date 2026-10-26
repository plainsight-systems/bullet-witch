// Debug separation: Core I.1 (#ri-explicit), R.3 (#rr-ptr), and performance
// TLM.1 (guidelines/telemetry/TLM.1-compile-telemetry-out-by-default.md),
// retrieved in design and implementation, keep preview capabilities bounded
// and compile out their owner/state/calls in ordinary production builds.
// Implement the owner contract in the matching header. Design I.1/C.9 keeps
// authoritative state private; implementation E.6 (#re-raii) preserves cleanup
// on failed preparation. Both-phase MEM.9
// (guidelines/memory/MEM.9-embedded-allocate-at-init.md) keeps retained buffers
// fixed during admitted frames. Existing source algorithms and ordering remain.
#include "player_animation_review.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <stdexcept>

#include "bw/content/formats/binary_reader.hpp"
#include "bw/math/vector.hpp"
namespace bw::game {
namespace {
constexpr std::string_view kPresentation =
    "Presentation controls - no combat outcomes | Menu: Animation Review";
constexpr float kSelectionMidpoint = .5F;
constexpr std::size_t kLastExpression = 6, kFirstRaisedJump = 8,
                      kLastRaisedJump = 11, kDamageVariants = 12,
                      kDeathVariants = 11, kSpellVariants = 8,
                      kPositiveDeathBegin = 6, kPositiveDeathEnd = 9,
                      kCrouchedPositiveDeath = 10, kCrouchedDeathBegin = 9;
constexpr std::array<math::Vector3<float>, 4> kImpactDirections{
    math::Vector3<float>{0, 0, 1}, {0, 0, -1}, {1, 0, 0}, {-1, 0, 0}};
bool started(const AliciaActionUpdate& value) {
  return value.outcome == AliciaActionOutcome::started;
}
}  // namespace
PlayerAnimationReview::PlayerAnimationReview(
    AnimationPreviewAccess access,
    const formats::CustomizationDefinition& customization)
    : access_(access) {
  if (access_.actor() != nullptr) {
    constexpr std::array death_path{
        std::string_view("witch"), std::string_view("actor"),
        std::string_view("pw0"), std::string_view("body")};
    const resources::detail::Reader body_reader{
        customization.lookup(death_path)};
    constexpr std::uint64_t kDeathWeights = 0x124,
                            kPositiveDeathWeights = 0x13c;
    const auto decode_weights = [&](auto& weights, std::uint64_t offset) {
      for (std::size_t index = 0; index < weights.size(); ++index) {
        weights.at(index) = std::bit_cast<float>(
            body_reader.u32(offset + index * sizeof(float)));
        if (!std::isfinite(weights.at(index)) || weights.at(index) < 0) {
          throw std::invalid_argument(
              "Invalid authored death selection weight");
        }
      }
    };
    decode_weights(review_death_.nonpositive_weights, kDeathWeights);
    decode_weights(review_death_.positive_weights, kPositiveDeathWeights);
    set_review_status(
        "Presentation controls - no combat outcomes | Menu: Animation Review");
  }
}
void PlayerAnimationReview::set_review_status(std::string_view text,
                                              std::string_view detail) {
  review_message_.fill(0);
  const auto count = std::min(text.size(), review_message_.size() - 1);
  std::ranges::copy(text.substr(0, count), review_message_.begin());
  const auto suffix =
      std::min(detail.size(), review_message_.size() - count - 1);
  std::ranges::copy(detail.substr(0, suffix),
                    std::span(review_message_).subspan(count).begin());
}
void PlayerAnimationReview::stop_review() {
  if (!review_entry_) {
    set_review_status("No active preview to stop; traversal continues");
    return;
  }
  reset_review_presentation(*review_entry_);
}
void PlayerAnimationReview::reset_review_presentation(
    const PlayerMovementState& state) {
  // Only preview entry/exit owns this neutralization. An inactive Stop must
  // never reset ordinary player targeting, motion, velocity or weapon cadence.
  if (access_.movement() == nullptr || access_.actor() == nullptr) {
    throw std::logic_error("Preview reset requires player traversal");
  }
  access_.restore(state);
  review_entry_.reset();
  review_fixed_pose_ = false;
  review_scripted_posture_ = false;
  review_next_phase_ = false;
  review_thunder_striking_ = false;
  review_spell_.reset();
  access_.select_target(false);
  access_.publish_pose();
  set_review_status(kPresentation);
}
AnimationReviewResult PlayerAnimationReview::review(
    AnimationReviewRequest request) {
  if (!paused() || access_.actor() == nullptr ||
      access_.movement() == nullptr) {
    return {false, "Preview requires paused player traversal"};
  }
  const auto& actor = *access_.actor();
  if (request.kind > AnimationReviewKind::spell) {
    return {false, "Unknown presentation review request"};
  }
  if (request.kind == AnimationReviewKind::stop) {
    stop_review();
    return {true, review_status()};
  }
  if (request.kind == AnimationReviewKind::next_phase) {
    if (!review_spell_) return {false, "No spell preview awaiting a phase"};
    if (*review_spell_ == SpellPresentationKind::thunder &&
        !review_thunder_striking_) {
      const auto transition = access_.accept_thunder_target();
      if (transition.outcome != AliciaActionOutcome::unchanged &&
          transition.outcome != AliciaActionOutcome::started) {
        return {false, "Thunder target transition unavailable"};
      }
      review_thunder_striking_ = true;
    } else {
      review_next_phase_ = true;
    }
    set_review_status(
        "PREVIEW: synthetic external phase requested | Menu: Stop / Next "
        "phase");
    return {true, review_status()};
  }
  const bool clip_request = request.kind >= AnimationReviewKind::body_clip &&
                            request.kind <= AnimationReviewKind::weapon_clip;
  const auto& catalog = request.kind == AnimationReviewKind::weapon_clip
                            ? actor.weapon_animations()
                            : actor.body_animations();
  if (clip_request && request.index >= catalog.motions().size()) {
    return {false, "Motion publication unavailable"};
  }
  // Validate representation before interrupting the current preview. Absolute
  // local poses and additive deltas cannot be exchanged between motion banks.
  if (clip_request) {
    const auto semantics = catalog.pose_semantics(request.index);
    if (semantics == MotionPoseSemantics::unknown) {
      return {false, "Motion consumer is unresolved; preview is unavailable"};
    }
    const auto required = request.kind == AnimationReviewKind::additive_clip
                              ? MotionPoseSemantics::additive
                              : MotionPoseSemantics::ordinary;
    if (semantics != required) {
      return {false,
              "Clip representation is incompatible with this motion bank"};
    }
  }
  if ((request.kind == AnimationReviewKind::facial &&
       (request.index > kLastExpression || request.index == 2)) ||
      (request.kind == AnimationReviewKind::melee && request.index > 2) ||
      (request.kind == AnimationReviewKind::scripted_jump &&
       request.index != 0 && request.index != 1 &&
       (request.index < kFirstRaisedJump || request.index > kLastRaisedJump)) ||
      (request.kind == AnimationReviewKind::damage &&
       request.index >= kDamageVariants) ||
      (request.kind == AnimationReviewKind::death &&
       request.index >= kDeathVariants) ||
      (request.kind == AnimationReviewKind::spell &&
       request.index >= kSpellVariants)) {
    return {false, "Unknown presentation variant"};
  }
  if (!review_entry_ &&
      ((*access_.movement()).phase != PlayerMovementPhase::grounded ||
       !(*access_.movement()).contact.grounded)) {
    return {false, "Land before starting a presentation preview"};
  }
  if (request.kind == AnimationReviewKind::facial &&
      !actor.has_facial_shapes()) {
    return {false, "This costume has no authored facial shape streams"};
  }
  const bool retain_weapon_action =
      review_entry_ && !review_fixed_pose_ && !review_spell_ &&
      !access_.blocks_locomotion() &&
      request.kind >= AnimationReviewKind::raise_weapon &&
      request.kind <= AnimationReviewKind::element_change;
  const bool retain_layers =
      retain_weapon_action ||
      (review_fixed_pose_ &&
       (request.kind == AnimationReviewKind::upper_clip ||
        request.kind == AnimationReviewKind::additive_clip ||
        request.kind == AnimationReviewKind::weapon_clip ||
        request.kind == AnimationReviewKind::facial));
  if (!retain_layers) {
    const auto entry = review_entry_.value_or((*access_.movement()));
    reset_review_presentation(entry);
    review_entry_ = (*access_.movement());
  }
  bool accepted = true;
  if (clip_request) {
    CharacterMotionBank bank = CharacterMotionBank::body;
    if (request.kind == AnimationReviewKind::upper_clip) {
      bank = CharacterMotionBank::upper_body;
    }
    if (request.kind == AnimationReviewKind::additive_clip) {
      bank = CharacterMotionBank::additive;
    }
    if (request.kind == AnimationReviewKind::weapon_clip) {
      bank = CharacterMotionBank::weapon;
    }
    (void)access_.request_animation(
        bank, {.clip = request.index,
               .binding = kOriginalCharacterMotionSlots + request.index,
               .parameter = bank == CharacterMotionBank::additive
                                ? std::nullopt
                                : catalog.parameter_for_clip(request.index),
               .channel = bank == CharacterMotionBank::upper_body ? 1U : 0U,
               .fresh = true});
    review_fixed_pose_ = true;
    set_review_status("PREVIEW fixed-position clip: ",
                      catalog.motions().name(request.index));
  } else if (request.kind == AnimationReviewKind::facial) {
    accepted = access_.facial(request.index);
    review_fixed_pose_ = accepted;
    set_review_status(accepted ? "PREVIEW facial expression | Menu: Stop / Face"
                               : "This costume has no requested facial shape");
  } else if (request.kind == AnimationReviewKind::scripted_jump) {
    // Deliberately supplied review velocity; not a newly inferred game input.
    constexpr float kReviewVerticalVelocity = 4.2F;
    accepted = started(access_.begin_jump(
        {.motion_word = static_cast<std::uint32_t>(request.index),
         .captured_velocity = {0, kReviewVerticalVelocity, 0}}));
    review_scripted_posture_ = request.index >= kFirstRaisedJump;
    set_review_status(
        "PREVIEW scripted flight: supplied velocity | Menu: Stop");
  } else if (request.kind == AnimationReviewKind::damage) {
    accepted = started(access_.begin_damage(
        {.direction = {.incoming = kImpactDirections.at(request.index % 4)},
         .impact_class = static_cast<DamageImpactClass>(request.index / 4)}));
    set_review_status("PREVIEW accepted impact; no health change | Menu: Stop");
  } else if (request.kind == AnimationReviewKind::death) {
    auto death = review_death_;
    const bool positive = (request.index >= kPositiveDeathBegin &&
                           request.index < kPositiveDeathEnd) ||
                          request.index == kCrouchedPositiveDeath;
    death.direction.incoming = positive ? math::Vector3<float>{0, 0, 1}
                                        : math::Vector3<float>{0, 0, -1};
    death.crouched_state = request.index >= kCrouchedDeathBegin;
    const auto weights =
        positive ? std::span<const float>(death.positive_weights)
                 : std::span<const float>(death.nonpositive_weights);
    const auto choice = request.index < kPositiveDeathBegin
                            ? request.index
                            : request.index - kPositiveDeathBegin;
    if (!death.crouched_state) {
      float total = 0, before = 0;
      for (std::size_t i = 0; i < weights.size(); ++i) {
        total += weights[i];
        if (i < choice) before += weights[i];
      }
      if (!(total > 0) || weights[choice] <= 0) {
        return {false, "Requested death has no authored selection weight"};
      }
      death.random_unit =
          (before + weights[choice] * kSelectionMidpoint) / total;
    }
    accepted = started(access_.begin_death(death));
    set_review_status(
        "PREVIEW death animation; no ragdoll/health change | Menu: Stop");
  } else if (request.kind == AnimationReviewKind::spell) {
    const auto kind = static_cast<SpellPresentationKind>(request.index);
    accepted = started(access_.begin_spell(kind));
    if (accepted) review_spell_ = kind;
    set_review_status("PREVIEW spell animation only | Menu: Next phase / Stop");
  } else if (request.kind == AnimationReviewKind::melee) {
    accepted = started(
        access_.begin_melee(static_cast<CloseAttackVariant>(request.index)));
    set_review_status("PREVIEW melee; no hit outcome | Menu: Stop");
  } else {
    const auto* body = actor.animation_state(CharacterMotionBank::body);
    const auto slot = static_cast<std::uint32_t>(body->binding);
    ActionMotionPlan plan;
    if (request.kind == AnimationReviewKind::raise_weapon) {
      access_.select_target(true);
      set_review_status(
          "PREVIEW raised weapon; move/look to inspect | Menu: Stop");
    } else if (request.kind == AnimationReviewKind::lower_weapon) {
      access_.select_target(false);
      access_.lower_weapon();
      plan =
          change_weapon_raised((*access_.movement()).stance, slot, true, false);
      set_review_status("PREVIEW lower weapon | Menu: Stop");
    } else if (request.kind == AnimationReviewKind::shot) {
      plan = accepted_shot(
          {.body_slot = slot, .weapon_class = access_.weapon_class()});
      set_review_status("PREVIEW shot; no ammo/projectile | Menu: Stop");
    } else if (request.kind == AnimationReviewKind::reload) {
      plan = accepted_reload(
          {.body_slot = slot, .weapon_class = access_.weapon_class()});
      set_review_status("PREVIEW reload; no ammo/resource change | Menu: Stop");
    } else if (request.kind == AnimationReviewKind::element_change) {
      plan = accepted_weapon_change((*access_.movement()).stance, slot);
      set_review_status(
          "PREVIEW element change; no enchantment effect | Menu: Stop");
    }
    const auto admission = access_.apply(plan);
    accepted = plan.count == 0 || admission.admitted > 0;
    if (admission.unavailable > 0) {
      set_review_status(
          "PREVIEW partial: class has absent action slots | Menu: Stop");
    }
  }
  if (!accepted) {
    stop_review();
    return {false, "Requested presentation unavailable in installed resources"};
  }
  return {true, review_status()};
}
void PlayerAnimationReview::prepare_review_observations(
    PlayerActionObservations& value) {
  if (review_scripted_posture_) value.aim.accepted_refresh = true;
  if (!review_spell_) return;
  // These are explicit preview observations, not successful world services.
  value.spell.target_valid = true;
  value.spell.camera_owned = *review_spell_ == SpellPresentationKind::tornado
                                 ? review_next_phase_
                                 : false;
  value.spell.cinematic_segment_finished = review_next_phase_;
  value.spell.meteor_world_finished = review_next_phase_;
}
void PlayerAnimationReview::observe_outcome() {
  if (review_entry_) {
    const auto outcome = (*access_.outcome()).outcome;
    if (outcome == AliciaActionOutcome::dynamic_body_required) {
      set_review_status(
          "PREVIEW held at dynamic-body handoff (not implemented) | Menu: "
          "Stop");
    } else if (review_spell_ &&
               access_.action_kind() != AliciaActionKind::magic) {
      review_spell_.reset();
      set_review_status(
          "PREVIEW completed; no world spell outcome | Menu: Stop");
    }
  }
}
void PlayerAnimationReview::clear_after_equipment() {
  review_entry_.reset();
  review_fixed_pose_ = review_next_phase_ = false;
  review_scripted_posture_ = false;
  review_thunder_striking_ = false;
  access_.select_target(false);
  review_spell_.reset();
  set_review_status(kPresentation);
}
double PlayerAnimationReview::advance(PlayerMovementInput movement,
                                      float camera_yaw, double delta_seconds,
                                      PlayerActionObservations observations) {
  if (!active() || access_.actor() == nullptr ||
      access_.movement() == nullptr) {
    throw std::logic_error(
        "Preview stepping requires an active physical owner");
  }
  prepare_review_observations(observations);
  double consumed = 0;
  if (review_fixed_pose_) {
    // Catalog inspection deliberately fixes physical placement; it still
    // uses the same four banks, pose modifiers, events, audio and renderer.
    access_.advance_fixed(delta_seconds);
    consumed = delta_seconds;
  } else if (review_next_phase_) {
    // An external phase acknowledgement is an edge, not a true value repeated
    // across every physical substep in a long host frame.
    const auto first = player_simulation_step(delta_seconds);
    consumed = access_.advance(movement, camera_yaw, first, observations);
    review_next_phase_ = false;
    prepare_review_observations(observations);
    if (consumed == first && delta_seconds > first) {
      consumed += access_.advance(movement, camera_yaw, delta_seconds - first,
                                  observations);
    }
  } else {
    consumed =
        access_.advance(movement, camera_yaw, delta_seconds, observations);
  }
  observe_outcome();
  return consumed;
}
}  // namespace bw::game
