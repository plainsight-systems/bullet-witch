// Session-owned audio preparation, with native output chosen only by the
// target executable. Resolve stable catalogs once, validate Alicia metadata,
// register the bounded common menu closure, then publish it. Mission
// preparation expands common and voice banks once at the loading boundary.
// Files/catalogs/factory/progress are borrowed only during each preparation;
// output copies its banks and the prepared policy retains cue values, not file
// spans. Failure destroys the unpublished output, including worker/device
// teardown. Actor audio owners borrow this policy and retain output only while
// this session owner survives. One loading/control thread; progress may pump
// loading presentation only at complete-asset boundaries and must not reenter
// this owner. Cost follows original bytes+decoded samples; limits are supplied
// by the caller and all variable retained storage uses its Domain. No runtime
// asset lookup. RNG belongs to the game session: callers inject seeded bounded
// choices into CharacterAudioEnvironment; this loader never invents an
// audio-local seed. Core R.20 (#rr-owner) expresses output ownership;
// Performance CDSA.32
// (guidelines/cpu-dsa/CDSA.32-transform-static-data-into-the-consumer-layout.md)
// prepares consumer lookup once. Both design corpora consulted. Reference and
// codec/budget evidence: docs/game-audio.md, animation-audio-presentation.md.
// The session also calls update() once per presentation frame, including world
// pause, so menu cues can finish independently of actor simulation. E.6 makes
// exception cleanup scoped; MEM.9 retains prepared banks across UI updates.
// Both corpora were consulted again for this lazy session ownership change.
#pragma once
#include "game/actors/audio/character_audio.hpp"
#include "game/content/original/stage.hpp"
namespace bw::game {
struct AudioMixLevels {
  static constexpr std::uint16_t kFullVolume = 256;
  std::uint16_t music{kFullVolume}, effects{kFullVolume};
};
class GameAudioFactory {
 public:
  GameAudioFactory() = default;
  GameAudioFactory(const GameAudioFactory&) = delete;
  GameAudioFactory& operator=(const GameAudioFactory&) = delete;
  GameAudioFactory(GameAudioFactory&&) = delete;
  GameAudioFactory& operator=(GameAudioFactory&&) = delete;
  virtual ~GameAudioFactory() = default;
  virtual std::shared_ptr<audio::CueOutput> create(
      std::span<const std::uint8_t> settings, audio::Limits limits,
      const std::shared_ptr<memory::Domain>& domain) = 0;
};
class GameAudio;
// Read-only borrowed profile lookup. The session must outlive every actor;
// this capability grants neither bank admission nor mixer/device control.
class CharacterAudioLookup {
 public:
  const PreparedCharacterAudio& prepared(CharacterAudioProfile) const;

 private:
  friend class GameAudio;
  explicit CharacterAudioLookup(const GameAudio& owner) : owner_(&owner) {}
  const GameAudio* owner_;
};
class GameAudio {
 public:
  GameAudio(const platform::Files& files, const resources::CatalogSet& catalogs,
            const std::shared_ptr<memory::Domain>& domain,
            GameAudioFactory& factory, audio::Limits limits,
            const content::LoadProgress& progress = {});
  // Failed incremental admission is terminal for this owner: no retry can
  // publish a policy over partially registered native banks.
  void prepare_mission(const platform::Files& files,
                       const resources::CatalogSet& catalogs,
                       const std::filesystem::path& sound_directory,
                       const std::shared_ptr<memory::Domain>& domain,
                       const content::LoadProgress& progress = {});
  // Admit additional actor profiles after placement/environment metadata is
  // decoded and before actors bind. Existing native banks are extended with
  // only their requested streaming cues. Prepared objects retain stable owned
  // addresses across later admission; no file access occurs during dispatch.
  void prepare_character_profiles(const platform::Files&,
                                  const resources::CatalogSet&,
                                  const std::filesystem::path& sound_directory,
                                  std::span<const CharacterAudioProfile>,
                                  const std::shared_ptr<memory::Domain>&);
  const PreparedCharacterAudio& prepared(CharacterAudioProfile) const;
  CharacterAudioLookup character_profiles() const {
    return CharacterAudioLookup(*this);
  }
  void prepare_frontend_music(const platform::Files& files,
                              const std::filesystem::path& sound_directory,
                              const std::shared_ptr<memory::Domain>& domain);
  // Original script track selection resolves through the source compatibility
  // table. Preparation replaces the one device track, so frontend reentry
  // must select its track again. No caller samples or paths survive this call.
  void prepare_script_music(std::uint32_t index, const platform::Files& files,
                            const std::filesystem::path& sound_directory,
                            const std::shared_ptr<memory::Domain>& domain);
  const std::shared_ptr<audio::CueOutput>& output() const { return output_; }
  // Session-global source slots/history survive mission retirement. The world
  // borrows update authority; actor adapters borrow request admission only.
  CharacterVoicePool& character_voices() { return voices_; }
  // Session control-thread service; independent of world tick admission.
  void update();
  // Called after scene borrowers retire; preserves prepared banks and UI audio.
  void retire_mission();
  // Observe game settings; this capability cannot mutate the stored profile.
  // Original effects volume controls effects and voice together, scale 1/256.
  void apply_mix(AudioMixLevels levels);
  const PreparedCharacterAudio& prepared() const {
    if (!prepared_) {
      throw std::logic_error("Mission audio policy is unprepared");
    }
    return *prepared_;
  }

 private:
  std::shared_ptr<audio::CueOutput> output_;
  CharacterVoicePool voices_;
  std::optional<PreparedCharacterAudio> prepared_;
  memory::Vector<std::shared_ptr<const PreparedCharacterAudio>>
      character_profiles_;
  std::optional<std::uint32_t> prepared_music_;
  bool mission_preparation_started_{};
};
}  // namespace bw::game
