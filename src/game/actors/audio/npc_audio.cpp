// Preserve recovered per-actor sound/voice ownership independently of the AI
// controller. Core I.11/MEM.9 were reread for implementation: borrow the actor
// and prepared scene, allocate bounded channels at admission, stop before
// retire.
#include "npc_audio.hpp"

#include <cstdio>
#include <stdexcept>

#include "game/actors/npc/npc_actor.hpp"
namespace bw::game {
namespace {
constexpr std::size_t kRetainedSounds = 8;
void require_audio(CharacterAudioResult result) {
  if (result == CharacterAudioResult::capacity ||
      result == CharacterAudioResult::backend_failure) {
    throw std::runtime_error("NPC actor audio could not be admitted");
  }
}
}  // namespace
NpcAudio::NpcAudio(const NpcActor& actor,
                   const PreparedCharacterAudio& prepared,
                   audio::CueOutput& output,
                   const scene::StaticCollision& collision,
                   SourceRandom& random, float radius,
                   CharacterVoicePool& voices, ActorSoundEvents& events,
                   std::uint32_t affiliation,
                   const std::shared_ptr<memory::Domain>& domain)
    : actor_(actor),
      collision_(collision),
      random_(random),
      body_radius_(radius),
      sound_events_(events),
      affiliation_(affiliation),
      audio_(prepared, output, voices, CharacterVoiceRole::actor),
      body_(audio_, CharacterAudioBank::body, kRetainedSounds, domain),
      upper_(audio_, CharacterAudioBank::upper, kRetainedSounds, domain),
      additive_(audio_, CharacterAudioBank::body, kRetainedSounds, domain) {}
NpcAudio::~NpcAudio() {
  try {
    stop();
  } catch (const std::exception& error) {
    std::fputs("NPC audio teardown failed: ", stderr);
    std::fputs(error.what(), stderr);
    std::fputs("\n", stderr);
  }
}
void NpcAudio::set_player_position(math::Vector3<float> value) {
  player_position_ = value;
}
CharacterAudioResult NpcAudio::voice(std::uint32_t action) {
  const auto result = audio_.voice(action, *this);
  require_audio(result);
  return result;
}
void NpcAudio::damage_voice(std::uint32_t action) {
  require_audio(audio_.stop_voice());
  (void)voice(action);
}
CharacterVoiceRequest NpcAudio::voice_request() const noexcept {
  return audio_.voice_request();
}
bool NpcAudio::voice_active(CharacterVoiceRequest request) const {
  return audio_.voice_active(request);
}
void NpcAudio::consume(std::size_t bank, const NpcMotionEvent& event) {
  const std::array<CharacterAudioChannel*, 3> channels{&body_, &upper_,
                                                       &additive_};
  auto& channel = *channels.at(bank);
  if (std::holds_alternative<NpcPresentationReset>(event.command)) {
    require_audio(channel.reset());
    return;
  }
  const auto& command = std::get<MotionPresentationCommand>(event.command);
  if (const auto* sound = std::get_if<SoundMotionCommand>(&command)) {
    require_audio(channel.sound(*sound, event.phase, event.context, *this));
  } else if (const auto* voice = std::get_if<VoiceMotionCommand>(&command)) {
    require_audio(channel.voice(*voice, *this));
  }
}
void NpcAudio::stop() {
  require_audio(body_.reset());
  require_audio(upper_.reset());
  require_audio(additive_.reset());
  require_audio(audio_.stop_voice());
}
math::Vector3<float> NpcAudio::bone_position(std::int32_t bone) const {
  return actor_.motion_attachment_position(bone);
}
math::Vector3<float> NpcAudio::voice_position() const {
  return actor_.state().placement.position;
}
std::uint32_t NpcAudio::random_below(std::uint32_t upper) {
  return random_.next(upper);
}
void NpcAudio::emit_actor_sound(std::uint32_t action,
                                math::Vector3<float> position) {
  sound_events_.emit({action, position, affiliation_});
}
CharacterSoundEmission NpcAudio::sound_emission(
    SoundMotionCommand command) const {
  CharacterSoundEmission result{.position = bone_position(command.bone),
                                .surface = 0,
                                .default_surface = 0};
  constexpr float kNearSquared = 225, kClearance = .01F;
  constexpr std::uint32_t kFirstEnd = 5, kSecondBegin = 17, kSecondEnd = 19;
  const bool near =
      command.action <= kFirstEnd ||
      (command.action >= kSecondBegin && command.action <= kSecondEnd);
  const auto delta = math::subtract(result.position, player_position_);
  if (!near || math::dot(delta, delta) >= kNearSquared) {
    result.surface = 1;
    return result;
  }
  auto from = result.position, to = result.position;
  from[1] += body_radius_;
  to[1] -= body_radius_;
  const auto hit = collision_.clip_segment({from, to, kClearance, 0});
  if (hit.clipped) {
    result.position = hit.position;
    result.surface = hit.surface.value_or(0);
  }
  return result;
}
}  // namespace bw::game
