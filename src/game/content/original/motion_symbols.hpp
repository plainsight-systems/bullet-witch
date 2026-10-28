// Original action-language symbol compatibility, independent of actor catalogs.
// Return the original motion slot for an exact, case-sensitive symbolic name;
// unknown names remain absent, distinct from a known slot with no actor clip.
// Immutable name/number facts are prepared once, with no asset bytes or source
// instructions embedded. O(log N) lookup, no allocation or mutable state.
// Core F.7 (#rf-smart) borrows the name only for this call; performance MEM.9
// (guidelines/memory/MEM.9-embedded-allocate-at-init.md) keeps lookup storage
// fixed. Both design corpora read. See first-combat-exchange source coverage.
#pragma once
#include <cstdint>
#include <optional>
#include <string_view>
namespace bw::game::content {
std::optional<std::uint32_t> original_motion_symbol(std::string_view name);
}  // namespace bw::game::content
