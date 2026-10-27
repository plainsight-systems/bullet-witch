// Frontend is a requester; the session music owner alone advances envelopes.
// Core I.1/E.6 and MEM.9 were consulted in both phases; see matching header.
#include "frontend_audio.hpp"
namespace bw::game {
FrontEndAudio::FrontEndAudio(audio::CueOutput& output, MusicCommands music)
    : cues_(output), music_(music) {}
void FrontEndAudio::submit(const MenuSoundRequests& requests) {
  cues_.submit(requests);
}
void FrontEndAudio::start_menu_music() {
  constexpr std::uint32_t kFrontendTrack = 16;
  music_.play({kFrontendTrack, 0});
}
void FrontEndAudio::stop_menu_music(float duration_seconds) {
  music_.stop(duration_seconds);
}
void FrontEndAudio::advance(float presentation_seconds) {
  cues_.advance(presentation_seconds);
}
}  // namespace bw::game
