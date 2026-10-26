// Startup content operations: I.1 (#ri-explicit), read in both phases, names
// strict load and explicit replacement separately. MEM.9 bounds their temporary
// save buffers outside steady-state work. The original all-zero empty-save
// encoding is not a playable checkpoint; frontend-profile-load.md documents
// the one-time legacy two-file reconstruction migration.
// Power-up consumers receive UpgradePurchaseControl, permitting only owned
// progression observations and checked purchases. They cannot replace/load/save
// the profile, alter options, award results or mutate original byte buffers.
// This capability borrows the stable Profile until task teardown; Profile is
// noncopyable/nonmovable. Both-phase Core C.9 (#rc-private), C.21 (#rc-five)
// and COPY.5
// (guidelines/copy-move/COPY.5-resource-owning-types-are-move-only.md) preserve
// one authority and bounded value observations. Progression remains
// authoritative in the retained Record bytes. Typed observations own snapshots;
// purchases/rewards validate then encode mapped fields atomically. No caller
// can mutate a competing permanent mirror. Design C.9/I.30; implementation E.4
// (#re-design-invariants) preserves state on rejection. Both-phase MEM.9
// (guidelines/memory/ MEM.9-embedded-allocate-at-init.md) keeps all progression
// work fixed-size. Game profile compatibility boundary: retained original
// System/Record bytes with named, verified menu fields. Unknown bytes survive
// load/save unchanged. Inputs are exact-sized original files through a borrowed
// storage capability; successful load atomically publishes all validated data;
// failure retains old state. Missing files use recovered startup defaults. Save
// replaces each dirty file independently and reports the first failure; no
// checkpoint is invented. Main-thread only. Returned spans borrow until
// mutation/destruction. Algorithm: load candidate -> validate bounded sections
// -> publish; setters touch only mapped fields; save dirty buffers and clear
// each flag only after success. Cost O(total input), bounded 308264 bytes; no
// per-frame work. Design and implementation: cpp-guidelines R.1 (#rr-raii)
// composes byte owners; cpp-performance COPY.4
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
#include "progression_save.hpp"

namespace bw::game {
inline constexpr std::size_t profile_system_bytes = 72;
class Profile;
struct ControllerSettings;
class SettingsControl;
class SettingsSaveControl;
// An entry snapshot may only be restored by its originating Profile. It owns
// the original fixed System bytes and dirty state, not mutation authority.
class SettingsCheckpoint {
 public:
  friend class SettingsControl;

 private:
  SettingsCheckpoint(const Profile* owner,
                     std::array<std::byte, profile_system_bytes> bytes,
                     bool dirty, std::uint64_t generation)
      : owner_(owner), bytes_(bytes), dirty_(dirty), generation_(generation) {}
  const Profile* owner_;
  std::array<std::byte, profile_system_bytes> bytes_;
  bool dirty_;
  std::uint64_t generation_;
};
// Native Options edits only System settings. It can snapshot/restore its own
// live edit transaction and persist System through a supplied storage port;
// no Record progression or world/checkpoint mutation is available. Profile
// remains the stable authority and outlives all capabilities/checkpoints.
// Checked candidate publication preserves unknown bytes. Both design corpora:
// E.4 (#re-design-invariants) and MEM.9 (guidelines/memory/
// MEM.9-embedded-allocate-at-init.md) keep rollback valid and snapshots
// bounded.
class SettingsControl {
 public:
  SettingsCheckpoint checkpoint() const;
  bool changed_since(const SettingsCheckpoint& checkpoint) const;
  void restore(const SettingsCheckpoint& checkpoint) const;
  bool subtitles_enabled() const;
  bool autosave_enabled() const;
  unsigned music_volume() const;
  unsigned effects_volume() const;
  void set_subtitles(bool enabled) const;
  void set_autosave(bool enabled) const;
  void set_music_volume(unsigned value) const;
  void set_effects_volume(unsigned value) const;
  ControllerSettings controller_settings() const;
  void set_controller_settings(const ControllerSettings& settings) const;

 private:
  platform::SaveStatus save(platform::SaveStorage& storage) const;
  friend class SettingsSaveControl;
  friend class Profile;
  explicit SettingsControl(Profile& profile) : profile_(&profile) {}
  Profile* profile_;
};
inline constexpr std::size_t help_topic_count = 33;
struct HelpProgress {
  std::array<bool, help_topic_count> available{}, read{};
};
// Help may observe learned topics and mark an available topic read. It cannot
// unlock topics, mutate settings/progression, or load/save Profile. The stable
// Profile outlives this value capability. Core C.9/I.1 and performance MEM.9
// (guidelines/memory/MEM.9-embedded-allocate-at-init.md), both phases
// consulted, retain one authoritative fixed Record and copied observations.
class HelpControl {
 public:
  HelpProgress progress() const;
  void mark_read(unsigned topic) const;

 private:
  friend class Profile;
  explicit HelpControl(Profile& profile) : profile_(&profile) {}
  Profile* profile_;
};
// Persistence admission names only the original System file. Ordinary Options
// navigation receives SettingsControl; only its save phase gets this handle.
class SettingsSaveControl {
 public:
  platform::SaveStatus save() const;

 private:
  friend class Profile;
  SettingsSaveControl(SettingsControl settings, platform::SaveStorage& storage)
      : settings_(settings), storage_(&storage) {}
  SettingsControl settings_;
  platform::SaveStorage* storage_;
};
// Options may persist only the retained Record after an explicit upgrade-save
// decision. It cannot change progression, System settings or checkpoint data.
// The stable Profile and storage owner outlive this borrowed capability.
class RecordSaveControl {
 public:
  platform::SaveStatus save() const;

 private:
  friend class Profile;
  RecordSaveControl(Profile& profile, platform::SaveStorage& storage)
      : profile_(&profile), storage_(&storage) {}
  Profile* profile_;
  platform::SaveStorage* storage_;
};
class UpgradePurchaseControl {
 public:
  Progression progression() const;
  PurchaseResult purchase(UpgradeSelection selection) const;

 private:
  friend class Profile;
  explicit UpgradePurchaseControl(Profile& profile) : profile_(&profile) {}
  Profile* profile_;
};
struct ProfileResult {
  platform::SaveStatus status{platform::SaveStatus::ok};
  std::string_view file;
};
struct MissionBestRecord {
  std::uint32_t score{};
  unsigned rank{};
};
class Profile {
 public:
  Profile();
  ~Profile() = default;
  Profile(const Profile&) = delete;
  Profile& operator=(const Profile&) = delete;
  Profile(Profile&&) = delete;
  Profile& operator=(Profile&&) = delete;
  HelpControl help_control() { return HelpControl(*this); }
  SettingsControl settings_control() { return SettingsControl(*this); }
  SettingsSaveControl settings_save_control(platform::SaveStorage& storage) {
    return {settings_control(), storage};
  }
  RecordSaveControl record_save_control(platform::SaveStorage& storage) {
    return {*this, storage};
  }
  UpgradePurchaseControl upgrade_purchases() {
    return UpgradePurchaseControl(*this);
  }
  // Replace the active profile only after all available files validate.
  ProfileResult load(platform::SaveStorage& storage);
  // Original startup requires all three files. Missing System requests content
  // creation; missing Record is a load failure. Valid legacy two-file port
  // saves receive only the original empty checkpoint sentinel (no reset).
  ProfileResult load_startup(platform::SaveStorage& storage);
  // Explicit fresh-content operation after original startup policy admits it.
  // Writes System, the original all-zero no-checkpoint sentinel, then Record.
  // Publishes defaults only after all writes succeed; partial disk writes are
  // reported, never presented as an atomic multi-file transaction.
  ProfileResult create_defaults(platform::SaveStorage& storage);
  // Persist dirty System/Record files independently; checkpoints remain
  // read-only.
  ProfileResult save(platform::SaveStorage& storage);
  Progression progression() const;
  // Copied persisted selector observations, not mutable result authority.
  // Campaign IDs are 1..6; concept IDs retain the original 0..63 identity.
  MissionBestRecord mission_best(bool is_concept, unsigned mission_id) const;
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
  // Original System flags; Options owns edits through SettingsControl.
  bool subtitles_enabled() const;
  bool autosave_enabled() const;
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
  ProfileResult load_files(platform::SaveStorage& storage, bool require_all);
  friend class HelpControl;
  friend class RecordSaveControl;
  friend class SettingsControl;
  static constexpr std::size_t kSystemBytes = profile_system_bytes;
  static constexpr std::size_t kRecordBytes = 992;
  std::array<std::byte, kSystemBytes> system_{};
  std::array<std::byte, kRecordBytes> record_{};
  std::vector<std::byte> checkpoint_;
  bool system_dirty_{}, record_dirty_{};
  std::uint64_t system_generation_{};
};
}  // namespace bw::game
