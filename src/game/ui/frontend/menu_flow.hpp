// Temporal list contract: accept input before advancing supplied-time
// responses; nested difficulty retains its parent carousel state. Both-phase
// E.4
// (#re-design-invariants) and MEM.9 keep one owner and fixed observations.
// Prompt/confirmation repair: I.1 (#ri-explicit) and MEM.9
// (guidelines/memory/MEM.9-embedded-allocate-at-init.md), read in both
// phases, keep original UI animation in this supplied-time owner. Footer and
// confirmation opacity are copied observations; rendering never advances them.
// Localized-text repair: design I.1 and implementation E.4, with MEM.9
// retrieved in both phases. Policy publishes content IDs; the surface owns
// prepared purchased text/art. No English fallback or frame-time decoding.
// Main-shell feedback is a copied observation; MenuFlow alone owns
// supplied-time transitions and fixed selector response state. Core I.1 and
// performance MEM.9 were read in design: explicit time/event outputs, no frame
// allocation. G2: recovered front-end selection policy and presentation values.
// Change axis: original game menu choices; rendering, storage and Lua routing
// remain with their owners. The flow exclusively borrows Profile until its
// destruction; all methods run on the session thread. Presentation owns a
// bounded row array whose authored content is identified by compatibility IDs.
// Selection completion publishes a value, never a mission instance or borrowed
// save. Algorithm: establish page rows from profile availability, apply one
// pressed action/repeated direction, then publish the selected action after
// confirmation. Concept missions are selectable without game completion by
// project policy; campaign-stage and higher-difficulty gates remain injected.
// Main/difficulty navigation wraps and skips disabled entries (4c6570);
// costume/stage/concept lists clamp at their endpoints (4c6610). Costs: bounded
// O(64) row/navigation work, no per-update allocations. Design MCP:
// cpp-guidelines Enum.3 (#renum-class) keeps pages/actions distinct;
// cpp-performance COPY.7
// (guidelines/copy-move/COPY.7-eliminate-hidden-copies.md) uses small explicit
// value copies and borrows the larger profile. Concept-menu policy design and
// implementation consult I.1 (#ri-explicit): expose the intentional departure
// here instead of coupling it to the campaign-stage flag; COPY.7 retains small
// value inputs and bounded borrowed presentation without added copies.
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string_view>

#include "bw/math/scalar.hpp"
#include "game/ui/audio/menu_audio.hpp"
#include "game/ui/common/menu_dialog.hpp"
#include "game/ui/common/menu_input.hpp"
#include "game/ui/common/menu_prompt.hpp"

namespace bw::game {
class Profile;
inline constexpr std::size_t menu_difficulty_count = 5;
inline constexpr std::size_t menu_row_capacity = 64;
inline constexpr std::size_t main_menu_row_count = 6;
struct MenuUnlocks {
  // Campaign progression gates are separate from installed-content
  // availability. Concept-menu access does not consume a progression gate.
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
  unsigned best_rank{};
  std::uint32_t best_score{};
};
enum class MainMenuMotion : std::uint8_t { idle, entering, leaving, returning };
struct MainMenuFeedback {
  MainMenuMotion motion{MainMenuMotion::idle};
  float elapsed{};
  float description_alpha{1};
  // Native confirmation echo advances linearly from zero to one.
  float confirmation_elapsed{1};
  std::array<float, main_menu_row_count> row_y{}, row_scale{1, 1, 1, 1, 1, 1};
};
// Original difficulty row anchors are shared by policy cursor targets and art.
inline constexpr std::array<std::array<float, 2>, menu_difficulty_count>
    difficulty_row_positions{
        {{656, 226}, {592, 294}, {538, 362}, {472, 424}, {624, 486}}};
enum class ChildMenuMotion : std::uint8_t { entering, active, leaving };
// Native child-task state is observed by the surface; it owns neither clocks
// nor Profile access. scroll is the original damped absolute row displacement.
struct ChildMenuFeedback {
  ChildMenuMotion motion{ChildMenuMotion::entering};
  float elapsed{}, scroll{}, pulse{}, description_alpha{}, input_delay{};
  std::array<float, menu_row_capacity> highlight{};
  unsigned current_costume{};
  std::array<float, 2> selector_position{};
  float selector_opacity{}, selector_scale{}, selector_echo{};
};
enum class FrontendDialog : std::uint8_t {
  none,
  no_checkpoint,
  continue_game,
  change_costume,
  costume_changed,
  save_costume,
  costume_saved,
  save_failed,
  costume_saved_manual,
  profile_loading,
  profile_creating,
  profile_load_failed,
  profile_overwrite,
  profile_created,
  profile_enable_autosave
};
struct StageMenuFeedback {
  bool visible{}, is_concept{};
  std::array<unsigned, menu_row_capacity> identities{};
  std::size_t count{}, selected{};
  unsigned best_rank{};
  std::uint32_t best_score{};
  // Parent-owned veil response while difficulty owns input, not a child tint.
  float obscuration{};
  ChildMenuFeedback motion;
};
struct MenuPresentation {
  MenuPage page{MenuPage::main};
  std::string_view title;
  std::array<MenuRow, menu_row_capacity> rows{};
  std::size_t count{};
  std::size_t selected{};
  std::string_view message;
  MainMenuFeedback main;
  ChildMenuFeedback child;
  StageMenuFeedback stage;
  FrontendDialog dialog{FrontendDialog::none};
  MenuDialogPresentation dialog_view;
  bool dialog_interactive{true};
  // CharacterLoad is a separate black native task, not Continue artwork.
  bool storage_background{};
  MenuPrompts prompts{menu_prompts(MenuInputDevice::keyboard_mouse)};
  float opacity{1};
  float footer_opacity{};
};
struct MenuSelection {
  MenuAction action{MenuAction::new_game};
  unsigned stage{1};
  unsigned difficulty{};
  unsigned costume{};
  unsigned original_status{};
};
class MenuFlow {
 public:
  explicit MenuFlow(Profile& profile, MenuUnlocks unlocks = {});
  void start();
  // Presentation time is supplied by the session; input waits through authored
  // entry and child-transition tracks. No system or world clock is read.
  void update(MenuInput input, float presentation_seconds = 0);
  MenuSoundRequests take_sounds() {
    const auto result = sounds_;
    sounds_ = {};
    return result;
  }
  // Presentation is borrowed until the next mutation; it carries no render
  // assets.
  const MenuPresentation& presentation() const { return presentation_; }
  const std::optional<MenuSelection>& selection() const { return selection_; }
  // The session applies this original child conceal before task completion.
  float departure_fade_seconds() const { return departure_fade_seconds_; }
  std::optional<float> take_music_fade() {
    const auto result = music_fade_;
    music_fade_.reset();
    return result;
  }
  // Options is owned by the shared native Options flow. This child rendezvous
  // does not restart the main selector or acquire storage authority.
  void begin_costume_child();
  void complete_options() { populate(MenuPage::main); }
  bool costume_child_complete() const { return costume_complete_; }
  void acknowledge_costume_child();
  bool take_settings_save_request();
  void settings_save_completed(bool success);
  void set_prompts(MenuPrompts prompts) { presentation_.prompts = prompts; }
  bool settings_changed() const { return settings_changed_; }
  void acknowledge_settings() { settings_changed_ = false; }

 private:
  void advance(MenuInput input, float seconds);
  void update_footer(float seconds);
  void populate(MenuPage page);
  void enter_child(MenuPage page);
  void update_child(MenuInput input, float seconds);
  void advance_child(MenuInput input, float seconds);
  void start_dialog(FrontendDialog dialog, bool yes = true);
  void update_dialog(MenuInput input, float seconds);
  void begin_child_exit(bool accepted);
  void update_child_animation(float seconds);
  void update_difficulty_selector(float seconds);
  void animate(float seconds);
  void begin_main_motion(MainMenuMotion motion);
  void navigate(int direction);
  void accept();
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
  MainMenuFeedback feedback_;
  MenuSoundRequests sounds_;
  std::array<math::DampedResponseState<float>, main_menu_row_count> scale_{};
  math::DampedResponseState<float> description_;
  float pulse_seconds_{};
  float footer_opacity_{};
  std::size_t main_selection_{};
  bool pending_accept_{};
  bool options_return_{};
  float child_fade_{};
  bool child_reveal_{};
  float departure_fade_seconds_{};
  std::optional<float> music_fade_;
  ChildMenuFeedback child_;
  std::array<math::DampedResponseState<float>, 2> selector_position_{};
  std::array<float, 2> selector_target_{};
  math::DampedResponseState<float> selector_opacity_, selector_scale_;
  float selector_confirmation_seconds_{1};
  math::DampedResponseState<float> scroll_, child_description_;
  std::array<math::DampedResponseState<float>, menu_row_capacity>
      child_highlight_{};
  MenuDialog dialog_;
  bool costume_complete_{}, costume_changed_{}, costume_confirmation_pending_{};
  bool save_requested_{}, save_pending_{}, child_exit_accept_{};
  std::size_t stage_selection_{};
  float child_transition_{}, stage_obscuration_{};
  bool difficulty_pending_{};
  StageMenuFeedback stage_backdrop_;
};
}  // namespace bw::game
