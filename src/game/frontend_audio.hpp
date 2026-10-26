// Frontend sound policy borrows the session's cue/music capabilities. Main-menu
// entry lazily prepares and starts the original music; child menus keep it
// alive. Source exits request a supplied-time linear fade. This owner never
// pumps the device, reads a clock, accesses Profile or owns native playback
// resources. The preparation callable and borrowed output must outlive this
// stable owner; preparation failure publishes neither readiness nor playback.
// Caller advances existing work before submitting new state-transition events.
// O(1) music work and MenuAudio's fixed queue; allocation occurs only at lazy
// preparation. Core E.6 (#re-raii) and Performance MEM.9 (guidelines/memory/
// MEM.9-embedded-allocate-at-init.md), read for design/implementation, confine
// resource preparation to the explicit entry boundary. See frontend-audio.md.
#pragma once
#include <functional>

#include "bw/audio/music.hpp"
#include "game/menu_audio.hpp"

namespace bw::game {
class FrontEndAudio {
 public:
  FrontEndAudio(audio::CueOutput& output, std::function<void()> prepare_music);
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
  audio::MusicOutput& music_;
  std::function<void()> prepare_music_;
  double remaining_{}, duration_{};
  float gain_{1}, fade_start_{1};
  bool prepared_{}, fading_{};
};
}  // namespace bw::game
