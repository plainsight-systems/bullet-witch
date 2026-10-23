// Shared launch configuration and startup orchestration for the game/engine.
// Target entry points normalize arguments, parse before opening native output,
// then borrow Files/StartupHost for run_startup. All calls use the main thread;
// capabilities outlive the call and the decoder factory creates owned sessions.
// Parsing owns its result; invalid input throws. run_startup returns zero for
// normal close or the verified boundary, throws on failure, and retires game /
// media owners before returning. See docs/native-startup-host.md.
// Algorithm: parse options -> mount resources -> construct game/media session
// -> pump input, advance media/game and present -> unwind owned session
// resources. Existing loop timing and verification are preserved. Cost is the
// composed resource/media work; capabilities are borrowed without copying
// native owners. Design MCP: C.129 (#rh-kind) separates codec contracts from
// implementation; I.27 (#ri-pimpl) keeps native details behind the existing
// host declaration. cpp-performance COPY.5 (guidelines/copy-move/
// COPY.5-resource-owning-types-are-move-only.md) retains unique media
// ownership. Implementation MCP: E.6 (#re-raii) retains exception-safe owner
// cleanup; COPY.5 above preserves unique ownership through composition.
#pragma once
#include <filesystem>
#include <span>

#include "bw/platform/files.hpp"
#include "bw/platform/startup_host.hpp"
#include "bw/video/player.hpp"

namespace bw::framework {
struct StartupOptions {
  std::filesystem::path game;
  static constexpr int kDefaultMaximumSeconds = 90;
  int maximum_seconds{kDefaultMaximumSeconds};
  bool verify{};
};
StartupOptions parse_startup_options(std::span<char*> arguments);
// Requires validated options, a live main-thread host and a nonempty factory.
int run_startup(const StartupOptions& configuration,
                platform::StartupHost& host, platform::Files& files,
                video::Player::OpenDecoder open_decoder);
}  // namespace bw::framework
