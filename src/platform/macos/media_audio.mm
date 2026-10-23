// Memory adoption: injected domain owns service State/control and its fixed
// callback mapping arrays. Prepare acquires before publication; failed native
// setup joins its owner and releases that state before throwing. Stop joins
// before the final domain release. Allocator capability remains alive while
// empty; default construction is explicit legacy test/probe compatibility.
// No allocation, capability copy or domain lock enters a native callback.
// AudioQueue buffers/runtime remain external native allocations.
// Design R.1 (#rr-raii), implementation E.6 (#re-raii), MEM.6
// (guidelines/memory/MEM.6-inject-allocators-behind-one-interface.md) and MEM.9
// (guidelines/memory/MEM.9-embedded-allocate-at-init.md) retain injected
// lifetime and callback preallocation; consulted in the media migration phases.
// macOS reusable media audio service; see platform/media_audio.hpp and
// docs/video-player.md. One joined CFRunLoop owner creates AudioQueue and runs
// its callbacks. Callbacks pull prepared PCM, or maintenance silence while
// rebuffering, without render/game calls, allocation or exceptions. Actual
// enqueue timestamps map fixed device ranges to media; buffer reuse is not
// playback. Native time and range retirement run independently of presentation.
// prepare borrows Decoder until stop joins; partial acquisitions retire on the
// owner. Snapshot fields are atomic, with a sequence for coherent bounded reads.
// Initial queue time may be invalid; true native scheduling gaps fail explicitly.
// Storage: three 1024-frame stereo blocks and 256 fixed mapping records, plus
// AudioQueue's external storage. O(block frames) copy; O(expired ranges) retire.
// Design/implementation MCP: CP.23 (#rconc-join) scopes borrowed PCM lifetime;
// cpp-performance MEM.9 (guidelines/memory/MEM.9-embedded-allocate-at-init.md)
// allocates authored callback storage before start. Unreal MediaSoundComponent
// and MediaPlayerFacade guide consumer-owned timing, without source copying.
#include "bw/platform/media_audio.hpp"

#include <AudioToolbox/AudioToolbox.h>
#include <CoreFoundation/CoreFoundation.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <climits>
#include <cmath>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <string>
#include <thread>

namespace bw::platform {
namespace {
constexpr std::size_t kBlockFrames = 1024;
constexpr std::size_t kBlocks = 3;
constexpr std::size_t kMappings = 256;
constexpr std::size_t kChannels = video::kAudioChannelCount;
constexpr double kRate = video::kAudioSampleRate;
constexpr double kServicePeriod = 0.005;
constexpr double kMaximumOriginSeconds = 21'600;
constexpr std::int32_t kSourceFailure = -100001;
constexpr std::int32_t kMappingFailure = -100002;
constexpr std::int32_t kClockFailure = -100003;
constexpr std::int32_t kNativeGap = -100004;
enum class RangeKind : std::uint8_t { pcm, initial, maintenance };
struct Range {
  double device_begin{}, media_begin{};
  std::size_t frames{};
  std::uint64_t pcm_begin{};
  RangeKind kind{};
};
struct Published {
  std::atomic<std::uint64_t> sequence{}, submitted{}, played{}, initial{}, maintenance{},
      underruns{};
  std::atomic<double> media{};
  std::atomic<bool> valid{}, drained{}, buffering{};
  std::atomic<std::int32_t> failure{};
  void store(const video::AudioSnapshot& value) noexcept {
    sequence.fetch_add(1);
    media.store(value.media_seconds);
    submitted.store(value.submitted_frames);
    played.store(value.played_frames);
    initial.store(value.initial_silence_frames);
    maintenance.store(value.maintenance_frames);
    underruns.store(value.underruns);
    valid.store(value.valid);
    drained.store(value.drained);
    buffering.store(value.buffering);
    failure.store(value.failure_code);
    sequence.fetch_add(1);
  }
  video::AudioSnapshot load() const noexcept {
    constexpr int retries = 4;
    for (int attempt = 0; attempt < retries; ++attempt) {
      const auto before = sequence.load();
      if ((before & 1U) != 0) {
        continue;
      }
      video::AudioSnapshot result{.media_seconds = media.load(),
                                  .submitted_frames = submitted.load(),
                                  .played_frames = played.load(),
                                  .initial_silence_frames = initial.load(),
                                  .maintenance_frames = maintenance.load(),
                                  .underruns = underruns.load(),
                                  .valid = valid.load(),
                                  .drained = drained.load(),
                                  .buffering = buffering.load(),
                                  .failure_code = failure.load()};
      if (sequence.load() == before) {
        return result;
      }
    }
    return {};  // Busy publication: invalid timing, never a fabricated clock.
  }
};
static_assert(std::atomic<double>::is_always_lock_free);
static_assert(std::atomic<std::uint64_t>::is_always_lock_free);
}  // namespace
struct MediaAudio::State {
  video::Decoder& source;
  const std::size_t prime_frames;
  Published published;
  std::atomic<bool> stop_requested{}, start_requested{}, initialized{};
  std::thread worker;
  // Everything below is confined to worker/runloop callbacks.
  AudioQueueRef queue{};
  AudioQueueTimelineRef timeline{};
  std::array<AudioQueueBufferRef, kBlocks> buffers{};
  std::array<Range, kMappings> ranges{};
  std::size_t range_head{}, range_count{}, leading_frames{};
  video::AudioSnapshot current;
  double next_media{}, last_device_time{};
  bool started{}, ending{}, drain_requested{}, have_device_time{};
  State(video::Decoder& decoder, std::size_t prime) : source(decoder), prime_frames(prime) {
    worker = std::thread([this] { run(); });
    initialized.wait(false);
  }
  ~State() {
    stop_requested.store(true);
    if (worker.joinable()) {
      worker.join();
    }
  }
  State(const State&) = delete;
  State& operator=(const State&) = delete;
  State(State&&) = delete;
  State& operator=(State&&) = delete;
  bool status(OSStatus code) noexcept {
    if (code != noErr) {
      current.failure_code = code;
      return false;
    }
    return true;
  }
  static void callback(void* context, AudioQueueRef, AudioQueueBufferRef buffer) noexcept {
    auto& state = *static_cast<State*>(context);
    if (!state.ending && state.current.failure_code == 0 && !state.stop_requested.load()) {
      state.observe();
      state.fill(buffer);
    }
  }
  void retire(double device_time) noexcept {
    while (range_count != 0) {
      const auto& range = std::span{ranges}[range_head];
      if (device_time < range.device_begin) {
        break;
      }
      const auto played = std::min(static_cast<double>(range.frames),
                                   std::max(0.0, device_time - range.device_begin));
      if (range.kind != RangeKind::maintenance) {
        current.media_seconds = range.media_begin + played / kRate;
      }
      if (range.kind == RangeKind::pcm) {
        current.played_frames = range.pcm_begin + static_cast<std::uint64_t>(played);
      }
      if (played < static_cast<double>(range.frames)) {
        break;
      }
      range_head = (range_head + 1) % ranges.size();
      --range_count;
    }
  }
  void observe() noexcept {
    if (!started || current.failure_code != 0) {
      return;
    }
    AudioTimeStamp stamp{};
    Boolean discontinuity = false;
    const auto result = AudioQueueGetCurrentTime(queue, timeline, &stamp, &discontinuity);
    if (result == kAudioQueueErr_InvalidRunState) {
      return;
    }
    if (!status(result)) {
      return;
    }
    if (discontinuity || (stamp.mFlags & kAudioTimeStampSampleTimeValid) == 0 ||
        !std::isfinite(stamp.mSampleTime)) {
      current.failure_code = kClockFailure;
      return;
    }
    const auto device_time = std::max(0.0, stamp.mSampleTime);
    if (have_device_time && device_time < last_device_time) {
      current.failure_code = kClockFailure;
      return;
    }
    last_device_time = device_time;
    have_device_time = true;
    current.valid = true;
    retire(last_device_time);
  }
  void fill(AudioQueueBufferRef buffer) noexcept {
    if (current.failure_code != 0) {
      return;
    }
    // Timestamped PCM segments may be shorter than an output block.
    std::array<Range, kMappings> pieces{};
    std::size_t piece_count = 0;
    std::size_t filled = 0;
    auto samples = std::span(static_cast<float*>(buffer->mAudioData), kBlockFrames * kChannels);
    auto add = [&](std::size_t count, RangeKind kind) {
      std::span{pieces}[piece_count++] = {.device_begin = static_cast<double>(filled),
                                          .media_begin = next_media,
                                          .frames = count,
                                          .pcm_begin = current.submitted_frames,
                                          .kind = kind};
      filled += count;
      if (kind != RangeKind::maintenance) {
        next_media += static_cast<double>(count) / kRate;
      }
      if (kind == RangeKind::pcm) {
        current.submitted_frames += count;
      } else if (kind == RangeKind::initial) {
        current.initial_silence_frames += count;
      } else {
        current.maintenance_frames += count;
      }
    };
    if (leading_frames != 0) {
      const auto count = std::min(leading_frames, kBlockFrames);
      std::fill_n(samples.begin(), count * kChannels, 0.0F);
      leading_frames -= count;
      add(count, RangeKind::initial);
    }
    if (current.buffering &&
        (source.buffered_audio_frames() >= prime_frames || source.audio_ended())) {
      current.buffering = false;
    }
    while (filled < kBlockFrames && !ending && !current.buffering) {
      if (piece_count == pieces.size()) {
        current.failure_code = kMappingFailure;
        return;
      }
      const auto read = source.read_audio(samples.subspan(filled * kChannels));
      if (read.state == video::DecodeState::failed || read.state == video::DecodeState::stopped) {
        current.failure_code = kSourceFailure;
        return;
      }
      if (read.frame_count != 0) {
        if (read.frame_count > kBlockFrames - filled || !std::isfinite(read.timestamp_seconds) ||
            read.timestamp_seconds < 0) {
          current.failure_code = kSourceFailure;
          return;
        }
        next_media = read.timestamp_seconds;
        add(read.frame_count, RangeKind::pcm);
      }
      if (read.state == video::DecodeState::end ||
          (source.audio_ended() && source.buffered_audio_frames() == 0)) {
        ending = true;
      } else if (read.state == video::DecodeState::pending) {
        current.buffering = true;
        ++current.underruns;
      } else if (read.frame_count == 0) {
        current.failure_code = kSourceFailure;
        return;
      }
    }
    if (filled < kBlockFrames && !ending) {
      if (piece_count == pieces.size()) {
        current.failure_code = kMappingFailure;
        return;
      }
      const auto count = kBlockFrames - filled;
      std::fill(samples.begin() + static_cast<std::ptrdiff_t>(filled * kChannels), samples.end(),
                0.0F);
      add(count, RangeKind::maintenance);
    }
    if (filled == 0) {
      return;
    }
    if (piece_count > ranges.size() - range_count) {
      current.failure_code = kMappingFailure;
      return;
    }
    buffer->mAudioDataByteSize = static_cast<UInt32>(filled * kChannels * sizeof(float));
    AudioTimeStamp actual{};
    if (!status(AudioQueueEnqueueBufferWithParameters(queue, buffer, 0, nullptr, 0, 0, 0, nullptr,
                                                      nullptr, &actual))) {
      return;
    }
    if ((actual.mFlags & kAudioTimeStampSampleTimeValid) == 0 ||
        !std::isfinite(actual.mSampleTime)) {
      current.failure_code = kClockFailure;
      return;
    }
    if (have_device_time && actual.mSampleTime + 1 < last_device_time) {
      current.failure_code = kNativeGap;
      return;
    }
    for (std::size_t index = 0; index < piece_count; ++index) {
      auto piece = std::span{pieces}[index];
      piece.device_begin += actual.mSampleTime;
      std::span{ranges}[(range_head + range_count) % ranges.size()] = piece;
      ++range_count;
    }
  }
  bool prepare() noexcept {
    const auto origin = source.metadata().audio_start_seconds;
    if (!std::isfinite(origin) || origin < 0 || origin > kMaximumOriginSeconds) {
      current.failure_code = kSourceFailure;
      return false;
    }
    leading_frames = static_cast<std::size_t>(std::llround(origin * kRate));
    AudioStreamBasicDescription format{};
    format.mSampleRate = kRate;
    format.mFormatID = kAudioFormatLinearPCM;
    format.mFormatFlags = kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked;
    format.mBytesPerPacket = format.mBytesPerFrame = kChannels * sizeof(float);
    format.mFramesPerPacket = 1;
    format.mChannelsPerFrame = kChannels;
    format.mBitsPerChannel = sizeof(float) * CHAR_BIT;
    if (!status(AudioQueueNewOutput(&format, callback, this, CFRunLoopGetCurrent(),
                                    kCFRunLoopDefaultMode, 0, &queue)) ||
        !status(AudioQueueCreateTimeline(queue, &timeline))) {
      return false;
    }
    for (auto& buffer : buffers) {
      if (!status(
              AudioQueueAllocateBuffer(queue, kBlockFrames * kChannels * sizeof(float), &buffer))) {
        return false;
      }
      if (!ending) {
        fill(buffer);
      }
    }
    return current.failure_code == 0;
  }
  void run() noexcept {
    const bool prepared = prepare();
    published.store(current);
    initialized.store(true);
    initialized.notify_one();
    if (prepared) {
      while (!stop_requested.load() && current.failure_code == 0 && !current.drained) {
        if (start_requested.load() && !started) {
          started = status(AudioQueueStart(queue, nullptr));
        }
        CFRunLoopRunInMode(kCFRunLoopDefaultMode, kServicePeriod, true);
        observe();
        if (ending && started && !drain_requested) {
          drain_requested = status(AudioQueueStop(queue, false));
        }
        if (drain_requested) {
          UInt32 running = 1;
          UInt32 size = sizeof(running);
          if (status(
                  AudioQueueGetProperty(queue, kAudioQueueProperty_IsRunning, &running, &size)) &&
              running == 0) {
            current.drained = current.valid = true;
            current.media_seconds = next_media;
            current.played_frames = current.submitted_frames;
          }
        }
        published.store(current);
      }
    }
    if (queue != nullptr) {
      (void)AudioQueueStop(queue, true);
      // AudioQueueDispose also disposes its associated timeline (Apple contract).
      (void)AudioQueueDispose(queue, true);
      queue = nullptr;
    }
    published.store(current);
  }
};
MediaAudio::MediaAudio(const std::shared_ptr<memory::Domain>& domain)
    : allocator_(domain ? memory::Allocator<std::byte>(domain) : memory::Allocator<std::byte>{}) {}
MediaAudio::~MediaAudio() = default;
void MediaAudio::prepare(video::Decoder& source, std::size_t prime_frames) {
  stop();
  if (prime_frames == 0 || !source.metadata().has_audio) {
    throw std::invalid_argument("Audio preparation requires a source and prime target");
  }
  auto prepared = std::allocate_shared<State>(allocator_, source, prime_frames);
  if (prepared->published.load().failure_code != 0) {
    throw std::runtime_error("Native audio preparation failed: " +
                             std::to_string(prepared->published.load().failure_code));
  }
  state_ = std::move(prepared);
}
void MediaAudio::start() {
  if (state_) {
    state_->start_requested.store(true);
  }
}
video::AudioSnapshot MediaAudio::snapshot() const {
  return state_ ? state_->published.load() : video::AudioSnapshot{};
}
void MediaAudio::stop() noexcept { state_.reset(); }
}  // namespace bw::platform
