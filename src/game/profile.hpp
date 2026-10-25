// Progression remains authoritative in the retained Record bytes. Typed
// observations own snapshots; purchases/rewards validate then encode mapped
// fields atomically. No caller can mutate a competing permanent mirror.
// Design C.9/I.30; implementation E.4 (#re-design-invariants) preserves state
// on rejection. Both-phase MEM.9 (guidelines/memory/
// MEM.9-embedded-allocate-at-init.md) keeps all progression work fixed-size.
// Game profile compatibility boundary: retained original System/Record bytes
// with named, verified menu fields. Unknown bytes survive load/save unchanged.
// Inputs are exact-sized original files through a borrowed storage capability;
// successful load atomically publishes all validated data; failure retains old
// state. Missing files use recovered startup defaults. Save replaces each dirty
// file independently and reports the first failure; no checkpoint is invented.
// Main-thread only. Returned spans borrow until mutation/destruction.
// Algorithm: load candidate -> validate bounded sections -> publish; setters
// touch only mapped fields; save dirty buffers and clear each flag only after
// success. Cost O(total input), bounded 308264 bytes; no per-frame work. Design
// and implementation: cpp-guidelines R.1 (#rr-raii) composes byte owners;
// cpp-performance COPY.4
// (guidelines/copy-move/COPY.4-rule-of-five-rule-of-zero.md) retains
// rule-of-zero values. See research/engine/subsystems/profile-persistence.md.
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

#include "bw/platform/save_storage.hpp"
#include "game/progression_save.hpp"

namespace bw::game {
struct ProfileResult {
  platform::SaveStatus status{platform::SaveStatus::ok};
  std::string_view file;
};
class Profile {
 public:
  Profile();
  // Replace the active profile only after all available files validate.
  ProfileResult load(platform::SaveStorage& storage);
  // Persist dirty System/Record files independently; checkpoints remain
  // read-only.
  ProfileResult save(platform::SaveStorage& storage);
  Progression progression() const;
  PurchaseResult purchase(UpgradeSelection selection);
  // Call once from the result task's publication phase; this is not a result
  // ledger. Returns points actually credited after the original wallet cap.
  std::uint32_t award_result(MissionResultReward result);
  std::optional<CheckpointPlayer> checkpoint_player() const;
  unsigned current_difficulty() const;
  void set_current_difficulty(unsigned value);
  // Original PC operation 2300 refreshes installed extra missions/costumes.
  void refresh_installed_content();
  bool costume_available(unsigned value) const;
  bool extra_available(unsigned value) const;
  unsigned difficulty() const;
  void set_difficulty(unsigned value);
  unsigned music_volume() const;
  unsigned effects_volume() const;
  void set_music_volume(unsigned value);
  void set_effects_volume(unsigned value);
  unsigned costume() const;
  void set_costume(unsigned value);
  bool has_checkpoint() const noexcept;
  unsigned checkpoint_stage() const;
  unsigned checkpoint_difficulty() const;
  std::span<const std::byte> checkpoint() const noexcept;
  std::span<const std::byte> system_bytes() const noexcept;
  std::span<const std::byte> record_bytes() const noexcept;
  bool dirty() const noexcept;

 private:
  static constexpr std::size_t kSystemBytes = 72;
  static constexpr std::size_t kRecordBytes = 992;
  std::array<std::byte, kSystemBytes> system_{};
  std::array<std::byte, kRecordBytes> record_{};
  std::vector<std::byte> checkpoint_;
  bool system_dirty_{}, record_dirty_{};
};
}  // namespace bw::game
