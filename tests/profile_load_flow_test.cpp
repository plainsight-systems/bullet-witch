// Source startup storage and dialog contracts using in-memory file results.
// Check admission, original empty-checkpoint encoding, legacy preservation,
// and failure publication through public capabilities. No files/native UI.
// I.1 (#ri-explicit) exposes outcomes; MEM.9 (guidelines/memory/
// MEM.9-embedded-allocate-at-init.md) bounds production state; fixture buffers
// are setup-only. This does not claim filesystem multi-file atomicity.
#include "game/profile_load_flow.hpp"

#include <algorithm>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

#include "game/pause_surface_text.hpp"

namespace {
using bw::game::ProfileLoadDialog;
using bw::platform::SaveStatus;
constexpr std::size_t kEmptyCheckpointBytes = 307200;
void require(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}
class Storage final : public bw::platform::SaveStorage {
 public:
  bw::platform::SaveRead read(std::string_view name, std::size_t) override {
    if (read_failure == name) return {SaveStatus::io_error, {}};
    const auto found = files.find(std::string(name));
    return found == files.end()
               ? bw::platform::SaveRead{SaveStatus::missing, {}}
               : bw::platform::SaveRead{SaveStatus::ok, found->second};
  }
  SaveStatus write(std::string_view name,
                   std::span<const std::byte> bytes) override {
    writes.emplace_back(name);
    if (write_failure == name) return SaveStatus::io_error;
    files[std::string(name)] = {bytes.begin(), bytes.end()};
    return SaveStatus::ok;
  }
  std::map<std::string, std::vector<std::byte>> files;
  std::vector<std::string> writes;
  std::string read_failure, write_failure;
};
class Media final : public bw::game::LogoMedia {
 public:
  bool open_movie(std::string_view) override { return false; }
  bool movie_complete() const override { return false; }
  void stop_movie() noexcept override {}
  void begin_fade(bw::game::FadeDirection, float) override {}
  bool fade_pending() const override { return fading; }
  bool fading{};
};
void reach(bw::game::ProfileLoadFlow& flow, ProfileLoadDialog identity) {
  for (unsigned frame = 0; frame < 100; ++frame) {
    if (flow.presentation().identity == identity) return;
    flow.update({.1F, 0}, true);
  }
  throw std::runtime_error("Profile dialog not reached");
}
void acknowledge(bw::game::ProfileLoadFlow& flow, bool yes = true) {
  flow.update({.4F, 0}, true);
  if (!yes) flow.update({0, 0x2000}, true);
  flow.update({0, 0x1000}, true);
  flow.update({.4F, 0}, true);
}
void complete(bw::game::ProfileLoadFlow& flow) {
  for (unsigned frame = 0; frame < 20; ++frame) flow.update({.1F, 0}, true);
  require(flow.progress().status == bw::game::FlowStatus::complete,
          "Profile load completes only after lifecycle gates");
}
void original_empty_content() {
  Storage storage;
  Media media;
  bw::game::Profile profile;
  bw::game::ProfileLoadFlow flow(profile, storage, media);
  flow.start();
  flow.update({100, 0x1010}, false);
  require(!flow.progress().visible && storage.writes.empty(),
          "Resource admission prevents premature storage writes");
  reach(flow, ProfileLoadDialog::created);
  require(storage.writes == std::vector<std::string>{"BulletWitchSystem",
                                                     "BulletWitchSaveData",
                                                     "BulletWitchRecord"},
          "Original fresh-content write order is preserved");
  const auto& empty = storage.files.at("BulletWitchSaveData");
  require(empty.size() == kEmptyCheckpointBytes &&
              std::ranges::all_of(
                  empty, [](std::byte b) { return b == std::byte{}; }) &&
              !profile.has_checkpoint(),
          "Stored zero sentinel does not fabricate a world checkpoint");
  acknowledge(flow);
  require(flow.presentation().identity == ProfileLoadDialog::enable_autosave &&
              flow.presentation().dialog.yes,
          "Fresh original settings ask to enable autosave, default Yes");
  media.fading = true;
  acknowledge(flow);
  require(profile.autosave_enabled(), "Affirmative autosave choice is applied");
  for (unsigned i = 0; i < 5; ++i) flow.update({}, true);
  require(flow.progress().status == bw::game::FlowStatus::pending,
          "Creation acknowledgment still waits for conceal completion");
  media.fading = false;
  complete(flow);
  bw::game::Profile loaded;
  require(
      loaded.load(storage).status == SaveStatus::ok && !loaded.has_checkpoint(),
      "Fresh content round trips through normal profile loading");
  storage.files.at("BulletWitchSaveData").back() = std::byte{1};
  require(loaded.load(storage).status == SaveStatus::invalid,
          "A nonzero malformed checkpoint is never accepted as empty");
  storage.files.at("BulletWitchSaveData") = {std::byte{}};
  require(loaded.load(storage).status == SaveStatus::invalid,
          "Wrong-sized all-zero checkpoint is invalid");
}
void overwrite_requires_original_decision() {
  Storage storage;
  Media media;
  bw::game::Profile profile;
  require(profile.create_defaults(storage).status == SaveStatus::ok,
          "Fixture creates original content");
  storage.files.erase("BulletWitchRecord");
  storage.writes.clear();
  profile.set_costume(4);
  bw::game::ProfileLoadFlow flow(profile, storage, media);
  flow.start();
  reach(flow, ProfileLoadDialog::load_failed);
  require(storage.writes.empty() && profile.costume() == 4,
          "Missing record fails without publishing defaults or writing");
  acknowledge(flow);
  require(flow.presentation().identity == ProfileLoadDialog::overwrite &&
              flow.presentation().dialog.yes,
          "Original overwrite prompt defaults to Yes");
  acknowledge(flow, false);
  reach(flow, ProfileLoadDialog::load_failed);
  require(storage.writes.empty() && profile.costume() == 4 &&
              flow.progress().status == bw::game::FlowStatus::pending,
          "No retries lookup while preserving data; it cannot fake success");
  acknowledge(flow);
  acknowledge(flow);
  reach(flow, ProfileLoadDialog::created);
  require(profile.costume() == 0 && !profile.dirty(),
          "Affirmative replacement publishes defaults after successful writes");
  acknowledge(flow);
  require(flow.presentation().identity == ProfileLoadDialog::enable_autosave,
          "Replacement retains original autosave opt-in");
  acknowledge(flow, false);
  require(!profile.autosave_enabled(), "No leaves original autosave disabled");
  complete(flow);
}
void legacy_migration_preserves_progress() {
  Storage storage;
  bw::game::Profile profile;
  require(profile.load(storage).status == SaveStatus::ok,
          "Fresh compatibility load");
  profile.set_costume(4);
  profile.set_music_volume(101);
  require(profile.save(storage).status == SaveStatus::ok,
          "Legacy two-file fixture");
  const auto system = storage.files.at("BulletWitchSystem");
  const auto record = storage.files.at("BulletWitchRecord");
  storage.writes.clear();
  bw::game::Profile loaded;
  loaded.set_costume(2);
  storage.write_failure = "BulletWitchSaveData";
  require(loaded.load_startup(storage).status == SaveStatus::io_error &&
              loaded.costume() == 2,
          "Failed migration leaves active authority unchanged");
  storage.write_failure.clear();
  require(loaded.load_startup(storage).status == SaveStatus::ok &&
              loaded.costume() == 4 && loaded.music_volume() == 101 &&
              !loaded.has_checkpoint() &&
              storage.files.at("BulletWitchSystem") == system &&
              storage.files.at("BulletWitchRecord") == record,
          "Legacy migration preserves every validated System/Record byte");
  require(std::ranges::all_of(
              storage.writes,
              [](const auto& name) { return name == "BulletWitchSaveData"; }),
          "Migration writes only the absent empty checkpoint");
  storage.writes.clear();
  require(loaded.load_startup(storage).status == SaveStatus::ok &&
              storage.writes.empty(),
          "Migration is one-time; subsequent valid loading performs no writes");
}
void failed_creation_never_publishes() {
  Storage storage;
  bw::game::Profile profile;
  profile.set_costume(3);
  profile.set_music_volume(90);
  storage.write_failure = "BulletWitchRecord";
  const auto result = profile.create_defaults(storage);
  require(result.status == SaveStatus::io_error &&
              result.file == "BulletWitchRecord" && profile.costume() == 3 &&
              profile.music_volume() == 90 && profile.dirty(),
          "Partial disk replacement cannot publish defaults or clear active "
          "dirtiness");
  Media media;
  bw::game::ProfileLoadFlow flow(profile, storage, media);
  flow.start();
  reach(flow, ProfileLoadDialog::load_failed);
  acknowledge(flow);
  acknowledge(flow);
  reach(flow, ProfileLoadDialog::creating);
  reach(flow, ProfileLoadDialog::load_failed);
  require(profile.costume() == 3 &&
              flow.progress().status == bw::game::FlowStatus::pending,
          "Failed creation returns through original lookup, not success "
          "acknowledgment");
}
}  // namespace
void profile_load_flow_contracts() {
  std::array<char, 32> original_text{};
  bw::game::pause_text::prepare_text("ｄｏｎ’ｔ", original_text);
  require(std::string_view(original_text.data()) == "don't",
          "Original English storage apostrophe uses existing bitmap glyph");
  original_empty_content();
  overwrite_requires_original_decision();
  legacy_migration_preserves_progress();
  failed_creation_never_publishes();
}
