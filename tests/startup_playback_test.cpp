// Explicit fixture factory keeps these contracts independent of codec linkage;
// C.129 (#rh-kind), E.6 (#re-raii) and cpp-performance COPY.5 (guidelines/
// copy-move/COPY.5-resource-owning-types-are-move-only.md) preserve unique
// owners. Shared media control contracts exercised with independent synthetic
// decode and native-audio observations, not startup scenes or device
// assumptions. Metadata determines the clock before PCM exists; output starts
// after first presentation; audio drain and terminal-frame exposure both gate
// completion. No files/processes/native calls. Fake source/output outlive
// borrowed owners. Design/implementation MCP: CP.23 (#rconc-join) verifies stop
// before borrowed source destruction; cpp-performance MEM.9 (guidelines/memory/
// MEM.9-embedded-allocate-at-init.md) keeps fixture storage bounded and
// explicit.
#include "game/startup_playback.hpp"

#include <stdexcept>
#include <utility>
#include <vector>

namespace {
void require(bool condition, const char* message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}
class Source final : public bw::video::Decoder {
 public:
  explicit Source(std::vector<bw::video::VideoFrame> frames, bool audio = false)
      : frames_(std::move(frames)) {
    metadata_.has_audio = audio;
  }
  const bw::video::DecoderMetadata& metadata() const noexcept override {
    return metadata_;
  }
  bw::video::VideoPoll poll_video() override {
    if (position_ == frames_.size()) {
      return {.state = bw::video::DecodeState::end};
    }
    return {.state = bw::video::DecodeState::sample,
            .sample = std::move(frames_.at(position_++))};
  }
  bw::video::AudioRead read_audio(std::span<float>) noexcept override {
    return {};
  }
  std::size_t buffered_audio_frames() const noexcept override { return 0; }
  bool audio_ended() const noexcept override { return false; }
  void wait_until_ready(std::size_t) override {}
  std::exception_ptr failure() const override { return {}; }
  bw::video::DecoderDiagnostics diagnostics() const override {
    return {.audio_frames = metadata_.has_audio ? 48000U : 0U};
  }
  void stop() noexcept override {}

 private:
  bw::video::DecoderMetadata metadata_;
  std::vector<bw::video::VideoFrame> frames_;
  std::size_t position_{};
};
class Audio final : public bw::video::AudioOutput {
 public:
  void prepare(bw::video::Decoder&, std::size_t) override { prepared = true; }
  void start() override { started = true; }
  bw::video::AudioSnapshot snapshot() const override { return value; }
  void stop() noexcept override { ++stops; }
  bw::video::AudioSnapshot value;
  bool prepared{}, started{};
  std::size_t stops{};
};
class Output final : public bw::video::Output {
 public:
  void upload(const bw::video::VideoFrame& frame) override {
    timestamps.push_back(frame.timestamp_seconds);
  }
  void clear_video() noexcept override {}
  bool draw(float) override { return drawable; }
  bw::video::AudioOutput& audio() noexcept override { return device; }
  Audio device;
  std::vector<double> timestamps;
  bool drawable{true};
};
bw::video::VideoFrame frame(double timestamp, double duration) {
  return {.width = 1,
          .height = 1,
          .timestamp_seconds = timestamp,
          .duration_seconds = duration,
          .bgra = {0, 0, 0, 255}};
}
void video_start_and_terminal_presentation() {
  Output output;
  bw::video::Player player(output, [](const auto&) {
    return std::make_unique<Source>(std::vector{frame(0, 1), frame(1, 1)});
  });
  player.open("synthetic");
  player.advance(10);
  require(output.timestamps == std::vector<double>{0},
          "Opening delay does not advance a video-only movie");
  player.draw(1);
  player.advance(2);
  require(!player.complete(),
          "Late terminal upload is not a presented terminal interval");
  player.draw(1);
  player.advance(0.5F);
  require(!player.complete(),
          "Terminal frame retains its presentation duration");
  player.advance(0.5F);
  require(player.complete() && player.statistics().final_frame_presented,
          "Terminal exposure and video EOF finish once");
  player.advance(1);
  require(player.completed_movies() == 1, "Completion is counted once");
}
void audio_metadata_and_drain() {
  Output output;
  bw::video::Player player(output, [](const auto&) {
    return std::make_unique<Source>(std::vector{frame(0, 0.5), frame(0.5, 0.5)},
                                    true);
  });
  player.open("synthetic");
  require(output.device.prepared && !output.device.started,
          "Audio is prepared before first presentation");
  output.drawable = false;
  player.draw(1);
  require(!output.device.started, "Unavailable drawable cannot start playback");
  output.drawable = true;
  player.draw(1);
  player.advance(10);
  require(
      player.statistics().media_seconds == 0 && output.timestamps.size() == 1,
      "Declared audio with no valid clock cannot become video-only wall time");
  output.device.value = {.media_seconds = 0.75,
                         .submitted_frames = 48000,
                         .played_frames = 36000,
                         .valid = true};
  player.advance(0);
  player.draw(1);
  output.device.value.media_seconds = 1;
  output.device.value.played_frames = 48000;
  player.advance(1);
  require(!player.complete(),
          "Consumed time alone does not replace actual output drain");
  output.device.value.drained = true;
  player.advance(0);
  require(player.complete() && player.statistics().played_audio_frames == 48000,
          "Device drain and terminal presentation complete audio movie");
  player.stop();
  require(output.device.stops == 2 && !player.complete(),
          "Stop cancels output before releasing session");
}
void buffering_snapshot_holds_media() {
  Output output;
  bw::video::Player player(output, [](const auto&) {
    return std::make_unique<Source>(std::vector{frame(0, 1), frame(1, 1)},
                                    true);
  });
  player.open("synthetic");
  player.draw(1);
  output.device.value = {
      .media_seconds = 0.25, .valid = true, .buffering = true};
  player.advance(3);
  require(player.statistics().media_seconds == 0.25,
          "Maintenance silence cannot advance media with wall time");
  output.device.value.media_seconds = 0.249;
  player.advance(0);
  require(player.statistics().media_seconds == 0.249,
          "Decoder-approved media PTS correction is distinct from device time "
          "regression");
  output.device.value.media_seconds = 1.25;
  output.device.value.buffering = false;
  player.advance(0);
  require(output.timestamps == std::vector<double>({0, 1}),
          "Resumed consumed PCM resumes video selection");
  output.device.value.failure_code = -1;
  bool failed = false;
  try {
    player.advance(0);
  } catch (const std::runtime_error&) {
    failed = true;
  }
  require(failed, "Native discontinuity/error is not successful completion");
}
void audio_ends_before_video() {
  Output output;
  bw::video::Player player(output, [](const auto&) {
    return std::make_unique<Source>(
        std::vector{frame(0, 1), frame(1, 1), frame(2, 1)}, true);
  });
  player.open("synthetic");
  player.draw(1);
  output.device.value = {.media_seconds = 0.1, .valid = true, .drained = true};
  player.advance(10);
  require(player.statistics().media_seconds == 0.1,
          "Drain establishes tail baseline without old wall delta");
  for (int tick = 0; tick < 8 && !player.complete(); ++tick) {
    player.advance(0.5F);
    player.draw(1);
  }
  require(player.complete() && output.timestamps.size() == 3,
          "Explicit audio EOF/drain permits a long video tail");
}
void interrupted_fade() {
  Output output;
  bw::game::StartupPlayback playback(output, "unused", [](const auto&) {
    return std::make_unique<Source>(std::vector<bw::video::VideoFrame>{});
  });
  using bw::game::FadeDirection;
  playback.begin_fade(FadeDirection::reveal, 1);
  playback.advance({.wall_delta = 5, .game_delta = 0.25F});
  require(playback.opacity() == 0.25F && playback.fade_pending(),
          "Recovered fade uses game delta");
  playback.begin_fade(FadeDirection::conceal, 1);
  require(playback.opacity() == 0.25F, "Interrupted fade preserves alpha");
  playback.advance({.wall_delta = 5, .game_delta = 0.25F});
  require(playback.opacity() == 0 && playback.fade_pending(),
          "Clamping does not finish fade timer early");
  playback.advance({.wall_delta = 1, .game_delta = 1});
  require(!playback.fade_pending(), "Terminal fade clears timer");
}
}  // namespace
void startup_playback_contracts() {
  video_start_and_terminal_presentation();
  audio_metadata_and_drain();
  buffering_snapshot_holds_media();
  audio_ends_before_video();
  interrupted_fade();
}
