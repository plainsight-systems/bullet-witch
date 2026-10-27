// Frontend publishes original menu cues and music requests to session owners.
// It owns no music gain, fade, transport or clock. The command handle borrows
// the stable MusicPlayback owner; callers advance only this fixed cue queue.
// Core I.1/E.6 and performance MEM.9 were read for both phases: commands expose
// no device/clock authority; all track preparation belongs to session
// admission.
#pragma once
#include "game/session/audio/music_playback.hpp"
#include "menu_audio.hpp"

namespace bw::game {
class FrontEndAudio {
 public:
  FrontEndAudio(audio::CueOutput& output, MusicCommands music);
  FrontEndAudio(const FrontEndAudio&) = delete;
  FrontEndAudio& operator=(const FrontEndAudio&) = delete;
  FrontEndAudio(FrontEndAudio&&) = delete;
  FrontEndAudio& operator=(FrontEndAudio&&) = delete;
  ~FrontEndAudio() = default;
  void submit(const MenuSoundRequests& requests);
  void advance(float presentation_seconds);
  void start_menu_music();
  void stop_menu_music(float duration_seconds);

 private:
  MenuAudio cues_;
  MusicCommands music_;
};
}  // namespace bw::game
