// Retained original model-group visibility, separate from renderer membership.
// Construction copies the authored eight-word mask. Each motion bank owns the
// fallback groups it activated; release/reset clears those bits in the shared
// model mask, including bits initially enabled. Registered actor listeners are
// resolved by the caller first. Source accessors explicitly ignore words >=8,
// so out-of-range fallback targets have no observable group mutation. No draw
// index is inferred from a target. Fixed inline masks, O(1) command/O(8) reset,
// no frame allocation or borrowed lifetime. Core I.1 (#ri-explicit) and MEM.9
// (embedded-allocate-at-init), read for design and implementation, make bank
// ownership and bounded storage explicit. Source:
// first-combat-physics-boundary.
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
namespace bw::game {
class ModelVisibility {
 public:
  static constexpr std::size_t kWords = 8, kBanks = 3;
  explicit ModelVisibility(std::span<const std::uint32_t, kWords> initial);
  void command(std::size_t bank, std::int32_t target, bool enabled);
  void reset(std::size_t bank);
  bool visible(std::uint32_t group) const noexcept;
  // Registered listeners write model bits independently of fallback ownership.
  void set_group(std::uint32_t group, bool enabled) noexcept;

 private:
  static constexpr std::uint32_t kWordBits = 32;
  std::array<std::uint32_t, kWords> groups_{};
  std::array<std::array<std::uint32_t, kWords>, kBanks> active_{};
};
}  // namespace bw::game
