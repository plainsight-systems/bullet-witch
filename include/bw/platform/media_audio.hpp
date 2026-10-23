// Native media audio service; unrelated to startup/game policy. prepare borrows
// one Decoder PCM source until stop joins the native owner. Main/control thread
// calls this API; a private service thread owns AudioQueue, runloop callbacks,
// fixed output blocks and device/media mappings. Every callback is retired
// before source destruction. Source failures/device errors become snapshots.
// Algorithm: prefill -> explicit start -> callback PCM/silence feed -> native
// clock mapping -> EOF drain. Snapshot publication uses atomic fields and a
// version, never unsynchronized struct reads. Fixed metadata capacity bounds
// native lookahead; exhaustion fails explicitly. See docs/video-player.md.
// Design/implementation MCP: CP.23 (#rconc-join) scopes borrowed PCM lifetime;
// cpp-performance MEM.9 (guidelines/memory/MEM.9-embedded-allocate-at-init.md)
// prepares all authored callback storage. Platform internals may allocate.
#pragma once
#include <memory>

#include "bw/video/output.hpp"
namespace bw::platform {
class MediaAudio final : public video::AudioOutput {
 public:
  MediaAudio();
  ~MediaAudio() override;
  MediaAudio(const MediaAudio&) = delete;
  MediaAudio& operator=(const MediaAudio&) = delete;
  MediaAudio(MediaAudio&&) = delete;
  MediaAudio& operator=(MediaAudio&&) = delete;
  void prepare(video::Decoder& source, std::size_t prime_frames) override;
  void start() override;
  video::AudioSnapshot snapshot() const override;
  void stop() noexcept override;

 private:
  struct State;
  std::unique_ptr<State> state_;
};
}  // namespace bw::platform
