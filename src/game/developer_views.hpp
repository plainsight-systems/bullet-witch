// Build-owned developer-view availability. Normal builds have no input route
// to inspection views; capture/logging options cannot change game behavior.
// This immutable compile-time value owns no resources and introduces no runtime
// authority. Core I.1 (#ri-explicit), retrieved in design and implementation,
// makes the feature selection explicit; performance MEM.9 keeps this decision
// constant and allocation-free. See native-pause-menu.md.
#pragma once

namespace bw::game {
inline constexpr bool developer_views_enabled = BW_DEVELOPER_VIEWS != 0;
}  // namespace bw::game
