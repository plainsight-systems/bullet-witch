// Front-end extension follows the header lifetime and owned handoff contract.
// Keep Lua dispatch authoritative, validate native slot values, and retain a
// pending mission wait after capturing the request. Design MCP I.30
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
                               FrontEnd* frontend)
    : logos_(media,
             {"data/movie/marvelousLogo.ssu", "data/movie/xeedLogo.ssu"}),
      advertise_(media),
      title_(media),
      title_surface_(title_surface),
      frontend_(frontend) {}
StartupSession::~StartupSession() {
  if (title_surface_ != nullptr) title_surface_->end();
}
void StartupSession::start_root(resources::ResourceService& resources,
                                resources::ResourceTag tag) {
  if (root_) {
    throw std::logic_error("Startup root already started");
  }
  auto root = std::make_unique<RootDriver>(resources, *this, tag);
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
  } else if (screen == Screen::character_load || screen == Screen::title_menu) {
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
  } else if (screen == Screen::character_load || screen == Screen::title_menu) {
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
    if (state != tasks::State::idle) {
      return true;
    }
    scheduler_.request(*entry.id, tasks::State::created);
  }
  return result.status != FlowStatus::complete;
}
double StartupSession::status(Screen screen) const {
  return progress(screen).result;
}
double StartupSession::latest_stage() const {
  if (frontend_ != nullptr && frontend_->selection() &&
      frontend_->selection()->action == MenuAction::stage_select) {
    return frontend_->selection()->stage;
  }
  throw std::runtime_error("Saved stage selection is not implemented");
}
void StartupSession::start_mission(std::string_view mission) {
  if (frontend_ == nullptr || gameplay_request_ ||
      frontend_->progress(Screen::title_menu).status != FlowStatus::complete) {
    throw std::logic_error(
        "Mission request requires one confirmed menu selection");
  }
  const auto selected = frontend_->selection();
  if (!selected) {
    throw std::logic_error("Mission request has no menu selection");
  }
  const auto selection = *selected;
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
  GameplayRequest request{std::string(mission), selection, {}};
  if (selection.action == MenuAction::continue_game) {
    const auto checkpoint = frontend_->profile().checkpoint();
    if (checkpoint.empty()) {
      throw std::logic_error("Continue requires checkpoint data");
    }
    request.checkpoint.assign(checkpoint.begin(), checkpoint.end());
  }
  gameplay_request_ = std::move(request);
}
bool StartupSession::mission_pending() {
  if (!gameplay_request_) {
    throw std::logic_error("Mission Wait precedes StartMission");
  }
  return true;
}
bool StartupSession::quit_requested() const {
  return frontend_ != nullptr && frontend_->selection() &&
         frontend_->selection()->action == MenuAction::quit &&
         frontend_->progress(Screen::title_menu).status == FlowStatus::complete;
}
}  // namespace bw::game
