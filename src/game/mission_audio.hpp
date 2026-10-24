// Mission-owned audio preparation, with native output chosen only by the
// target executable. Resolve stable catalogs once, validate Alicia metadata,
// register original common/voice banks, then seal before exposing the owner.
// Files/catalogs/factory/progress are borrowed only during construction; output
// copies its banks and the prepared policy retains cue values, not file spans.
// Failure destroys the unpublished output, including worker/device teardown.
// Actor audio owners borrow this policy and retain output only while this
// mission owner survives. One loading/control thread; progress may pump loading
// presentation only at complete-asset boundaries and must not reenter this
// owner. Cost follows original bytes+decoded samples; limits are supplied by
// the caller and all variable retained storage uses its Domain. No runtime
// asset lookup. RNG belongs to the game session: callers inject seeded bounded
// choices into CharacterAudioEnvironment; this loader never invents an
// audio-local seed. Core R.20 (#rr-owner) expresses output ownership;
// Performance CDSA.32
// (guidelines/cpu-dsa/CDSA.32-transform-static-data-into-the-consumer-layout.md)
// prepares consumer lookup once. Both design corpora consulted. Reference and
// codec/budget evidence: docs/game-audio.md, animation-audio-presentation.md.
#pragma once
#include "bw/content/stage.hpp"
#include "game/character_audio.hpp"
namespace bw::game {
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
class MissionAudio {
 public:
  MissionAudio(const platform::Files& files,
               const resources::CatalogSet& catalogs,
               const std::filesystem::path& sound_directory,
               const std::shared_ptr<memory::Domain>& domain,
               GameAudioFactory& factory, audio::Limits limits,
               const content::LoadProgress& progress = {});
  const std::shared_ptr<audio::CueOutput>& output() const { return output_; }
  const PreparedCharacterAudio& prepared() const {
    if (!prepared_) {
      throw std::logic_error("Mission audio policy is unprepared");
    }
    return *prepared_;
  }

 private:
  std::shared_ptr<audio::CueOutput> output_;
  std::optional<PreparedCharacterAudio> prepared_;
};
}  // namespace bw::game
