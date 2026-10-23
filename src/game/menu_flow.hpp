// G2: recovered front-end selection policy and presentation values.
// Change axis: original game menu choices; rendering, storage and Lua routing
// remain with their owners. The flow exclusively borrows Profile until its
// destruction; all methods run on the session thread. Presentation owns a
// bounded row array whose string views reference static literals. Selection
// completion publishes a value, never a mission instance or borrowed save.
// Algorithm: establish page rows from profile availability, apply one pressed
// action/repeated direction, then publish the selected action after
// confirmation. Navigation wraps and skips disabled entries as original
// 0x4c6570 does. Costs: bounded O(64) row/navigation work, no per-update
// allocations. Design MCP: cpp-guidelines Enum.3 (#renum-class) keeps
// pages/actions distinct; cpp-performance COPY.7
// (guidelines/copy-move/COPY.7-eliminate-hidden-copies.md) uses small explicit
// value copies and borrows the larger profile.
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string_view>

namespace bw::game {
class Profile;
inline constexpr std::size_t menu_difficulty_count = 5;
inline constexpr std::size_t menu_row_capacity = 64;
struct MenuUnlocks {
  std::array<bool, menu_difficulty_count> difficulties{true, true, true};
  bool stage_select{};
};
enum class MenuPage : std::uint8_t {
  main,
  difficulty,
  stage,
  costume,
  options,
  extra,
  confirm,
  complete
};
enum class MenuAction : std::uint8_t {
  new_game,
  continue_game,
  stage_select,
  concept_mission,
  return_to_title,
  quit
};
struct MenuRow {
  std::string_view label;
  std::string_view value;
  bool enabled{true};
  unsigned compatibility_id{};
};
struct MenuPresentation {
  MenuPage page{MenuPage::main};
  std::string_view title;
  std::array<MenuRow, menu_row_capacity> rows{};
  std::size_t count{};
  std::size_t selected{};
  std::string_view message;
};
struct MenuSelection {
  MenuAction action{MenuAction::new_game};
  unsigned stage{1};
  unsigned difficulty{};
  unsigned costume{};
  unsigned original_status{};
};
struct MenuInput {
  std::uint32_t pressed{};
  std::uint32_t repeated{};
};
class MenuFlow {
 public:
  explicit MenuFlow(Profile& profile, MenuUnlocks unlocks = {});
  void start();
  void update(MenuInput input);
  const MenuPresentation& presentation() const { return presentation_; }
  const std::optional<MenuSelection>& selection() const { return selection_; }
  bool settings_changed() const { return settings_changed_; }
  void acknowledge_settings() { settings_changed_ = false; }

 private:
  void populate(MenuPage page);
  void navigate(int direction);
  void accept();
  void adjust(int direction);
  void add(std::string_view label, std::string_view value = {},
           bool enabled = true);
  void cancel();
  void finish(MenuAction action, unsigned original_status);
  std::reference_wrapper<Profile> profile_;
  MenuUnlocks unlocks_;
  MenuPresentation presentation_;
  std::array<unsigned, menu_row_capacity> row_values_{};
  std::optional<MenuSelection> selection_;
  MenuAction pending_{MenuAction::new_game};
  unsigned stage_{1};
  unsigned difficulty_{};
  unsigned costume_{};
  bool settings_changed_{};
  MenuPage previous_{MenuPage::main};
};
}  // namespace bw::game
