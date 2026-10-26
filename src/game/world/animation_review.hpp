// Explicit in-scene presentation review requests, separate from player input
// and gameplay outcomes. Menu owns selection only; WorldRuntime validates
// the requested family/index against retained resources before action mutation.
// No health, ammunition, targets, spell effects or save writes are implied.
// Review requests are small copied values, single game-thread use, no owned
// buffers. Catalog labels remain borrowed from the current actor; menus must
// rebind after a weapon transaction. Source-based action owners still execute
// their transitions; next_phase supplies a visibly synthetic review
// observation. Design Core I.1 (#ri-explicit), E.4 (#re-design-invariants)
// distinguish preview from admission and preserve failure invariants.
// Performance MEM.9 (guidelines/memory/MEM.9-embedded-allocate-at-init.md)
// bounds frame state. See interactive-animation-review.md for intentional
// review-only behavior.
#pragma once
#include <cstddef>
#include <cstdint>
#include <string_view>
namespace bw::game {
enum class AnimationReviewKind : std::uint8_t {
  stop,
  next_phase,
  raise_weapon,
  lower_weapon,
  shot,
  reload,
  melee,
  element_change,
  body_clip,
  upper_clip,
  additive_clip,
  weapon_clip,
  facial,
  scripted_jump,
  damage,
  death,
  spell
};
struct AnimationReviewRequest {
  AnimationReviewKind kind{AnimationReviewKind::stop};
  // Clip index for catalog pages; face0..6 (2 unavailable); jump0/1/8..11;
  // damage class*4+direction; death0..10; spell0..7; melee0..2.
  std::size_t index{};
};
struct AnimationReviewResult {
  bool accepted{};
  std::string_view message;
};
}  // namespace bw::game
