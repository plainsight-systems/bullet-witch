// Profile format implementation. Validate all bounded file inputs before
// publishing a candidate; retain unknown bytes and change only mapped fields.
// Original defaults: 0x4bb800/0x4bb900, persistence 0x4c2eb0/0x4c3050.
// Save checkpoint bytes are read-only here; world serialization is not implied.
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
    std::ranges::copy(record.bytes, candidate.record_.begin());
  } else if (record.status == platform::SaveStatus::missing) {
    candidate.record_dirty_ = true;
  } else {
    return {record.status, kRecord};
  }
  auto checkpoint = storage.read(kCheckpoint, kCheckpointBytes);
  if (checkpoint.status == platform::SaveStatus::ok) {
    if (!valid_checkpoint(checkpoint.bytes)) {
      return {platform::SaveStatus::invalid, kCheckpoint};
    }
    candidate.checkpoint_ = std::move(checkpoint.bytes);
  } else if (checkpoint.status != platform::SaveStatus::missing) {
    return {checkpoint.status, kCheckpoint};
  }
  *this = std::move(candidate);
  return {};
}
ProfileResult Profile::save(platform::SaveStorage& storage) {
  if (system_dirty_) {
    const auto status = storage.write(kSystem, system_);
    if (status != platform::SaveStatus::ok) {
      return {status, kSystem};
    }
    system_dirty_ = false;
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
