// Memory composition: target-selected resource, runtime and media domains are
// required capabilities. Pass each to its owner before work starts; retained
// catalog/sample/context storage keeps the capability through final release.
// No native backing choice or historical aggregate budget is inferred here.
// R.1 (#rr-raii) and MEM.6 (guidelines/memory/
// MEM.6-inject-allocators-behind-one-interface.md), read in both phases, keep
// exception cleanup and injection explicit. No per-frame composition work.
// Front-end extension: SaveStorage is borrowed for the whole run alongside
// Files and StartupHost. The target chooses a writable profile root separate
// from the game installation. The framework owns profile/menu composition and
// returns at a confirmed gameplay request until a mission consumer exists.
// No checkpoint is synthesized. Bounded profile I/O and one menu image load per
// activation supplement the costs below. Design I.30 (#ri-encapsulate),
// implementation E.6 (#re-raii), and performance COPY.4 below preserve service
// separation, owner lifetimes and absence of per-frame profile copies.
// Startup route extension: game policies borrow capabilities; composition owns
// them through cleanup. Design MCP R.20 (#rr-owner) retains explicit lifetime;
// implementation E.6 (#re-raii) unwinds on failures. cpp-performance COPY.4
// (guidelines/copy-move/COPY.4-rule-of-five-rule-of-zero.md) preserves
// container ownership without per-frame payload copies. Shared presentation
// contracts are described in
// research/engine/subsystems/startup-title-integration.md. Shared launch
// configuration and startup orchestration for the game/engine. Target entry
// points normalize arguments, parse before opening native output, then borrow
// Files/StartupHost for run_startup. All calls use the main thread;
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
#include <memory>
#include <span>

#include "bw/memory/domain.hpp"
#include "bw/platform/files.hpp"
#include "bw/platform/save_storage.hpp"
#include "bw/platform/startup_host.hpp"
#include "bw/video/player.hpp"

namespace bw::framework {
struct StartupOptions {
  std::filesystem::path game;
  std::filesystem::path profile;
  static constexpr int kDefaultMaximumSeconds = 300;
  int maximum_seconds{kDefaultMaximumSeconds};
  bool verify{};
  platform::WindowOptions window;
};
// Target-selected capabilities; device allocation belongs to the native host.
struct StartupMemory {
  std::shared_ptr<memory::Domain> resources;
  std::shared_ptr<memory::Domain> runtime;
  std::shared_ptr<memory::Domain> media;
};
StartupOptions parse_startup_options(std::span<char*> arguments);
// Requires validated options, a live main-thread host and a nonempty factory.
int run_startup(const StartupOptions& configuration,
                platform::StartupHost& host, platform::Files& files,
                platform::SaveStorage& saves,
                video::Player::OpenDecoder open_decoder,
                const StartupMemory& memory);
}  // namespace bw::framework
