// Mission music capacity follows the measured segmented-track corpus (largest
// 12,125,184 interleaved samples), with a fixed 16M-sample admission ceiling.
// Core SL.con.3 and performance MEM.9 were read for design/implementation:
// reject oversized decode before allocation; retain one prepared track owner.
// Acquire common menu publications at session setup, mission publications at
// mission loading and music at main entry, then
// release encoded acquisition storage after the backend and prepared policy own
// their required state. Header documents borrowing/progress/rollback. Core
// SL.con.3 (#rsl-bounds) validates archive names before exposing payloads;
// Performance MEM.9 (guidelines/memory/MEM.9-embedded-allocate-at-init.md)
// confines metadata preparation and sample residency to setup. Both
// implementation corpora consulted; source mapping is in game-audio.md.
#include "game_audio.hpp"

#include <algorithm>
#include <array>

#include "bw/audio/music_decode.hpp"
#include "bw/content/formats/binary_reader.hpp"
namespace bw::game {
namespace {
resources::ResourceArchive load(const platform::Files& files,
                                const resources::CatalogSet& catalogs,
                                std::string_view name,
                                const std::shared_ptr<memory::Domain>& domain) {
  const auto selected = catalogs.resolve(name);
  const auto& mount = catalogs.catalogs().at(selected.catalog_index);
  const auto& resource = mount.catalog.resources().at(selected.resource_index);
  return content::open_archive(
      files, content::archive_path(files, mount, resource.raw.at(3)), domain);
}
std::span<const std::uint8_t> payload(const resources::ResourceArchive& archive,
                                      std::string_view name) {
  std::optional<std::size_t> selected;
  for (std::size_t i = 0; i < archive.records().size(); ++i) {
    const auto& record = archive.records().at(i);
    if (record.type != "USER" || record.name != name) continue;
    if (selected) resources::detail::malformed("Duplicate audio payload name");
    selected = i;
  }
  if (!selected) {
    resources::detail::malformed("Required audio payload is absent");
  }
  return archive.payload(*selected);
}
constexpr std::uint16_t kCommonBank = 0, kVoiceBank = 4;
}  // namespace
GameAudio::GameAudio(const platform::Files& files,
                     const resources::CatalogSet& catalogs,
                     const std::shared_ptr<memory::Domain>& domain,
                     GameAudioFactory& factory, audio::Limits limits,
                     const content::LoadProgress& progress)
    : character_profiles_(
          memory::Allocator<std::shared_ptr<const PreparedCharacterAudio>>(
              domain)) {
  {
    const auto settings =
        load(files, catalogs, "audio.xgs.BulletWitch", domain);
    output_ =
        factory.create(payload(settings, "BulletWitch.xgs"), limits, domain);
    if (!output_) {
      resources::detail::malformed("Game audio factory returned no output");
    }
  }
  if (progress) progress();
  {
    const auto common =
        load(files, catalogs, "audio.soundbank.bw_common", domain);
    constexpr std::array<std::uint16_t, 13> kMenuCues{
        0, 1, 2, 4, 5, 6, 8, 10, 11, 12, 17, 362, 363};
    output_->register_bank(kCommonBank, payload(common, "BW_COMMON.csb"),
                           payload(common, "BW_COMMON.xwb"), kMenuCues);
  }
  if (progress) progress();
  output_->seal();
}
void GameAudio::prepare_mission(const platform::Files& files,
                                const resources::CatalogSet& catalogs,
                                const std::filesystem::path& sound_directory,
                                const std::shared_ptr<memory::Domain>& domain,
                                const content::LoadProgress& progress) {
  if (prepared_) return;
  if (mission_preparation_started_) {
    throw std::logic_error("Mission audio preparation did not complete");
  }
  mission_preparation_started_ = true;
  std::optional<PreparedCharacterAudio> candidate;
  output_->extend_bank(kCommonBank, {});
  if (progress) progress();
  {
    const auto voice = load(files, catalogs, "audio.soundbank.voice", domain);
    const auto sound = payload(voice, "VOICE.csb");
    const auto waves = payload(voice, "VOICE.xwb");
    const auto definition =
        formats::AudioBankDefinition::parse({sound, waves}, domain);
    const auto streamed =
        load(files, catalogs, "audio.soundbank.game_voice", domain);
    const auto streamed_sound = payload(streamed, "GAME_VOICE.csb");
    const auto base = files.canonical(sound_directory);
    const auto path = files.canonical(base / "stream_GAME_VOICE.xwb");
    if (path.parent_path() != base) {
      resources::detail::malformed(
          "External voice bank escapes sound directory");
    }
    const auto streamed_waves = files.read(path, domain);
    const auto streamed_definition = formats::AudioBankDefinition::parse(
        {streamed_sound, streamed_waves}, domain);
    const auto talk = load(files, catalogs, "audio.talkdata", domain);
    candidate.emplace(payload(talk, "voice_weight.bin"),
                      formats::TextTable::parse(payload(talk, "alicia.bin")),
                      definition, streamed_definition);
    output_->register_bank(kVoiceBank, sound, waves);
    constexpr std::uint16_t kStreamedVoiceBank = 50;
    memory::Vector<std::uint16_t> admitted{
        memory::Allocator<std::uint16_t>(domain)};
    for (std::uint32_t action = 0; action < kVoiceActions; ++action) {
      const auto* voice_action = candidate->voice(action);
      if (voice_action == nullptr) continue;
      for (const auto& variant : voice_action->variants) {
        if (variant.available && variant.bank == kStreamedVoiceBank) {
          admitted.push_back(variant.cue);
        }
      }
    }
    if (admitted.empty()) {
      resources::detail::malformed("Alicia streaming voice closure is empty");
    }
    output_->register_bank(kStreamedVoiceBank, streamed_sound, streamed_waves,
                           admitted);
  }
  if (progress) progress();
  output_->seal();
  prepared_ = candidate;
}
const PreparedCharacterAudio& GameAudio::prepared(
    CharacterAudioProfile profile) const {
  if (prepared_ && prepared_->profile() == profile) return *prepared_;
  for (const auto& candidate : character_profiles_) {
    if (candidate->profile() == profile) return *candidate;
  }
  throw std::logic_error("Actor audio profile is unprepared");
}
void GameAudio::prepare_character_profiles(
    const platform::Files& files, const resources::CatalogSet& catalogs,
    const std::filesystem::path& sound_directory,
    std::span<const CharacterAudioProfile> profiles,
    const std::shared_ptr<memory::Domain>& domain) {
  if (!prepared_) {
    throw std::logic_error("Prepare mission banks before actor profiles");
  }
  memory::Vector<CharacterAudioProfile> pending{
      memory::Allocator<CharacterAudioProfile>(domain)};
  for (const auto profile : profiles) {
    if (prepared_->profile() == profile ||
        std::ranges::any_of(
            character_profiles_,
            [&](const auto& item) { return item->profile() == profile; }) ||
        std::ranges::find(pending, profile) != pending.end()) {
      continue;
    }
    pending.push_back(profile);
  }
  if (pending.empty()) return;
  const auto voice = load(files, catalogs, "audio.soundbank.voice", domain);
  const auto voice_definition = formats::AudioBankDefinition::parse(
      {payload(voice, "VOICE.csb"), payload(voice, "VOICE.xwb")}, domain);
  const auto streamed =
      load(files, catalogs, "audio.soundbank.game_voice", domain);
  const auto base = files.canonical(sound_directory);
  const auto path = files.canonical(base / "stream_GAME_VOICE.xwb");
  if (path.parent_path() != base) {
    resources::detail::malformed("External voice bank escapes sound directory");
  }
  const auto waves = files.read(path, domain);
  const auto streamed_definition = formats::AudioBankDefinition::parse(
      {payload(streamed, "GAME_VOICE.csb"), waves}, domain);
  const auto talk = load(files, catalogs, "audio.talkdata", domain);
  const auto weights = payload(talk, "voice_weight.bin");
  memory::Vector<std::shared_ptr<const PreparedCharacterAudio>> candidates{
      memory::Allocator<std::shared_ptr<const PreparedCharacterAudio>>(domain)};
  memory::Vector<std::uint16_t> cues{memory::Allocator<std::uint16_t>(domain)};
  constexpr std::uint16_t kStreamedVoiceBank = 50;
  for (const auto profile : pending) {
    auto policy = profile.voice_group == 0
                      ? PreparedCharacterAudio::silent(profile)
                      : PreparedCharacterAudio(
                            weights,
                            formats::TextTable::parse(payload(
                                talk, std::string(character_voice_group_name(
                                          profile.voice_group)) +
                                          ".bin")),
                            voice_definition, streamed_definition, profile);
    for (std::uint32_t action = 0; action < kVoiceActions; ++action) {
      const auto* definition = policy.voice(action);
      if (!definition) continue;
      for (const auto& variant : definition->variants) {
        if (variant.available && variant.bank == kStreamedVoiceBank &&
            std::ranges::find(cues, variant.cue) == cues.end()) {
          cues.push_back(variant.cue);
        }
      }
    }
    candidates.push_back(std::allocate_shared<const PreparedCharacterAudio>(
        memory::Allocator<PreparedCharacterAudio>(domain), policy));
  }
  character_profiles_.reserve(character_profiles_.size() + candidates.size());
  if (!cues.empty()) output_->extend_bank(kStreamedVoiceBank, cues);
  output_->seal();
  for (auto& candidate : candidates) {
    character_profiles_.push_back(std::move(candidate));
  }
}
void GameAudio::prepare_frontend_music(
    const platform::Files& files, const std::filesystem::path& sound_directory,
    const std::shared_ptr<memory::Domain>& domain) {
  constexpr std::uint32_t kFrontendMusic = 16;
  prepare_script_music(kFrontendMusic, files, sound_directory, domain);
}
void GameAudio::prepare_script_music(
    std::uint32_t index, const platform::Files& files,
    const std::filesystem::path& sound_directory,
    const std::shared_ptr<memory::Domain>& domain) {
  // Original script IDs 1..16 map through 82f1a8 then filename table956b68.
  // This verified range is explicit; untraced indices are not inferred from
  // neighboring data. Source startup7 selects bgm_07, checkpoint3 bgm_03.
  constexpr std::array<std::string_view, 16> kTracks{
      "bgm_01.xwv",  "bgm_02.xwv", "bgm_03.xwv",  "bgm_04.xwv",
      "bgm_05a.xwv", "bgm_06.xwv", "bgm_07.xwv",  "bgm_08b.xwv",
      "bgm_09.xwv",  "bgm_10.xwv", "bgm_11c.xwv", "bgm_12.xwv",
      "bgm_13.xwv",  "bgm_14.xwv", "bgm_15.xwv",  "bgm_16.xwv"};
  if (index == 0 || index > kTracks.size()) {
    throw std::out_of_range("Unverified original script music index");
  }
  if (prepared_music_ == index) return;
  const auto base = files.canonical(sound_directory);
  const auto path = files.canonical(base / kTracks.at(index - 1));
  if (path.parent_path() != base) {
    resources::detail::malformed("Music escapes sound directory");
  }
  const auto encoded = files.read(path, domain);
  // Segmented-track metadata census bounds the largest original track below
  // 16M samples (64 MB of float PCM); CP610 music3 uses 11,781,120. This
  // admission ceiling is independent of the shared 60M cue-mixer budget.
  constexpr std::size_t kMaximumMusicSamples = 16'000'000;
  output_->music().prepare_music(
      audio::decode_segmented_music(encoded, kMaximumMusicSamples, domain));
  prepared_music_ = index;
}
// Device service continues during world pause; actor admission does not own it.
void GameAudio::update() { output_->update(); }
void GameAudio::retire_mission() {
  // Source stop-all clears requests, not global variant history.
  if (voices_.stop_all() == CharacterAudioResult::backend_failure) {
    throw std::runtime_error("Mission character voices could not stop");
  }
  output_->retire_world();
}
void GameAudio::apply_mix(AudioMixLevels levels) {
  constexpr auto kFullVolume = AudioMixLevels::kFullVolume;
  if (levels.music > kFullVolume || levels.effects > kFullVolume) {
    throw std::invalid_argument("Invalid game audio mix level");
  }
  const float music = static_cast<float>(levels.music) / kFullVolume;
  const float effects = static_cast<float>(levels.effects) / kFullVolume;
  output_->set_mix_gains({music, effects, effects});
}
const PreparedCharacterAudio& CharacterAudioLookup::prepared(
    CharacterAudioProfile profile) const {
  return owner_->prepared(profile);
}
}  // namespace bw::game
