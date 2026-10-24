// Acquire original resource publications at the mission loading boundary, then
// release encoded acquisition storage after the backend and prepared policy own
// their required state. Header documents borrowing/progress/rollback. Core
// SL.con.3 (#rsl-bounds) validates archive names before exposing payloads;
// Performance MEM.9 (guidelines/memory/MEM.9-embedded-allocate-at-init.md)
// confines metadata preparation and sample residency to setup. Both
// implementation corpora consulted; source mapping is in game-audio.md.
#include "game/mission_audio.hpp"

#include "formats/reader.hpp"
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
MissionAudio::MissionAudio(const platform::Files& files,
                           const resources::CatalogSet& catalogs,
                           const std::filesystem::path& sound_directory,
                           const std::shared_ptr<memory::Domain>& domain,
                           GameAudioFactory& factory, audio::Limits limits,
                           const content::LoadProgress& progress) {
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
    output_->register_bank(kCommonBank, payload(common, "BW_COMMON.csb"),
                           payload(common, "BW_COMMON.xwb"));
  }
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
    prepared_.emplace(payload(talk, "voice_weight.bin"),
                      formats::TextTable::parse(payload(talk, "alicia.bin")),
                      definition, streamed_definition);
    output_->register_bank(kVoiceBank, sound, waves);
    constexpr std::uint16_t kStreamedVoiceBank = 50;
    memory::Vector<std::uint16_t> admitted{
        memory::Allocator<std::uint16_t>(domain)};
    for (std::uint32_t action = 0; action < kVoiceActions; ++action) {
      const auto* voice_action = prepared().voice(action);
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
}
}  // namespace bw::game
