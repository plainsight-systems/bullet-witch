// Front-end handoff extension: validate the Lua mission argument, dispatch to
// the receiving game service, and suspend at its explicit completion gate.
// Design MCP I.30 (#ri-encapsulate) contains the compatibility boundary;
// implementation E.6 (#re-raii) preserves VM cleanup on exceptions, and
// cpp-performance COPY.4
// (guidelines/copy-move/COPY.4-rule-of-five-rule-of-zero.md) retains owning
// context semantics. The borrowed string lasts through dispatch; the receiver
// owns any retained request. No world work or success is invented. G1/G2:
// original root package -> init -> repeated main, with 19 startup bindings
// delegated to explicit game services. This is not a screen renderer.
// Resources/services must outlive this nonmovable driver. Tag must be exclusive
// to this driver; destructor releases it. Single-threaded and nonreentrant.
// Algorithm: request logical DPK, advance resource phases, copy/decode package,
// load original root bytecode, pump init, then main until pending/error/budget.
// _WaitTitle preserves its initial pending return; completed waits resume in
// the same pump. Unknown Lua/native calls fail visibly. Costs O(package bytes)
// setup plus Lua/handler work. Optimization: retain one VM across main calls.
// Global-value side effects belong to game services, never a fabricated array.
// MCP design/implementation: I.30 (#ri-encapsulate) isolates the Lua/native
// boundary; SL.con.3 (#rsl-bounds) validates argument counts/types. Performance
// COPY.5 (guidelines/copy-move/COPY.5-resource-owning-types-are-move-only.md)
// motivates unique context ownership; this callback owner has fixed identity.

// Naming design/implementation consultation: cpp-guidelines P.3 (#rp-what)
// selects role-based names. cpp-performance COPY.4 (guidelines/copy-move/
// COPY.4-rule-of-five-rule-of-zero.md) preserves owning values and implicit
// moves; renaming adds no wrapper, allocation or payload copy.

#include "root_driver.hpp"

#include <cmath>
#include <limits>

#include "bw/formats/named_file_package.hpp"
namespace bw::game {
namespace {
using resources::Error;
using resources::ErrorCode;
using script::Reply;
using script::RunState;
using script::Value;
void count(std::span<const Value> args, std::size_t expected) {
  if (args.size() != expected) {
    throw Error(ErrorCode::malformed, "Native argument count");
  }
}
double number(const Value& value) {
  const auto* n = std::get_if<double>(&value);
  if (!n || !std::isfinite(*n)) {
    throw Error(ErrorCode::malformed, "Finite native number required");
  }
  return *n;
}
int slot(const Value& value) {
  const double n = number(value);
  if (n < 0 || n > std::numeric_limits<int>::max() || std::floor(n) != n) {
    throw Error(ErrorCode::malformed, "Native global slot");
  }
  return static_cast<int>(n);
}
}  // namespace
void StartupServices::start_mission(std::string_view) {
  throw std::runtime_error("Mission receiver is not configured");
}
bool StartupServices::mission_pending() {
  throw std::runtime_error("Mission receiver is not configured");
}
RootDriver::RootDriver(resources::ResourceService& resources,
                       StartupServices& services, resources::ResourceTag tag)
    : resources_(resources), services_(services), tag_(tag) {
  bindings();
  resources_.get().request("script.root.dpk", tag_);
}
RootDriver::~RootDriver() { resources_.get().release(tag_); }
void RootDriver::bindings() {
  context_.bind("_StartMission", [this](auto args) {
    count(args, 1);
    const auto* mission = std::get_if<std::string>(&args[0]);
    if (mission == nullptr || mission->empty()) {
      throw Error(ErrorCode::malformed,
                  "Mission name must be a nonempty string");
    }
    services_.get().start_mission(*mission);
    return Reply{};
  });
  context_.bind("_WaitMission", [this](auto args) {
    count(args, 0);
    return Reply{{}, [this] { return services_.get().mission_pending(); }};
  });
  context_.bind("_GetGlobalValue", [this](auto args) {
    count(args, 1);
    return Reply{{services_.get().global(slot(args[0]))}, {}};
  });
  context_.bind("_SetGlobalValue", [this](auto args) {
    count(args, 2);
    services_.get().set_global(slot(args[0]), number(args[1]));
    return Reply{};
  });
  context_.bind("_GetLatestStageSelectNo", [this](auto args) {
    count(args, 0);
    return Reply{{services_.get().latest_stage()}, {}};
  });
  struct Named {
    const char* suffix;
    Screen screen;
  };
  constexpr std::array names{Named{"CompanyLogo", Screen::company_logo},
                             Named{"MemoryCardCheck", Screen::memory_card},
                             Named{"Advertise", Screen::advertise},
                             Named{"Title", Screen::title},
                             Named{"CharacterLoad", Screen::character_load},
                             Named{"TitleMenu", Screen::title_menu}};
  for (const auto& name : names) {
    context_.bind(std::string("_Exec") + name.suffix,
                  [this, screen = name.screen](auto args) {
                    count(args, 0);
                    services_.get().execute(screen);
                    return Reply{};
                  });
    context_.bind(
        std::string("_Wait") + name.suffix,
        [this, screen = name.screen](auto args) {
          count(args, 0);
          return Reply{
              {}, [this, screen, first = screen == Screen::title]() mutable {
                if (first) {
                  first = false;
                  return true;
                }
                return services_.get().pending(screen);
              }};
        });
  }
  context_.bind("_GetTitleStatus", [this](auto args) {
    count(args, 0);
    return Reply{{services_.get().status(Screen::title)}, {}};
  });
  context_.bind("_GetTitleMenuStatus", [this](auto args) {
    count(args, 0);
    return Reply{{services_.get().status(Screen::title_menu)}, {}};
  });
}
script::RunResult RootDriver::advance() {
  if (result_.state == RunState::failed) {
    return result_;
  }
  try {
    if (!loaded_) {
      const auto load = resources_.get().pump(tag_);
      if (load.state == resources::LoadState::failed) {
        throw Error(ErrorCode::unsupported, load.diagnostic);
      }
      if (load.state != resources::LoadState::ready) {
        return result_ = {RunState::pending, "Loading script.root.dpk"};
      }
      auto lease = resources_.get().acquire("script.root.dpk");
      if (!lease) {
        throw Error(ErrorCode::not_found, "Root package unpublished");
      }
      const auto payload = lease->bytes();
      auto package =
          resources::NamedFilePackage::parse({payload.begin(), payload.end()});
      context_.load_binary(package.member("root.lub"));
      context_.start("init");
      loaded_ = true;
    }
    constexpr unsigned main_budget = 64;
    for (unsigned n = 0; n < main_budget; ++n) {
      result_ = context_.pump();
      if (result_.state != RunState::complete) {
        return result_;
      }
      context_.start("main");
    }
    return result_ = {RunState::running, "Host root dispatch budget reached"};
  } catch (const std::exception& failure) {
    return result_ = {RunState::failed, failure.what()};
  }
}
}  // namespace bw::game
