// Character-load lifecycle over the PC profile service. The borrowed profile,
// storage and fade service outlive this non-owning policy; main-thread calls
// only. Resource readiness gates loading and visibility. Missing files use the
// Profile defaults; invalid/I/O results remain an explicit retry-or-continue
// dialog. Continuing after failure disables later saves for this session.
// Algorithm: await resources -> reveal (original 0.1 s) -> load candidate ->
// resolve error if needed -> conceal (original zero duration) -> publish
// result. Original desktop storage/dialog adaptations and evidence are
// documented in research/engine/subsystems/frontend-integration.md. This does
// not emulate console storage-device selection. O(1) frame work outside bounded
// profile I/O; no per-frame allocation. Design MCP I.30 (#ri-encapsulate)
// contains original compatibility behavior; implementation E.6 (#re-raii)
// delegates buffer/handle cleanup to owners. cpp-performance MEM.9
// (guidelines/memory/ MEM.9-embedded-allocate-at-init.md) keeps lifecycle state
// inline.
#pragma once
#include <functional>

#include "profile.hpp"
#include "startup_flows.hpp"

namespace bw::game {
class ProfileLoadFlow {
 public:
  ProfileLoadFlow(Profile& profile, platform::SaveStorage& storage,
                  LogoMedia& media);
  void start();
  void update(LogoUpdate input, bool resources_ready);
  ScreenProgress progress() const { return progress_; }
  bool error_pending() const;
  ProfileResult error() const { return error_; }
  unsigned error_choice() const { return error_choice_; }
  bool saving_enabled() const { return saving_enabled_; }

 private:
  enum class Phase : std::uint8_t {
    resources,
    revealing,
    load,
    error,
    conceal,
    closing,
    finish
  };
  std::reference_wrapper<Profile> profile_;
  std::reference_wrapper<platform::SaveStorage> storage_;
  std::reference_wrapper<LogoMedia> media_;
  Phase phase_{Phase::resources};
  ScreenProgress progress_;
  ProfileResult error_;
  unsigned error_choice_{};
  bool saving_enabled_{true};
};
}  // namespace bw::game
