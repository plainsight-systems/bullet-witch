// Native service ownership checks without opening a device: a zero-byte domain
// rejects State allocation before constructor/worker/AudioQueue execution.
// Verify retained empty capability and failure/stop/destruction cleanup. This
// does not test successful AudioQueue startup, timing or device memory usage.
// Design R.1 (#rr-raii), implementation E.6 (#re-raii), MEM.6
// (guidelines/memory/MEM.6-inject-allocators-behind-one-interface.md) injects
// failure; MEM.9 (guidelines/memory/MEM.9-embedded-allocate-at-init.md) keeps
// acquisition before callback execution. Fixed fixture data, no I/O.
#include <stdexcept>

#include "bw/platform/media_audio.hpp"
#include "media_memory_fixture.hpp"

namespace {
class Source final : public bw::video::Decoder {
 public:
  const bw::video::DecoderMetadata& metadata() const noexcept override {
    return metadata_;
  }
  bw::video::VideoPoll poll_video() override { return {}; }
  bw::video::AudioRead read_audio(std::span<float>) noexcept override {
    return {};
  }
  std::size_t buffered_audio_frames() const noexcept override { return 0; }
  bool audio_ended() const noexcept override { return false; }
  void wait_until_ready(std::size_t) override {}
  std::exception_ptr failure() const override { return {}; }
  bw::video::DecoderDiagnostics diagnostics() const override { return {}; }
  void stop() noexcept override {}

 private:
  bw::video::DecoderMetadata metadata_{.has_audio = true};
};
void require(bool condition, const char* message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}
}  // namespace

void media_audio_memory_contracts() {
  const auto backing = std::make_shared<bw::test::MediaBacking>();
  auto domain = bw::memory::Domain::create(backing, {.max_live_bytes = 0});
  const std::weak_ptr<bw::memory::Domain> owner = domain;
  Source source;
  {
    bw::platform::MediaAudio audio(domain);
    domain.reset();
    require(!owner.expired(), "Empty audio service retains its domain");
    bool failed = false;
    try {
      audio.prepare(source, 1);
    } catch (const std::bad_alloc&) {
      failed = true;
    }
    require(failed && owner.lock()->snapshot().live_allocations == 0 &&
                !audio.snapshot().valid,
            "Audio rejects preparation without publishing state or charge");
    audio.stop();
    audio.stop();
  }
  require(owner.expired() && backing->acquired == backing->released,
          "Audio service releases its domain after failed preparation");
}
