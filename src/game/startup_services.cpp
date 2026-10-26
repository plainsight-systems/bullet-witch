// Mission requests own the current Profile progression snapshot, including
// concept routes; no concept grant modifies permanent data. Single setup copy.
// Design C.9/I.30, implementation E.4 (#re-design-invariants); both-phase MEM.9
// (guidelines/memory/MEM.9-embedded-allocate-at-init.md) preserves bounded
// work. Memory migration: share the root through its injected allocator and
// route scheduler nodes/snapshot through the same runtime domain. Exceptions
// unwind acquired owners; native paths never retry on the legacy heap. R.1
// (#rr-raii) and MEM.6
// (guidelines/memory/MEM.6-inject-allocators-behind-one-interface.md) were read
// in design and implementation. Setup ownership adds no tick copies. Front-end
// extension follows the header lifetime and owned handoff contract. Keep Lua
// dispatch authoritative, validate native slot values, and retain a pending
// mission wait after capturing the request. Design MCP I.30
// (#ri-encapsulate), implementation E.6 (#re-raii), cpp-performance COPY.4
// (guidelines/copy-move/COPY.4-rule-of-five-rule-of-zero.md) preserve explicit
// ownership; only the handoff copies checkpoint bytes. No frame-loop copies.
// Startup route extension: game policies borrow capabilities; composition owns
// them through cleanup. Design MCP R.20 (#rr-owner) retains explicit lifetime;
// implementation E.6 (#re-raii) unwinds on failures. cpp-performance COPY.4
// (guidelines/copy-move/COPY.4-rule-of-five-rule-of-zero.md) preserves
// container ownership without per-frame payload copies. Shared presentation
// contracts are described in
// research/engine/subsystems/startup-title-integration.md. G2: bind
// original-root Exec/Wait to owned startup flow tasks. See header and
// docs/startup-runtime.md for lifetimes and evidence. Each task is allocated
// once per execution; frame updates only pass snapshots and request states.
// Completion requests idle; Wait requests destruction only after observing that
// state, retaining the published result. Root failures become terminal results;
// direct service misuse throws. No host screen timeout fabricates success.
// Design/implementation MCP: cpp-guidelines I.1 (#ri-explicit) exposes missing
// screen capabilities; cpp-performance MEM.9
// (guidelines/memory/MEM.9-embedded-allocate-at-init.md) keeps bounded screen
// bookkeeping inline. Existing scheduler snapshots/media may allocate; this
// file does not claim globally allocation-free frames.
#include "startup_services.hpp"

#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>

namespace bw::game {
namespace {
constexpr int kScreenTaskPriority = 0x2000;
constexpr int kRootTaskPriority = 0x2001;
constexpr int kReplayStageSlot = 6;
constexpr unsigned kCampaignStages = 6;
[[noreturn]] void unsupported_screen(Screen screen) {
  switch (screen) {
    case Screen::character_load:
      throw std::runtime_error("Character load screen is not implemented");
    case Screen::title_menu:
      throw std::runtime_error("Title menu screen is not implemented");
    default:
      throw std::invalid_argument("Unsupported startup screen");
  }
}
}  // namespace
StartupSession::StartupSession(LogoMedia& media, TitleSurface* title_surface,
                               FrontEnd* frontend,
                               std::shared_ptr<memory::Domain> runtime_memory)
    : logos_(media,
             {"data/movie/marvelousLogo.ssu", "data/movie/xeedLogo.ssu"}),
      advertise_(media),
      title_(media),
      title_surface_(title_surface),
      frontend_(frontend),
      runtime_memory_(std::move(runtime_memory)),
      scheduler_(runtime_memory_
                     ? tasks::TaskLifecycleScheduler(runtime_memory_)
                     : tasks::TaskLifecycleScheduler()) {}
StartupSession::~StartupSession() {
  if (title_surface_ != nullptr) title_surface_->end();
}
void StartupSession::start_root(resources::ResourceService& resources,
                                resources::ResourceTag tag) {
  if (root_) {
    throw std::logic_error("Startup root already started");
  }
  auto root = runtime_memory_
                  ? std::allocate_shared<RootDriver>(
                        memory::Allocator<RootDriver>(runtime_memory_),
                        resources, *this, tag, runtime_memory_)
                  : std::make_shared<RootDriver>(resources, *this, tag);
  (void)scheduler_.add(kRootTaskPriority, tasks::Hooks{.update = [this] {
                         root_result_ = root_->advance();
                       }});
  root_ = std::move(root);
}
script::RunResult StartupSession::advance(LogoUpdate input,
                                          std::uint32_t repeated) {
  if (!std::isfinite(input.delta) || input.delta < 0) {
    throw std::invalid_argument("Startup delta must be finite and nonnegative");
  }
  if (root_result_.state == script::RunState::failed) {
    return root_result_;
  }
  input_ = input;
  repeated_ = repeated;
  // Screen priority precedes root priority, so Lua observes this tick's screen
  // work.
  scheduler_.tick(false);
  return root_result_;
}
StartupSession::ScreenTask& StartupSession::task(Screen screen) {
  switch (screen) {
    case Screen::company_logo:
      return logo_task_;
    case Screen::memory_card:
      return check_task_;
    case Screen::advertise:
      return advertise_task_;
    case Screen::title:
      return title_task_;
    case Screen::character_load:
      if (frontend_ != nullptr) return load_task_;
      unsupported_screen(screen);
    case Screen::title_menu:
      if (frontend_ != nullptr) return menu_task_;
      unsupported_screen(screen);
    case Screen::power_up:
      if (frontend_ != nullptr) return power_up_task_;
      unsupported_screen(screen);
    default:
      unsupported_screen(screen);
  }
}
ScreenProgress StartupSession::progress(Screen screen) const {
  switch (screen) {
    case Screen::company_logo:
      return logos_.progress();
    case Screen::memory_card:
      return check_.progress();
    case Screen::advertise:
      return advertise_.progress();
    case Screen::title:
      return title_.progress();
    case Screen::character_load:
    case Screen::title_menu:
    case Screen::power_up:
      if (frontend_ != nullptr) return frontend_->progress(screen);
      unsupported_screen(screen);
    default:
      unsupported_screen(screen);
  }
}
double StartupSession::global(int slot) const {
  if (frontend_ != nullptr) {
    switch (slot) {
      case 0:
        return campaign_stage_;
      case 2:
        return replay_mode_ ? 1 : 0;
      case 3:
        return concept_mode_ ? 1 : 0;
      case kReplayStageSlot:
        return replay_stage_;
      default:
        break;
    }
  }
  if (slot != 1) {
    throw std::runtime_error("Startup global slot is not implemented: " +
                             std::to_string(slot));
  }
  // Native accessor returns the stored 32 bits as an unsigned Lua number.
  return static_cast<std::uint32_t>(title_return_request_);
}
// StartupServices fixes the recovered Lua (slot, value) argument order. The
// root adapter validates each argument by role before this compatibility call.
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
void StartupSession::set_global(int slot, double value) {
  if (frontend_ != nullptr &&
      (slot == 0 || slot == 2 || slot == 3 || slot == kReplayStageSlot)) {
    if (!std::isfinite(value) || std::floor(value) != value || value < 0 ||
        value > kCampaignStages) {
      throw std::invalid_argument("Invalid front-end global value");
    }
    const auto integer = static_cast<unsigned>(value);
    if (slot == 0 || slot == kReplayStageSlot) {
      if (integer == 0) {
        throw std::invalid_argument("Campaign stage starts at one");
      }
      (slot == 0 ? campaign_stage_ : replay_stage_) = integer;
    } else {
      if (integer > 1) {
        throw std::invalid_argument("Mission mode must be boolean");
      }
      (slot == 2 ? replay_mode_ : concept_mode_) = integer != 0;
    }
    return;
  }
  if (slot != 1) {
    throw std::runtime_error("Startup global slot is not implemented: " +
                             std::to_string(slot));
  }
  if (!std::isfinite(value) ||
      value < std::numeric_limits<std::int32_t>::min() ||
      value > std::numeric_limits<std::int32_t>::max()) {
    throw std::invalid_argument("Startup global value exceeds signed 32 bits");
  }
  title_return_request_ = static_cast<std::int32_t>(value);
}
void StartupSession::execute(Screen screen) {
  if (screen == Screen::title && title_surface_ == nullptr) {
    throw std::runtime_error("Title presentation capability is required");
  }
  auto& entry = task(screen);
  if (entry.id && scheduler_.contains(*entry.id)) {
    throw std::logic_error("Startup screen already registered");
  }
  entry.id = scheduler_.add(
      kScreenTaskPriority,
      tasks::Hooks{.activate = [this, screen] { activate(screen); },
                   .update = [this, screen] { update(screen); }});
  entry.executed = true;
  entry.waiting_for_activation = true;
}
void StartupSession::activate(Screen screen) {
  if (screen == Screen::company_logo) {
    logos_.start();
    // Native 0x49e0a8 clears application flag 0x8; WM_CLOSE at 0x411d4b
    // invokes quit only when that bit is clear. This is not a Lua global.
    window_close_enabled_ = true;
  } else if (screen == Screen::title) {
    title_.start();
    title_surface_->begin();
  } else if (screen == Screen::advertise) {
    advertise_.start();
  } else if (screen == Screen::character_load || screen == Screen::title_menu ||
             screen == Screen::power_up) {
    frontend_->start(screen);
  } else {
    check_.start();
  }
  task(screen).waiting_for_activation = false;
}
void StartupSession::update(Screen screen) {
  if (screen == Screen::company_logo) {
    logos_.update(input_);
  } else if (screen == Screen::title) {
    title_.update(input_, title_surface_->ready());
    title_surface_->show(title_.progress().visible, title_.presentation());
  } else if (screen == Screen::advertise) {
    advertise_.update(input_);
  } else if (screen == Screen::character_load || screen == Screen::title_menu ||
             screen == Screen::power_up) {
    frontend_->update(screen, input_, repeated_);
    if (screen == Screen::character_load &&
        frontend_->progress(screen).status == FlowStatus::complete) {
      campaign_stage_ = frontend_->profile().has_checkpoint()
                            ? frontend_->profile().checkpoint_stage()
                            : 1;
    }
  } else {
    check_.update(input_.delta);
  }
  const auto result = progress(screen);
  if (result.status == FlowStatus::complete ||
      result.status == FlowStatus::failed) {
    const auto& id = task(screen).id;
    if (!id) {
      throw std::logic_error("Startup screen update has no task registration");
    }
    if (screen == Screen::title) title_surface_->end();
    scheduler_.request(*id, tasks::State::idle);
  }
}
bool StartupSession::pending(Screen screen) {
  auto& entry = task(screen);
  if (!entry.executed) {
    throw std::logic_error("Startup screen wait precedes execution");
  }
  if (entry.waiting_for_activation) {
    return true;
  }
  const auto result = progress(screen);
  if (result.status == FlowStatus::failed) {
    throw std::runtime_error(screen == Screen::title ? title_.diagnostic()
                             : screen == Screen::advertise
                                 ? advertise_.diagnostic()
                                 : logos_.diagnostic());
  }
  if (entry.id && scheduler_.contains(*entry.id)) {
    const auto state = scheduler_.state(*entry.id);
    // Wait for deactivation before scheduling destruction and releasing Lua's
    // wait.
    if (state != tasks::State::idle) {
      return true;
    }
    scheduler_.request(*entry.id, tasks::State::created);
  }
  return result.status != FlowStatus::complete;
}
double StartupSession::status(Screen screen) const {
  const auto result = progress(screen).result;
  // Native accessor 0x49a560 swaps internal menu results5/6 for the Lua API.
  if (screen == Screen::title_menu) {
    constexpr double kNativeConceptResult = 5;
    constexpr double kScriptConceptResult = 6;
    if (result == kNativeConceptResult) return kScriptConceptResult;
    if (result == kScriptConceptResult) return kNativeConceptResult;
  }
  return result;
}
double StartupSession::latest_stage() const {
  if (frontend_ != nullptr && frontend_->selection() &&
      frontend_->selection()->action == MenuAction::stage_select) {
    return frontend_->selection()->stage;
  }
  throw std::runtime_error("Saved stage selection is not implemented");
}
void StartupSession::start_mission(std::string_view mission) {
  if (frontend_ == nullptr || (gameplay_request_ && !mission_retired_) ||
      frontend_->progress(Screen::title_menu).status != FlowStatus::complete) {
    throw std::logic_error(
        "Mission request requires one confirmed menu selection");
  }
  const auto selected = frontend_->selection();
  if (!selected) {
    throw std::logic_error("Mission request has no menu selection");
  }
  const auto selection = *selected;
  if (selection.action == MenuAction::concept_mission) {
    if (mission != "concept" || !concept_mode_ ||
        selection.stage >= menu_row_capacity) {
      throw std::runtime_error(
          "Lua concept route disagrees with menu selection");
    }
    // Native0x4c2cf0/0x441a80 resolves the selected cm%02d_00 package alias.
    const auto number = std::to_string(selection.stage);
    const auto script =
        std::string("cm") + (selection.stage < 10 ? "0" : "") + number + "_00";
    gameplay_request_ = GameplayRequest{std::string(mission),
                                        selection,
                                        {},
                                        script,
                                        frontend_->profile().progression()};
    mission_retired_ = retired_music_observed_ = concept_unloaded_ = false;
    return;
  }
  if (selection.action != MenuAction::new_game &&
      selection.action != MenuAction::continue_game &&
      selection.action != MenuAction::stage_select) {
    throw std::runtime_error("Extra mission launch routing is unresolved");
  }
  constexpr std::array<std::string_view, kCampaignStages> names{
      "st01_00", "st02_00", "st03_00", "st04_00", "st05_00", "st06_00"};
  if (selection.stage == 0 || selection.stage > names.size() ||
      mission != names.at(selection.stage - 1) || concept_mode_ ||
      (selection.action == MenuAction::stage_select &&
       (!replay_mode_ || replay_stage_ != selection.stage))) {
    throw std::runtime_error("Lua mission route disagrees with menu selection");
  }
  GameplayRequest request{std::string(mission),
                          selection,
                          {},
                          std::string(mission),
                          frontend_->profile().progression()};
  // Own checkpoint bytes at handoff so the request can outlive front-end state.
  if (selection.action == MenuAction::continue_game) {
    const auto checkpoint = frontend_->profile().checkpoint();
    if (checkpoint.empty()) {
      throw std::logic_error("Continue requires checkpoint data");
    }
    request.checkpoint.assign(checkpoint.begin(), checkpoint.end());
  }
  gameplay_request_ = std::move(request);
  mission_retired_ = retired_music_observed_ = concept_unloaded_ = false;
}
bool StartupSession::mission_pending() {
  if (!gameplay_request_) {
    throw std::logic_error("Mission Wait precedes StartMission");
  }
  return !mission_retired_;
}
void StartupSession::complete_mission_retirement() {
  complete_mission_transition({MissionTransition::retire});
}
void StartupSession::validate_mission_transition(
    MissionTransitionRequest transition) const {
  if (!gameplay_request_ || mission_retired_) {
    throw std::logic_error("Mission transition requires one active mission");
  }
  switch (transition.operation) {
    case MissionTransition::retire:
    case MissionTransition::restart_stage:
      return;
    case MissionTransition::checkpoint:
      if (!gameplay_request_->checkpoint.empty()) {
        throw std::runtime_error(
            "Checkpoint return requires complete world restoration");
      }
      return;
    case MissionTransition::none:
      break;
  }
  throw std::invalid_argument("Invalid mission transition");
}
void StartupSession::complete_mission_transition(
    MissionTransitionRequest transition) {
  // Composition has retired all mission owners. Only retirement releases the
  // root wait; fresh reentry retains its mission identity and permanent owner.
  // Core I.1 and performance MEM.9 apply in implementation: typed publication,
  // constant-size observations, no payload copies or per-frame allocation.
  validate_mission_transition(transition);
  if (!gameplay_request_) {
    throw std::logic_error("Mission transition lost its entry request");
  }
  if (transition.operation == MissionTransition::retire) {
    mission_retired_ = true;
    return;
  }
  auto& request = gameplay_request_.value();
  if (transition.operation == MissionTransition::restart_stage) {
    request.checkpoint.clear();
  }
  request.progression = frontend_->profile().progression();
  // Restart keeps the committed session appearance; the pause addition does
  // not rewrite the permanent front-end selection merely to survive reentry.
  request.selection.costume = transition.costume;
}
void StartupSession::stop_mission_music() {
  // The current mission owns all cue/device resources. Its destruction has
  // already stopped and joined audio before the root wait can complete. This
  // acknowledges that postcondition; no live music is silently ignored.
  if (!mission_retired_) {
    throw std::logic_error("Mission audio must retire before the root stop");
  }
  retired_music_observed_ = true;
}
void StartupSession::unload_concept_mission() {
  if (!gameplay_request_ || gameplay_request_->mission != "concept" ||
      !mission_retired_ || !retired_music_observed_ || concept_unloaded_) {
    throw std::logic_error(
        "Concept unload requires completed mission teardown");
  }
  concept_unloaded_ = true;
}
double StartupSession::mission_status() const {
  if (!gameplay_request_ || !mission_retired_ || !retired_music_observed_ ||
      (gameplay_request_->mission == "concept" && !concept_unloaded_)) {
    throw std::logic_error("Mission status precedes retirement consumption");
  }
  constexpr double kRetired = 2;
  return kRetired;
}
bool StartupSession::quit_requested() const {
  return frontend_ != nullptr && frontend_->selection() &&
         frontend_->selection()->action == MenuAction::quit &&
         frontend_->progress(Screen::title_menu).status == FlowStatus::complete;
}
}  // namespace bw::game
