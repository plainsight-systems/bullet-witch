// Public profile compatibility tests use controlled in-memory storage. No disk,
// subprocess, or game assets. Check atomic publication, original bytes, bounded
// fields and retry after partial write. Cost fixed small buffers plus one
// bounded checkpoint. Design/implementation: cpp-guidelines R.1 (#rr-raii) owns
// fixtures; cpp-performance COPY.4
// (guidelines/copy-move/COPY.4-rule-of-five-rule-of-zero.md) retains container
// owners. Does not establish native filesystem durability.
#include "game/profile.hpp"

#include <algorithm>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

void profile_load_flow_contracts();

namespace {
void require(bool condition, const char* message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}
class MemoryStorage final : public bw::platform::SaveStorage {
 public:
  bw::platform::SaveRead read(std::string_view name, std::size_t) override {
    const auto found = files.find(std::string(name));
    if (found == files.end()) {
      return {bw::platform::SaveStatus::missing, {}};
    }
    return {bw::platform::SaveStatus::ok, found->second};
  }
  bw::platform::SaveStatus write(std::string_view name,
                                 std::span<const std::byte> data) override {
    if (name == failure) {
      return bw::platform::SaveStatus::io_error;
    }
    files[std::string(name)] = {data.begin(), data.end()};
    return bw::platform::SaveStatus::ok;
  }
  std::map<std::string, std::vector<std::byte>> files;
  std::string failure;
};
void defaults_and_round_trip() {
  MemoryStorage storage;
  bw::game::Profile profile;
  require(profile.load(storage).status == bw::platform::SaveStatus::ok &&
              profile.dirty(),
          "missing files publish fresh defaults without writing");
  require(storage.files.empty() && !profile.has_checkpoint() &&
              profile.difficulty() == 1 && profile.music_volume() == 256 &&
              profile.effects_volume() == 256,
          "recovered system defaults");
  profile.set_current_difficulty(2);
  profile.set_costume(4);
  profile.set_music_volume(128);
  require(profile.save(storage).status == bw::platform::SaveStatus::ok &&
              !profile.dirty(),
          "save writes original sizes");
  require(storage.files.at("BulletWitchSystem").size() == 72 &&
              storage.files.at("BulletWitchRecord").size() == 992 &&
              storage.files.size() == 2,
          "checkpoint never synthesized");
  storage.files.at("BulletWitchRecord")[900] = std::byte{0xab};
  bw::game::Profile loaded;
  require(loaded.load(storage).status == bw::platform::SaveStatus::ok &&
              loaded.costume() == 4 && loaded.current_difficulty() == 2 &&
              loaded.difficulty() == 1,
          "live difficulty distinct from settings preference");
  loaded.set_costume(2);
  require(loaded.save(storage).status == bw::platform::SaveStatus::ok &&
              storage.files.at("BulletWitchRecord")[900] == std::byte{0xab},
          "opaque bytes survive modification");
}
void checkpoint_owned_bytes() {
  MemoryStorage storage;
  auto& bytes = storage.files["BulletWitchSaveData"];
  bytes.resize(307200);
  auto word = [&bytes](std::size_t offset, unsigned value) {
    for (unsigned index = 0; index < 4; ++index) {
      bytes.at(offset + index) =
          static_cast<std::byte>((value >> (index * 8U)) & 255U);
    }
  };
  word(0, 0x10001);
  word(4, 0x8e0);
  word(8, 0x500);
  word(12, 0x30);
  word(16, 0x3a4);
  word(20, 0x530);
  bytes[0x30] = std::byte{4};
  bytes[0x32] = std::byte{3};
  bytes.back() = std::byte{0xab};
  bw::game::Profile profile;
  require(profile.load(storage).status == bw::platform::SaveStatus::ok &&
              profile.has_checkpoint() && profile.checkpoint_stage() == 4 &&
              profile.checkpoint_difficulty() == 3 &&
              std::ranges::equal(profile.checkpoint(), bytes),
          "valid checkpoint publishes exact owned bytes and handoff fields");
  bytes.back() = std::byte{0};
  require(profile.checkpoint().back() == std::byte{0xab},
          "checkpoint owns bytes independently of storage");
  word(20, 0x100);
  require(profile.load(storage).status == bw::platform::SaveStatus::invalid &&
              profile.checkpoint_stage() == 4,
          "overlapping sections cannot replace accepted checkpoint");
  word(20, 0xfffffff0U);
  require(profile.load(storage).status == bw::platform::SaveStatus::invalid &&
              profile.checkpoint_difficulty() == 3,
          "out-of-bounds offsets cannot publish");
}
void installed_content_refresh() {
  MemoryStorage storage;
  bw::game::Profile profile;
  require(profile.load(storage).status == bw::platform::SaveStatus::ok &&
              profile.save(storage).status == bw::platform::SaveStatus::ok,
          "create controlled record");
  auto& bytes = storage.files.at("BulletWitchRecord");
  bytes[0x3c + 12] = std::byte{1};
  bytes[0x7c + 8] = std::byte{1};
  require(profile.load(storage).status == bw::platform::SaveStatus::ok,
          "load controlled availability");
  profile.refresh_installed_content();
  require(profile.costume_available(0) && profile.costume_available(1) &&
              profile.costume_available(5) && !profile.costume_available(6) &&
              profile.costume_available(8) && !profile.extra_available(12) &&
              profile.extra_available(0) && profile.extra_available(21) &&
              profile.extra_available(37) && !profile.extra_available(38),
          "PC refresh preserves exact ranges and unrelated costume flags");
  require(profile.save(storage).status == bw::platform::SaveStatus::ok &&
              storage.files.at("BulletWitchRecord")[0x7d] == std::byte{2},
          "refresh persists original availability value");
}
void failures_preserve_state() {
  MemoryStorage storage;
  bw::game::Profile profile;
  profile.set_costume(3);
  storage.files["BulletWitchRecord"] = {std::byte{1}};
  require(profile.load(storage).status == bw::platform::SaveStatus::invalid &&
              profile.costume() == 3,
          "short record cannot partially publish settings");
  storage.files.clear();
  require(profile.load(storage).status == bw::platform::SaveStatus::ok,
          "retry load");
  storage.failure = "BulletWitchRecord";
  require(profile.save(storage).status == bw::platform::SaveStatus::io_error &&
              profile.dirty(),
          "failed file retains dirty state");
  storage.failure.clear();
  require(profile.save(storage).status == bw::platform::SaveStatus::ok &&
              !profile.dirty(),
          "write retry completes");
  storage.files["BulletWitchSaveData"] = std::vector<std::byte>(307200);
  require(profile.load(storage).status == bw::platform::SaveStatus::invalid &&
              !profile.has_checkpoint(),
          "invalid checkpoint header rejected");
}
}  // namespace
int main() {
  try {
    defaults_and_round_trip();
    failures_preserve_state();
    installed_content_refresh();
    checkpoint_owned_bytes();
    profile_load_flow_contracts();
  } catch (const std::exception&) {
    return 1;
  }
}
