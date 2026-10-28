// Global voice ownership: Core I.1 and performance MEM.9 were read for
// design and implementation. Session owns ten fixed slots and shared history;
// world alone advances it; actors unregister before their environments die.
// Only the admitted world update advances delays and observes cue completion.
// Shared actor animation sound/voice policy over the shared cue engine.
// Preparation resolves original action/surface tables and weighted talk
// metadata to immutable cue identities; event dispatch never parses assets or
// looks up names. The world voice pool arbitrates priority/history, while
// each motion bank owns its retained sound handles so resetting one bank cannot
// stop another bank's sound. The environment supplies source
// emitter/contact/context decisions and bounded randomness; unknown surface
// context remains explicit, never a guessed material. All collaborators outlive
// these nonmoving owners, used on one control thread. Fixed voice
// tables/history and a setup-sized domain vector bound runtime work. Calls
// report unavailable/capacity/backend failure; no exception escapes motion
// callbacks. Explicit reset precedes output teardown; actor destruction also
// unregisters pending requests before its environment storage disappears.
// Core R.20 (#rr-owner) gives retained storage explicit domain ownership;
// Performance CDSA.32
// (guidelines/cpu-dsa/CDSA.32-transform-static-data-into-the-
// consumer-layout.md) prepares cue/variation lookups once. Both design corpora
// consulted. Profile extension rereads Core I.1 (#ri-explicit) and CDSA.32
// to prepare one immutable actor policy with explicit source class/group.
// Source and pinned FACT reference: animation-audio-presentation.md.
#pragma once

#include "bw/audio/output.hpp"
#include "bw/content/formats/audio_bank.hpp"
#include "bw/content/formats/text_table.hpp"
#include "game/actors/animation/motion_presentation.hpp"

namespace bw::game {
inline constexpr std::size_t kVoiceActions = 62, kVoiceVariants = 17,
                             kVoiceHistory = 8, kStreamVoiceSlots = 2,
                             kMemoryVoiceSlots = 8;
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
  float maximum_delay_seconds{};
  bool available{};
};
struct CharacterAudioProfile {
  static constexpr std::uint8_t kAliciaVoiceGroup = 22;
  std::uint8_t sound_class{}, voice_group{kAliciaVoiceGroup};
  bool operator==(const CharacterAudioProfile&) const = default;
};
// Source model-family/action dispatch and authored CharacterEnvironment voice
// group. Unknown voice groups resolve to source silence; unknown model families
// reject. The transformed-actor flag preserves original alternate group policy.
CharacterAudioProfile original_character_audio_profile(
    std::string_view model, std::uint32_t actor_flags,
    std::uint32_t voice_group);
std::string_view character_voice_group_name(std::uint8_t group);
class PreparedCharacterAudio {
 public:
  // GVTD/talk metadata are selected by the copied original actor profile.
  // Talk tree and original memory/stream bank definitions are borrowed only
  // during preparation.
  PreparedCharacterAudio(
      std::span<const std::uint8_t> voice_weights,
      const formats::TextTable& talk,
      const formats::AudioBankDefinition& voice_bank,
      const formats::AudioBankDefinition& streamed_voice_bank,
      CharacterAudioProfile profile = {});
  static PreparedCharacterAudio silent(CharacterAudioProfile);
  CharacterAudioProfile profile() const noexcept { return profile_; }
  std::optional<CharacterSoundCue> actor_sound(
      std::uint32_t action, std::optional<std::uint8_t> surface) const noexcept;
  static std::optional<CharacterSoundCue> sound(
      std::uint32_t action, std::optional<std::uint8_t> surface,
      std::uint8_t source_class = 0) noexcept;
  const CharacterVoiceAction* voice(std::uint32_t action) const noexcept;

 private:
  explicit PreparedCharacterAudio(CharacterAudioProfile profile)
      : profile_(profile) {}
  CharacterAudioProfile profile_;
  std::array<CharacterVoiceAction, kVoiceActions> voices_{};
};
struct CharacterSoundEmission {
  math::Vector3<float> position{};
  std::optional<std::uint8_t> surface, default_surface;
  bool suppressed{};
};
struct ActorSoundEmission {
  std::uint32_t action{};
  math::Vector3<float> position{};
  std::uint32_t affiliation{};
};
// Emission-only world capability. Copied descriptor flags preserve original
// affiliation; no actor pointer or backend voice handle crosses the boundary.
class ActorSoundEvents {
 public:
  ActorSoundEvents() = default;
  ActorSoundEvents(const ActorSoundEvents&) = delete;
  ActorSoundEvents& operator=(const ActorSoundEvents&) = delete;
  ActorSoundEvents(ActorSoundEvents&&) = delete;
  ActorSoundEvents& operator=(ActorSoundEvents&&) = delete;
  virtual ~ActorSoundEvents() = default;
  virtual void emit(ActorSoundEmission) = 0;
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
  virtual void emit_actor_sound(std::uint32_t action,
                                math::Vector3<float> position) = 0;
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
class CharacterAudio;
class CharacterVoicePool;
enum class CharacterVoiceRole : std::uint8_t { actor, player };
// Opaque request identity belongs to one nonmoving actor audio owner. It may
// be queried only while that owner lives; replacement completes the old
// request.
class CharacterVoiceRequest {
 public:
  CharacterVoiceRequest() = default;

 private:
  friend class CharacterAudio;
  CharacterVoiceRequest(const CharacterAudio* owner, std::uint64_t generation)
      : owner_(owner), generation_(generation) {}
  const CharacterAudio* owner_{};
  std::uint64_t generation_{};
};
class CharacterAudio {
 public:
  CharacterAudio(const PreparedCharacterAudio& prepared,
                 audio::CueOutput& output, CharacterVoicePool&,
                 CharacterVoiceRole);
  CharacterAudio(const CharacterAudio&) = delete;
  CharacterAudio& operator=(const CharacterAudio&) = delete;
  CharacterAudio(CharacterAudio&&) = delete;
  CharacterAudio& operator=(CharacterAudio&&) = delete;
  ~CharacterAudio();
  CharacterAudioResult voice(std::uint32_t action,
                             CharacterAudioEnvironment& environment) noexcept;
  CharacterAudioResult stop_voice() noexcept;
  bool voice_active() const;
  CharacterVoiceRequest voice_request() const noexcept {
    return {this, voice_generation_};
  }
  bool voice_active(CharacterVoiceRequest request) const;
  const PreparedCharacterAudio& prepared() const { return prepared_; }
  audio::CueOutput& output() const { return output_; }

 private:
  friend class CharacterVoicePool;
  CharacterAudioResult advance_voice(float world_delta_seconds) noexcept;
  const PreparedCharacterAudio& prepared_;
  audio::CueOutput& output_;
  CharacterVoicePool& pool_;
  CharacterVoiceRole role_;
  std::uint64_t identity_{};
  CharacterAudioEnvironment* environment_{};
  std::uint32_t voice_action_{};
  bool admitted_{};
  std::optional<audio::CueHandle> voice_;
  std::uint16_t voice_priority_{};
  std::uint64_t voice_generation_{};
  struct PendingVoice {
    CharacterVoiceVariant choice;
    float maximum_delay{}, remaining{};
    bool initialized{};
  };
  std::optional<PendingVoice> pending_voice_;
};
// Original global manager: stream slots precede memory slots. Fixed shared
// history excludes variants across actors in one voice group. Requests compare
// priority, or same-action/class distance to the player, before displacement.
// Pool outlives every CharacterAudio; reset is allowed only after retirement.
class CharacterVoicePool {
 public:
  CharacterVoicePool() = default;
  ~CharacterVoicePool() = default;
  CharacterVoicePool(const CharacterVoicePool&) = delete;
  CharacterVoicePool& operator=(const CharacterVoicePool&) = delete;
  CharacterVoicePool(CharacterVoicePool&&) = delete;
  CharacterVoicePool& operator=(CharacterVoicePool&&) = delete;
  CharacterAudioResult advance(float admitted_delta_seconds,
                               math::Vector3<float> player_position) noexcept;
  // Mission stop retires requests without erasing session variation history.
  CharacterAudioResult stop_all() noexcept;
  void reset();

 private:
  friend class CharacterAudio;
  struct History {
    std::uint64_t actor{};
    std::uint32_t action{};
    std::uint8_t group{}, variant{};
  };
  std::uint64_t identify();
  void release(CharacterAudio&) noexcept;
  std::array<CharacterAudio*, kStreamVoiceSlots + kMemoryVoiceSlots> slots_{};
  std::array<History, kVoiceHistory> history_{};
  std::size_t next_history_{};
  std::uint64_t next_identity_{};
  math::Vector3<float> player_position_{};
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
