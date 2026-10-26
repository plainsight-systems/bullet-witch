// Debug separation: Core I.1 (#ri-explicit), R.3 (#rr-ptr), and performance
// TLM.1 (guidelines/telemetry/TLM.1-compile-telemetry-out-by-default.md),
// retrieved in design and implementation, keep preview capabilities bounded
// and compile out their owner/state/calls in ordinary production builds.
// Ownership split of the existing stage runtime; no new simulation algorithm.
// Design Core I.1 (#ri-explicit), C.9 (#rc-private) and R.21 (#rr-unique)
// keep authority private and borrows explicit. Implementation E.6 (#re-raii)
// preserves destruction/unwind order. Both-phase MEM.9
// (guidelines/memory/MEM.9-embedded-allocate-at-init.md) reserves domain
// buffers during preparation; frame operations retain their bounded fixed
// footprint. COPY.4 (guidelines/copy-move/COPY.4-rule-of-five-rule-of-zero.md)
// keeps stable owner identities noncopyable/nonmovable. See
// docs/architecture/reorganization-plan.md. Own only explicitly requested
// animation preview state and status text. Borrow stable
// actor/controller/targeting plus an update-only publisher; the mission and
// presentation outlive this owner. Preview entry validates motion semantics,
// saves physical state, and restores only on active Stop. Synthetic spell
// acknowledgements remain separate from real world action observations. No
// equipment residency, real ammunition admission, world clock or lighting.
// Construct death selection weights from authored customization once; all
// status/phase operations use bounded value storage on the game thread.
#pragma once
#include "game/world/animation_preview.hpp"
namespace bw::game {
class PlayerAnimationReview final : public AnimationPreview {
 public:
  ~PlayerAnimationReview() override = default;
  PlayerAnimationReview(AnimationPreviewAccess,
                        const formats::CustomizationDefinition&);
  AnimationReviewResult review(AnimationReviewRequest) override;
  std::string_view review_status() const override {
    return review_message_.data();
  }
  void set_review_status(std::string_view text, std::string_view detail = {});
  bool active() const override { return review_entry_.has_value(); }
  bool blocks_shooter() const override {
    return review_fixed_pose_ || review_spell_;
  }
  // Explicit preview stepping preserves the same physical substep ordering;
  // fixed catalog poses and synthetic one-shot phase acknowledgements live
  // here.
  double advance(PlayerMovementInput, float camera_yaw, double seconds,
                 PlayerActionObservations) override;
  // Called only after accepted paused equipment adoption; no physical reset.
  void clear_after_equipment() override;
  PlayerAnimationReview(const PlayerAnimationReview&) = delete;
  PlayerAnimationReview& operator=(const PlayerAnimationReview&) = delete;
  PlayerAnimationReview(PlayerAnimationReview&&) = delete;
  PlayerAnimationReview& operator=(PlayerAnimationReview&&) = delete;

 private:
  AnimationPreviewAccess access_;
  bool paused() const { return access_.paused(); }
  void prepare_review_observations(PlayerActionObservations&);
  void observe_outcome();
  void stop_review();
  void reset_review_presentation(const PlayerMovementState&);
  std::optional<PlayerMovementState> review_entry_;
  bool review_fixed_pose_{}, review_next_phase_{}, review_thunder_striking_{};
  bool review_scripted_posture_{};
  std::optional<SpellPresentationKind> review_spell_;
  static constexpr std::size_t kReviewMessageCapacity = 160;
  std::array<char, kReviewMessageCapacity> review_message_{};
  AcceptedDeathPresentation review_death_;
};
}  // namespace bw::game
