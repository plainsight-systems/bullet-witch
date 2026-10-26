// Alicia animation sound/voice policy over the shared cue engine. Preparation
// resolves original action/surface tables and weighted talk metadata to
// immutable cue identities; event dispatch never parses assets or looks up
// names. A shared actor owner arbitrates voice priority/history, while each
// motion bank owns its retained sound handles so resetting one bank cannot stop
// another bank's sound. The environment supplies source emitter/contact/context
// decisions and bounded randomness; unknown surface context remains explicit,
// never a guessed material. All collaborators outlive these nonmoving owners,
// used on one control thread. Fixed voice tables/history and a setup-sized
// domain vector bound runtime work. Calls report unavailable/capacity/backend
// failure; no exception escapes motion callbacks. Explicit reset precedes
// output teardown; destructors are passive. Core R.20 (#rr-owner) gives
// retained storage explicit domain ownership; Performance CDSA.32
// (guidelines/cpu-dsa/CDSA.32-transform-static-data-into-the-
// consumer-layout.md) prepares cue/variation lookups once. Both design corpora
// consulted. Source and pinned FACT reference: animation-audio-presentation.md.
#pragma once

#include "bw/audio/output.hpp"
#include "bw/content/formats/audio_bank.hpp"
#include "bw/content/formats/text_table.hpp"
#include "game/actors/animation/motion_presentation.hpp"

namespace bw::game {
inline constexpr std::size_t kVoiceActions = 62, kVoiceVariants = 17,
                             kVoiceHistory = 8;
struct CharacterSoundCue {
  std::uint16_t bank{}, cue{};
  bool retained{};
};
struct CharacterVoiceVariant {
  std::uint16_t bank{}, cue{};
  std::uint8_t weight{};
  bool available{};
};
struct CharacterVoiceAction {
  std::array<CharacterVoiceVariant, kVoiceVariants> variants{};
  std::uint16_t priority{};
  bool available{};
};
class PreparedCharacterAudio {
 public:
  // GVTD contains all groups; only the proven Alicia group22 is admitted here.
  // Talk tree and original memory/stream bank definitions are borrowed only
  // during preparation.
  PreparedCharacterAudio(
      std::span<const std::uint8_t> voice_weights,
      const formats::TextTable& alicia_talk,
      const formats::AudioBankDefinition& voice_bank,
      const formats::AudioBankDefinition& streamed_voice_bank);
  static std::optional<CharacterSoundCue> sound(
      std::uint32_t action, std::optional<std::uint8_t> surface) noexcept;
  const CharacterVoiceAction* voice(std::uint32_t action) const noexcept;

 private:
  std::array<CharacterVoiceAction, kVoiceActions> voices_{};
};
struct CharacterSoundEmission {
  math::Vector3<float> position{};
  std::optional<std::uint8_t> surface, default_surface;
  bool suppressed{};
};
class CharacterAudioEnvironment {
 public:
  CharacterAudioEnvironment() = default;
  CharacterAudioEnvironment(const CharacterAudioEnvironment&) = delete;
  CharacterAudioEnvironment& operator=(const CharacterAudioEnvironment&) =
      delete;
  CharacterAudioEnvironment(CharacterAudioEnvironment&&) = delete;
  CharacterAudioEnvironment& operator=(CharacterAudioEnvironment&&) = delete;
  virtual ~CharacterAudioEnvironment() = default;
  // Position/contact selection follows 5f8ac0; retained updates use the raw
  // stored bone/model emitter, not a new support query.
  virtual CharacterSoundEmission sound_emission(
      SoundMotionCommand command) const = 0;
  virtual math::Vector3<float> bone_position(std::int32_t bone) const = 0;
  virtual math::Vector3<float> voice_position() const = 0;
  // Result must be strictly less than upper, matching the original bounded RNG.
  virtual std::uint32_t random_below(std::uint32_t upper) = 0;
};
enum class CharacterAudioResult : std::uint8_t {
  played,
  updated,
  stopped,
  suppressed,
  unavailable,
  capacity,
  backend_failure
};
class CharacterAudio {
 public:
  CharacterAudio(const PreparedCharacterAudio& prepared,
                 audio::CueOutput& output)
      : prepared_(prepared), output_(output) {}
  CharacterAudio(const CharacterAudio&) = delete;
  CharacterAudio& operator=(const CharacterAudio&) = delete;
  CharacterAudio(CharacterAudio&&) = delete;
  CharacterAudio& operator=(CharacterAudio&&) = delete;
  ~CharacterAudio() = default;
  CharacterAudioResult voice(std::uint32_t action,
                             CharacterAudioEnvironment& environment) noexcept;
  CharacterAudioResult stop_voice() noexcept;
  const PreparedCharacterAudio& prepared() const { return prepared_; }
  audio::CueOutput& output() const { return output_; }

 private:
  struct History {
    std::uint32_t action{};
    std::uint8_t variant{};
  };
  const PreparedCharacterAudio& prepared_;
  audio::CueOutput& output_;
  std::array<History, kVoiceHistory> history_{};
  std::size_t next_history_{};
  std::optional<audio::CueHandle> voice_;
  std::uint16_t voice_priority_{};
};
enum class CharacterAudioBank : std::uint8_t { body, upper };
class CharacterAudioChannel {
 public:
  CharacterAudioChannel(CharacterAudio& owner, CharacterAudioBank bank,
                        std::size_t retained_capacity,
                        const std::shared_ptr<memory::Domain>& domain);
  CharacterAudioChannel(const CharacterAudioChannel&) = delete;
  CharacterAudioChannel& operator=(const CharacterAudioChannel&) = delete;
  CharacterAudioChannel(CharacterAudioChannel&&) = delete;
  CharacterAudioChannel& operator=(CharacterAudioChannel&&) = delete;
  ~CharacterAudioChannel() = default;
  CharacterAudioResult sound(SoundMotionCommand command,
                             animation::MotionEventPhase phase,
                             const animation::MotionEventContext& context,
                             CharacterAudioEnvironment& environment) noexcept;
  CharacterAudioResult voice(VoiceMotionCommand command,
                             CharacterAudioEnvironment& environment) noexcept;
  CharacterAudioResult reset() noexcept;

 private:
  struct Retained {
    std::optional<audio::CueHandle> handle;
    std::uint32_t action{};
    std::int32_t bone{};
  };
  CharacterAudio& owner_;
  CharacterAudioBank bank_;
  memory::Vector<Retained> retained_;
};
}  // namespace bw::game
