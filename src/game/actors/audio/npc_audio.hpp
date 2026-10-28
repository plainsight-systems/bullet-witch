// Per-admitted-actor audio ownership is independent of AI admission. The world
// destroys controllers before this owner, then retires its borrowed actor and
// immutable prepared banks. World voice arbitration and three motion-bank
// sound lifetimes reuse CharacterAudio; no second mixer or clock is introduced.
// World supplies a copied player position and owns voice advancement.
// Controllers borrow only request/observation operations, including source
// reaction-voice identity. Storage is prepared at admission, with no per-frame
// allocation. Core I.11
// (#ri-raw) keeps borrows explicit; performance MEM.9 keeps retained channels
// bounded. Both design corpora read. See first-combat-actor-audio.md.
#pragma once
#include "bw/spatial/static_collision.hpp"
#include "character_audio.hpp"
#include "game/actors/npc/npc_motion_events.hpp"
#include "game/session/compatibility/source_random.hpp"
namespace bw::game {
class NpcActor;
class NpcAudio final : private CharacterAudioEnvironment {
 public:
  NpcAudio(const NpcActor&, const PreparedCharacterAudio&, audio::CueOutput&,
           const scene::StaticCollision&, SourceRandom&, float body_radius,
           CharacterVoicePool&, ActorSoundEvents&, std::uint32_t affiliation,
           const std::shared_ptr<memory::Domain>&);
  ~NpcAudio() override;
  NpcAudio(const NpcAudio&) = delete;
  NpcAudio& operator=(const NpcAudio&) = delete;
  NpcAudio(NpcAudio&&) = delete;
  NpcAudio& operator=(NpcAudio&&) = delete;
  void set_player_position(math::Vector3<float>);
  CharacterAudioResult voice(std::uint32_t action);
  void damage_voice(std::uint32_t action);
  CharacterVoiceRequest voice_request() const noexcept;
  bool voice_active(CharacterVoiceRequest) const;
  void consume(std::size_t bank, const NpcMotionEvent&);
  void stop();

 private:
  CharacterSoundEmission sound_emission(SoundMotionCommand) const override;
  math::Vector3<float> bone_position(std::int32_t) const override;
  math::Vector3<float> voice_position() const override;
  std::uint32_t random_below(std::uint32_t) override;
  void emit_actor_sound(std::uint32_t, math::Vector3<float>) override;
  const NpcActor& actor_;
  const scene::StaticCollision& collision_;
  SourceRandom& random_;
  float body_radius_;
  ActorSoundEvents& sound_events_;
  std::uint32_t affiliation_;
  math::Vector3<float> player_position_{};
  CharacterAudio audio_;
  CharacterAudioChannel body_, upper_, additive_;
};
}  // namespace bw::game
