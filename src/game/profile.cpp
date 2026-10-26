// Startup content operations preserve stable Profile identity and validate
// before publication. I.1 and MEM.9 were queried for design/implementation:
// explicit replacement admission, bounded save buffers, no frame allocations.
// Progression mutations decode one candidate, validate the complete operation,
// then encode only mapped Record fields. Unknown bytes remain unchanged.
// Design C.9/I.30; implementation E.4 (#re-design-invariants) preserves state;
// both-phase MEM.9 (guidelines/memory/MEM.9-embedded-allocate-at-init.md)
// confines costs to fixed fields and avoids frame-time profile copies.
// Restricted upgrade capability forwards checked transactions to the original
// Record owner; it exposes no profile load, save, options or raw-byte mutation.
// Both-phase C.9/C.21 and COPY.5 retain fixed borrowed owner identity.
// Native Options transactions snapshot the fixed System buffer, apply checked
// live edits and restore only to the same stable owner. System-only save clears
// its dirty flag after successful publication; Record/world state is outside
// this capability. Core E.4 and performance MEM.9 were read again for this
// implementation: fixed candidates, no frame allocation, failure preserves
// pending edits and unknown fields.
// Profile format implementation. Validate all bounded file inputs before
// publishing a candidate; retain unknown bytes and change only mapped fields.
// Original defaults: 0x4bb800/0x4bb900, persistence 0x4c2eb0/0x4c3050.
// Existing checkpoint bytes are read-only; startup may create only the
// original all-zero absent-checkpoint encoding, never world serialization.
// Fixed-size settings/record owners and move-published checkpoint buffer bound
// work to O(input bytes). Main-thread only; storage is borrowed for each call.
// Design/implementation: cpp-guidelines R.1 (#rr-raii) owns every buffer;
// cpp-performance COPY.4
// (guidelines/copy-move/COPY.4-rule-of-five-rule-of-zero.md) preserves implicit
// moves and rule-of-zero values.
#include "game/profile.hpp"

#include <algorithm>
#include <stdexcept>
#include <utility>

#include "game/controller_settings.hpp"

namespace bw::game {
namespace {
constexpr unsigned kByteMask = 255U;
constexpr unsigned kByteBits = 8U;
constexpr unsigned kWordBits = 16U;
constexpr unsigned kCheckpointBytes = 307200;
constexpr unsigned kCheckpointVersion = 0x10001;
constexpr unsigned kMinimumCheckpointBytes = 0x530;
constexpr unsigned kBaseBytes = 0x500;
constexpr unsigned kHeaderBytes = 0x30;
constexpr unsigned kFullVolume = 256;
constexpr unsigned kBaseOffsetField = 12;
constexpr unsigned kCostumeFlagsOffset = 0x7c;
constexpr unsigned kExtraCount = 64;
constexpr unsigned kExtraFlagsOffset = 0x3c;
constexpr unsigned kMaximumCostume = 11;
constexpr unsigned kStageCount = 6;
constexpr unsigned kEffectsVolumeOffset = 8;
constexpr unsigned kSectionPairBytes = 8;
constexpr unsigned kBaseSizeField = 8;
constexpr unsigned kCostumeCount = 12;
constexpr std::string_view kSystem = "BulletWitchSystem";
constexpr std::string_view kRecord = "BulletWitchRecord";
constexpr std::string_view kCheckpoint = "BulletWitchSaveData";
unsigned read16(std::span<const std::byte> data, std::size_t offset) {
  return std::to_integer<unsigned>(data[offset]) |
         (std::to_integer<unsigned>(data[offset + 1]) << kByteBits);
}
std::uint32_t read32(std::span<const std::byte> data, std::size_t offset) {
  return read16(data, offset) | (read16(data, offset + 2) << kWordBits);
}
void write16(std::span<std::byte> data, std::size_t offset, unsigned value) {
  data[offset] = static_cast<std::byte>(value & kByteMask);
  data[offset + 1] = static_cast<std::byte>((value >> kByteBits) & kByteMask);
}
void write32(std::span<std::byte> data, std::size_t offset, unsigned value) {
  write16(data, offset, value);
  write16(data, offset + 2, value >> kWordBits);
}
bool valid_checkpoint(std::span<const std::byte> data) {
  if (data.size() != kCheckpointBytes ||
      read32(data, 0) != kCheckpointVersion) {
    return false;
  }
  const auto used = read32(data, 4);
  if (used < kMinimumCheckpointBytes || used > data.size() ||
      read32(data, kBaseSizeField) != kBaseBytes) {
    return false;
  }
  for (std::size_t offset = kBaseSizeField; offset < kHeaderBytes;
       offset += kSectionPairBytes) {
    const auto size = read32(data, offset);
    const auto start = read32(data, offset + 4);
    if (size != 0 &&
        (start < kHeaderBytes || start > used || size > used - start)) {
      return false;
    }
    if (size != 0) {
      for (std::size_t previous = kBaseSizeField; previous < offset;
           previous += kSectionPairBytes) {
        const auto previous_size = read32(data, previous);
        const auto previous_start = read32(data, previous + 4);
        if (previous_size != 0 && start < previous_start + previous_size &&
            previous_start < start + size) {
          return false;
        }
      }
    }
  }
  const auto base = read32(data, kBaseOffsetField);
  const auto stage = std::to_integer<unsigned>(data[base]);
  return stage >= 1 && stage <= kStageCount &&
         std::to_integer<unsigned>(data[base + 2]) <= 4;
}
}  // namespace
// Menu records read the retained best score and rank; no result is invented
// for the currently running mission. Unmapped companion bytes remain opaque.
MissionBestRecord Profile::mission_best(bool is_concept,
                                        unsigned mission_id) const {
  constexpr std::size_t kCampaignBase = 0x8c, kConceptBase = 0xe0;
  constexpr std::size_t kEntryBytes = 12, kScoreOffset = 4, kRankOffset = 8;
  if ((!is_concept && (mission_id == 0 || mission_id > kStageCount)) ||
      (is_concept && mission_id >= kExtraCount)) {
    throw std::invalid_argument(
        "Mission record identity outside source bounds");
  }
  const auto index = is_concept ? mission_id : mission_id - 1;
  const auto offset =
      (is_concept ? kConceptBase : kCampaignBase) + index * kEntryBytes;
  return {read32(record_, offset + kScoreOffset),
          std::to_integer<unsigned>(record_.at(offset + kRankOffset))};
}
Profile::Profile() {
  write16(system_, 0, 1);
  write16(system_, 2, 3);
  write32(system_, 4, kFullVolume);
  write32(system_, kEffectsVolumeOffset, kFullVolume);
  // Keyboard scan-code pairs in the original system defaults, not pointers.
  constexpr std::array<std::pair<std::size_t, unsigned>, 8> controls{
      {{0x24, 0x001f0011},
       {0x28, 0x0020001e},
       {0x2c, 0x012d012c},
       {0x30, 0x00390013},
       {0x34, 0x0012002a},
       {0x3a, 0x01340135},
       {0x3e, 0x002d002c},
       {0x42, 0x0001002e}}};
  for (const auto& [offset, value] : controls) {
    write32(system_, offset, value);
  }
  constexpr std::size_t key_offset = 0x38;
  constexpr unsigned key = 0x012e;
  write16(system_, key_offset, key);
  constexpr std::array<std::pair<std::size_t, unsigned>, 5> record_defaults{
      {{2, 0x0101}, {4, 1}, {8, 0x100}, {12, 1}, {28, 1}}};
  for (const auto& [offset, value] : record_defaults) {
    write32(record_, offset, value);
  }
}
ProfileResult Profile::load(platform::SaveStorage& storage) {
  return load_files(storage, false);
}
ProfileResult Profile::load_startup(platform::SaveStorage& storage) {
  return load_files(storage, true);
}
ProfileResult Profile::load_files(platform::SaveStorage& storage,
                                  bool require_all) {
  // Validate a complete candidate so failed reads leave the active profile
  // intact.
  Profile candidate;
  auto system = storage.read(kSystem, candidate.system_.size());
  if (system.status == platform::SaveStatus::ok) {
    if (system.bytes.size() != candidate.system_.size() ||
        read16(system.bytes, 0) > 4 || read32(system.bytes, 4) > kFullVolume ||
        read32(system.bytes, kEffectsVolumeOffset) > kFullVolume) {
      return {platform::SaveStatus::invalid, kSystem};
    }
    std::ranges::copy(system.bytes, candidate.system_.begin());
  } else if (system.status == platform::SaveStatus::missing) {
    if (require_all) return {system.status, kSystem};
    candidate.system_dirty_ = true;
  } else {
    return {system.status, kSystem};
  }
  auto record = storage.read(kRecord, candidate.record_.size());
  if (record.status == platform::SaveStatus::ok) {
    if (record.bytes.size() != candidate.record_.size() ||
        std::to_integer<unsigned>(record.bytes[0]) > 4 ||
        std::to_integer<unsigned>(record.bytes[1]) > kMaximumCostume) {
      return {platform::SaveStatus::invalid, kRecord};
    }
    try {
      (void)decode_record_progression(record.bytes);
    } catch (const std::invalid_argument&) {
      return {platform::SaveStatus::invalid, kRecord};
    }
    std::ranges::copy(record.bytes, candidate.record_.begin());
  } else if (record.status == platform::SaveStatus::missing) {
    if (require_all) return {record.status, kRecord};
    candidate.record_dirty_ = true;
  } else {
    return {record.status, kRecord};
  }
  auto checkpoint = storage.read(kCheckpoint, kCheckpointBytes);
  if (checkpoint.status == platform::SaveStatus::ok &&
      checkpoint.bytes.size() == kCheckpointBytes &&
      std::ranges::all_of(checkpoint.bytes,
                          [](std::byte byte) { return byte == std::byte{}; })) {
    // Original content creation persists the zeroed 4931c0 buffer. It is a
    // stored absence of a checkpoint, never a fabricated world snapshot.
  } else if (checkpoint.status == platform::SaveStatus::ok) {
    if (!valid_checkpoint(checkpoint.bytes)) {
      return {platform::SaveStatus::invalid, kCheckpoint};
    }
    try {
      (void)decode_checkpoint_player(checkpoint.bytes);
    } catch (const std::invalid_argument&) {
      return {platform::SaveStatus::invalid, kCheckpoint};
    }
    candidate.checkpoint_ = std::move(checkpoint.bytes);
  } else if (require_all &&
             checkpoint.status == platform::SaveStatus::missing) {
    // Earlier reconstruction builds saved these validated System/Record files
    // without the original empty checkpoint file. Preserve that progression:
    // materialize only the known empty sentinel, never repair nonzero data.
    const std::vector<std::byte> empty_checkpoint(kCheckpointBytes);
    const auto status = storage.write(kCheckpoint, empty_checkpoint);
    if (status != platform::SaveStatus::ok) return {status, kCheckpoint};
  } else if (checkpoint.status != platform::SaveStatus::missing) {
    return {checkpoint.status, kCheckpoint};
  }
  // Publish validated file contents without replacing the stable Profile
  // identity retained by menu/save capabilities. These owned transfers cannot
  // fail; a rejected load reaches none of them.
  system_ = candidate.system_;
  record_ = candidate.record_;
  checkpoint_ = std::move(candidate.checkpoint_);
  ++system_generation_;
  system_dirty_ = candidate.system_dirty_;
  record_dirty_ = candidate.record_dirty_;
  return {};
}
ProfileResult Profile::create_defaults(platform::SaveStorage& storage) {
  // Compatibility I.1/MEM.9: explicit startup operation; allocation is bounded
  // to the original save-buffer size and stays outside ordinary frame work.
  Profile candidate;
  const std::vector<std::byte> empty_checkpoint(kCheckpointBytes);
  auto status = storage.write(kSystem, candidate.system_);
  if (status != platform::SaveStatus::ok) return {status, kSystem};
  status = storage.write(kCheckpoint, empty_checkpoint);
  if (status != platform::SaveStatus::ok) return {status, kCheckpoint};
  status = storage.write(kRecord, candidate.record_);
  if (status != platform::SaveStatus::ok) return {status, kRecord};
  system_ = candidate.system_;
  record_ = candidate.record_;
  checkpoint_.clear();
  system_dirty_ = record_dirty_ = false;
  ++system_generation_;
  return {};
}
ProfileResult Profile::save(platform::SaveStorage& storage) {
  // Clear dirtiness per successful file replacement, not as a multi-file
  // commit.
  if (system_dirty_) {
    const auto status = storage.write(kSystem, system_);
    if (status != platform::SaveStatus::ok) {
      return {status, kSystem};
    }
    system_dirty_ = false;
    ++system_generation_;
  }
  if (record_dirty_) {
    const auto status = storage.write(kRecord, record_);
    if (status != platform::SaveStatus::ok) {
      return {status, kRecord};
    }
    record_dirty_ = false;
  }
  return {};
}
platform::SaveStatus RecordSaveControl::save() const {
  if (!profile_->record_dirty_) return platform::SaveStatus::ok;
  const auto result = storage_->write(kRecord, profile_->record_);
  if (result == platform::SaveStatus::ok) profile_->record_dirty_ = false;
  return result;
}
SettingsCheckpoint SettingsControl::checkpoint() const {
  return {profile_, profile_->system_, profile_->system_dirty_,
          profile_->system_generation_};
}
bool SettingsControl::changed_since(
    const SettingsCheckpoint& checkpoint) const {
  if (checkpoint.owner_ != profile_) {
    throw std::invalid_argument(
        "Settings checkpoint belongs to another profile");
  }
  return profile_->system_ != checkpoint.bytes_;
}
void SettingsControl::restore(const SettingsCheckpoint& checkpoint) const {
  (void)changed_since(checkpoint);
  profile_->system_ = checkpoint.bytes_;
  profile_->system_dirty_ = checkpoint.dirty_ || profile_->system_generation_ !=
                                                     checkpoint.generation_;
}
bool SettingsControl::subtitles_enabled() const {
  return profile_->subtitles_enabled();
}
bool SettingsControl::autosave_enabled() const {
  return profile_->autosave_enabled();
}
unsigned SettingsControl::music_volume() const {
  return profile_->music_volume();
}
unsigned SettingsControl::effects_volume() const {
  return profile_->effects_volume();
}
void SettingsControl::set_subtitles(bool enabled) const {
  constexpr std::size_t kFlags = 2;
  constexpr unsigned kMask = 1;
  const auto previous = read16(profile_->system_, kFlags);
  const auto updated = enabled ? previous | kMask : previous & ~kMask;
  if (previous == updated) {
    return;
  }
  write16(profile_->system_, kFlags, updated);
  profile_->system_dirty_ = true;
}
void SettingsControl::set_autosave(bool enabled) const {
  constexpr std::size_t kFlags = 2;
  constexpr unsigned kMask = 4;
  const auto previous = read16(profile_->system_, kFlags);
  const auto updated = enabled ? previous | kMask : previous & ~kMask;
  if (previous == updated) {
    return;
  }
  write16(profile_->system_, kFlags, updated);
  profile_->system_dirty_ = true;
}
void SettingsControl::set_music_volume(unsigned value) const {
  profile_->set_music_volume(value);
}
void SettingsControl::set_effects_volume(unsigned value) const {
  profile_->set_effects_volume(value);
}
ControllerSettings SettingsControl::controller_settings() const {
  return decode_controller_settings(profile_->system_);
}
void SettingsControl::set_controller_settings(
    const ControllerSettings& settings) const {
  auto candidate = profile_->system_;
  encode_controller_settings(candidate, settings);
  if (candidate == profile_->system_) {
    return;
  }
  profile_->system_ = candidate;
  profile_->system_dirty_ = true;
}
platform::SaveStatus SettingsSaveControl::save() const {
  return settings_.save(*storage_);
}
platform::SaveStatus SettingsControl::save(
    platform::SaveStorage& storage) const {
  if (!profile_->system_dirty_) {
    return platform::SaveStatus::ok;
  }
  const auto status = storage.write(kSystem, profile_->system_);
  if (status == platform::SaveStatus::ok) {
    profile_->system_dirty_ = false;
    ++profile_->system_generation_;
  }
  return status;
}
Progression UpgradePurchaseControl::progression() const {
  return profile_->progression();
}
// The original learned/read words are distinct. Viewing never unlocks a topic
// or changes mission progression; unused bits and all other bytes survive.
HelpProgress HelpControl::progress() const {
  constexpr std::size_t kLearnedOffset = 0x1c, kReadOffset = 0x24;
  constexpr unsigned kWordBits = 32;
  HelpProgress result;
  for (unsigned topic = 0; topic < help_topic_count; ++topic) {
    const auto offset = (topic / kWordBits) * sizeof(std::uint32_t);
    const auto mask = 1U << (topic % kWordBits);
    result.available.at(topic) =
        topic == 0 ||
        (read32(profile_->record_, kLearnedOffset + offset) & mask) != 0;
    result.read.at(topic) =
        (read32(profile_->record_, kReadOffset + offset) & mask) != 0;
  }
  return result;
}
void HelpControl::mark_read(unsigned topic) const {
  if (topic >= help_topic_count || !progress().available.at(topic)) {
    throw std::invalid_argument("Help topic is unavailable");
  }
  constexpr std::size_t kReadOffset = 0x24;
  constexpr unsigned kWordBits = 32;
  const auto offset = kReadOffset + (topic / kWordBits) * sizeof(std::uint32_t);
  const auto previous = read32(profile_->record_, offset);
  const auto updated = previous | (1U << (topic % kWordBits));
  if (updated != previous) {
    write32(profile_->record_, offset, updated);
    profile_->record_dirty_ = true;
  }
}
PurchaseResult UpgradePurchaseControl::purchase(
    UpgradeSelection selection) const {
  return profile_->purchase(selection);
}
Progression Profile::progression() const {
  return decode_record_progression(record_);
}
PurchaseResult Profile::purchase(UpgradeSelection selection) {
  auto candidate = progression();
  const auto result = purchase_upgrade(candidate, selection);
  if (result != PurchaseResult::purchased) {
    return result;
  }
  // First spell acquisition opens its original tutorial/book bit. Complete
  // validation precedes the only writes; there are no fallible operations
  // after.
  const bool acquired_spell = selection.category == UpgradeCategory::spell &&
                              candidate.levels.spells.at(selection.index) == 1;
  encode_record_progression(record_, candidate);
  if (acquired_spell) {
    mark_spell_acquired(record_, selection.index);
  }
  record_dirty_ = true;
  return result;
}
std::uint32_t Profile::award_result(MissionResultReward result) {
  const auto awarded = mission_reward_points(result);
  auto candidate = progression();
  const auto credited =
      std::min(awarded, maximum_upgrade_points - candidate.points);
  if (credited != 0) {
    candidate.points += credited;
    encode_record_progression(record_, candidate);
    record_dirty_ = true;
  }
  return credited;
}
std::optional<CheckpointPlayer> Profile::checkpoint_player() const {
  return has_checkpoint() ? decode_checkpoint_player(checkpoint_)
                          : std::nullopt;
}
unsigned Profile::current_difficulty() const {
  return std::to_integer<unsigned>(record_[0]);
}
void Profile::set_current_difficulty(unsigned value) {
  if (value > 4) {
    throw std::invalid_argument("difficulty outside recovered range");
  }
  record_[0] = static_cast<std::byte>(value);
  record_dirty_ = true;
}
void Profile::refresh_installed_content() {
  // 0x4c31b0 writes availability value 2 (the readers accept nonzero).
  constexpr unsigned installed_costumes = 5;
  for (unsigned index = 1; index <= installed_costumes; ++index) {
    record_.at(kCostumeFlagsOffset + index) = std::byte{2};
  }
  // Original 64-byte clearing starts at extra index 1 and includes costume 0.
  for (unsigned index = 1; index <= kExtraCount; ++index) {
    record_.at(kExtraFlagsOffset + index) = std::byte{};
  }
  constexpr std::array<unsigned, 3> groups{0, 16, 32};
  for (unsigned group : groups) {
    for (unsigned index = 0; index < kStageCount; ++index) {
      record_.at(kExtraFlagsOffset + group + index) = std::byte{2};
    }
  }
  record_dirty_ = true;
}
bool Profile::costume_available(unsigned value) const {
  return value == 0 || (value < kCostumeCount &&
                        record_.at(kCostumeFlagsOffset + value) != std::byte{});
}
bool Profile::extra_available(unsigned value) const {
  return value < kExtraCount &&
         record_.at(kExtraFlagsOffset + value) != std::byte{};
}
unsigned Profile::difficulty() const { return read16(system_, 0); }
void Profile::set_difficulty(unsigned value) {
  if (value > 4) {
    throw std::invalid_argument("difficulty outside recovered range");
  }
  write16(system_, 0, value);
  system_dirty_ = true;
}
bool Profile::subtitles_enabled() const {
  constexpr std::size_t kFlagsOffset = 2;
  constexpr unsigned kSubtitleMask = 1;
  return (read16(system_, kFlagsOffset) & kSubtitleMask) != 0;
}
bool Profile::autosave_enabled() const {
  // Native getter shifts the same System flag word by two; bit one is not
  // autosave. Keep this observation separate from the storage service owner.
  constexpr std::size_t kFlagsOffset = 2;
  constexpr unsigned kAutosaveMask = 4;
  return (read16(system_, kFlagsOffset) & kAutosaveMask) != 0;
}
unsigned Profile::music_volume() const { return read32(system_, 4); }
unsigned Profile::effects_volume() const {
  return read32(system_, kEffectsVolumeOffset);
}
void Profile::set_music_volume(unsigned value) {
  if (value > kFullVolume) {
    throw std::invalid_argument("music volume outside recovered range");
  }
  write32(system_, 4, value);
  system_dirty_ = true;
}
void Profile::set_effects_volume(unsigned value) {
  if (value > kFullVolume) {
    throw std::invalid_argument("effects volume outside recovered range");
  }
  write32(system_, kEffectsVolumeOffset, value);
  system_dirty_ = true;
}
unsigned Profile::costume() const {
  return std::to_integer<unsigned>(record_[1]);
}
void Profile::set_costume(unsigned value) {
  if (value > kMaximumCostume) {
    throw std::invalid_argument("costume outside recovered range");
  }
  record_[1] = static_cast<std::byte>(value);
  record_dirty_ = true;
}
bool Profile::has_checkpoint() const noexcept { return !checkpoint_.empty(); }
unsigned Profile::checkpoint_stage() const {
  return has_checkpoint()
             ? std::to_integer<unsigned>(
                   checkpoint_[read32(checkpoint_, kBaseOffsetField)])
             : 0;
}
unsigned Profile::checkpoint_difficulty() const {
  return has_checkpoint()
             ? std::to_integer<unsigned>(
                   checkpoint_[read32(checkpoint_, kBaseOffsetField) + 2])
             : 0;
}
std::span<const std::byte> Profile::checkpoint() const noexcept {
  return checkpoint_;
}
std::span<const std::byte> Profile::system_bytes() const noexcept {
  return system_;
}
std::span<const std::byte> Profile::record_bytes() const noexcept {
  return record_;
}
bool Profile::dirty() const noexcept { return system_dirty_ || record_dirty_; }
}  // namespace bw::game
