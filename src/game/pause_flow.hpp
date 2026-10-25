// Weapon rows observe mission-owned acquired levels; a disabled row cannot
// submit a preview request. Rebinding updates bounded presentation only and
// does not mutate permanent progression or resident equipment. Core C.9 design,
// E.4 (#re-design-invariants) implementation and both-phase MEM.9
// (guidelines/memory/MEM.9-embedded-allocate-at-init.md) preserve this
// boundary. Developer review visibility is fixed at construction. Hiding its
// root entries leaves normal Resume/Costume input and transactions unchanged.
// No capture dependency reaches the simulation. Both MCP phases: Core I.1
// (#ri-explicit) keeps the input explicit; MEM.9 (guidelines/memory/
// MEM.9-embedded-allocate-at-init.md) retains fixed menu storage.
// Review pages retain prepared pose semantics beside borrowed clip labels.
// Unknown or incompatible entries remain visible but disabled; actual
// publication indices never change. Core I.1/E.4 makes admission explicit;
// CDSA.32 reuses the immutable setup classification in bounded page work.
// Mission pause policy owns bounded menu/navigation state, not actor resources.
// Review pages borrow immutable catalogs. After replacing an owner, callers
// must rebind before any navigation or presentation reads old labels/pointers.
// No menu operation may overlap the resource transaction. Failure messages are
// copied into bounded inline storage. Copy/move is forbidden because menu views
// may refer to that storage. Review admission is separate from navigation;
// accepted previews resume, rejected requests stay paused. Page construction is
// bounded to 12 catalog entries plus navigation, with no heap allocation.
// Consume menu input before gameplay; update returns whether this entire frame
// belongs to the UI (including resume). Costume requests are explicit values;
// callers report success/failure after transactional publication. The selected
// costume is session state, never an implicit profile/save write. All calls run
// on the owner thread; presentation views borrow catalogs/static labels or
// this owner's error buffer. Invalid
// costume IDs throw before mutation. Algorithm: toggle/back -> confirm -> wrap
// navigation; retain pause until explicit resume. No allocations or clock work.
// Both-phase Core I.1 (#ri-explicit), R.1 (#rr-raii), performance MEM.9
// (guidelines/memory/MEM.9-embedded-allocate-at-init.md) keep ownership
// explicit and frame storage fixed. See pause-and-costume-switching.md for
// original exclusion, the approved improvement and pinned Unreal pause-input
// reference.
#pragma once
#include "animation_review.hpp"
#include "bw/animation/clip_catalog.hpp"
#include "character_animations.hpp"
#include "menu_flow.hpp"
namespace bw::game {
class PauseFlow {
 public:
  explicit PauseFlow(unsigned costume, bool developer_review = true);
  PauseFlow(const PauseFlow&) = delete;
  PauseFlow& operator=(const PauseFlow&) = delete;
  PauseFlow(PauseFlow&&) = delete;
  PauseFlow& operator=(PauseFlow&&) = delete;
  ~PauseFlow() = default;
  // Missing classification keeps that catalog unavailable; no representation
  // is inferred from its labels or from its actor/weapon role.
  void bind_review_catalogs(
      const animation::PreparedClipCatalog& body,
      const animation::PreparedClipCatalog& weapon, unsigned current_weapon,
      std::span<const MotionPoseSemantics> body_semantics = {},
      std::span<const MotionPoseSemantics> weapon_semantics = {});
  // Mission authority supplies acquired levels; preview selection cannot
  // bypass the same equipment availability used by gameplay cycling.
  void set_available_weapons(const std::array<std::uint8_t, 4>& levels);
  bool update(bool pause_pressed, MenuInput input);
  bool paused() const { return paused_; }
  std::optional<unsigned> requested_costume() const { return request_; }
  void complete_costume(bool success);
  std::optional<unsigned> requested_weapon() const { return weapon_request_; }
  void complete_weapon(bool success);
  std::optional<AnimationReviewRequest> requested_review() const {
    return review_request_;
  }
  void complete_review(AnimationReviewResult result);
  const MenuPresentation& presentation() const { return presentation_; }

 private:
  enum class Page : std::uint8_t { root, costumes, weapons, review, variants };
  void populate(Page page);
  void select();
  void message(std::string_view text);
  void append(std::string_view label, unsigned id, bool enabled = true);
  bool developer_review_;
  bool paused_{};
  Page page_{Page::root};
  AnimationReviewKind family_{AnimationReviewKind::stop};
  std::size_t catalog_page_{};
  const animation::PreparedClipCatalog* body_{};
  const animation::PreparedClipCatalog* weapon_{};
  unsigned weapon_class_{};
  std::array<bool, 4> available_weapons_{true, true, true, true};
  std::span<const MotionPoseSemantics> body_semantics_, weapon_semantics_;
  std::optional<unsigned> weapon_request_;
  std::optional<AnimationReviewRequest> review_request_;
  static constexpr std::size_t kMessageCapacity = 160;
  std::array<char, kMessageCapacity> message_{};
  unsigned costume_{};
  std::optional<unsigned> request_;
  MenuPresentation presentation_;
};
}  // namespace bw::game
