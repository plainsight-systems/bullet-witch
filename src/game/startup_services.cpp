// G2: bind original-root Exec/Wait to owned startup flow tasks. See header and
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
[[noreturn]] void unsupported_screen(Screen screen) {
  switch (screen) {
    case Screen::advertise:
      throw std::runtime_error("Advertise screen is not implemented");
    case Screen::title:
      throw std::runtime_error("Title screen is not implemented");
    case Screen::character_load:
      throw std::runtime_error("Character load screen is not implemented");
    case Screen::title_menu:
      throw std::runtime_error("Title menu screen is not implemented");
    default:
      throw std::invalid_argument("Unsupported startup screen");
  }
}
}  // namespace
StartupSession::StartupSession(LogoMedia& media)
    : logos_(media,
             {"data/movie/marvelousLogo.ssu", "data/movie/xeedLogo.ssu"}) {}
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
script::RunResult StartupSession::advance(LogoUpdate input) {
  if (!std::isfinite(input.delta) || input.delta < 0) {
    throw std::invalid_argument("Startup delta must be finite and nonnegative");
  }
  if (root_result_.state == script::RunState::failed) {
    return root_result_;
  }
  input_ = input;
  scheduler_.tick(false);
  return root_result_;
}
StartupSession::ScreenTask& StartupSession::task(Screen screen) {
  switch (screen) {
    case Screen::company_logo:
      return logo_task_;
    case Screen::memory_card:
      return check_task_;
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
    default:
      unsupported_screen(screen);
  }
}
double StartupSession::global(int slot) const {
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
  } else {
    check_.start();
  }
  task(screen).waiting_for_activation = false;
}
void StartupSession::update(Screen screen) {
  if (screen == Screen::company_logo) {
    logos_.update(input_);
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
    throw std::runtime_error(logos_.diagnostic());
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
  throw std::runtime_error("Saved stage selection is not implemented");
}
}  // namespace bw::game
