// Backend selection: C.129 (#rh-kind) keeps factories off the Decoder
// interface; E.6 (#re-raii) and performance COPY.5 (guidelines/copy-move/
// COPY.5-resource-owning-types-are-move-only.md) retain unique session cleanup.
// Asynchronous ASF movie backend. One demux owner routes bounded compressed
// packets; independent video and audio owners decode/convert and drain their
// streams. Open establishes immutable stream metadata before workers start.
// Video moves owned BGRA frames to the control thread; the native audio
// consumer copies timestamped prepared PCM through a fixed SPSC ring without
// codec work, allocation, destruction, logging or queue locks. Retire output
// before stop. Algorithm: acquire/probe local input; reserve demux budget;
// route packets; independently receive/send each codec; reserve video output
// before conversion or publish PCM segments; drain codecs/resampler after
// ordered input exhaustion. Capacity waits are cancellable. An exhausted
// compressed budget with a required input stream blocked behind full output
// fails explicitly. Stop joins owners. Cost: fixed packet descriptor arrays,
// three bounded video reservations and a fixed PCM/segment ring; conversion is
// O(pixels/samples) with worker allocations. FFmpeg
// probe/parser/reference/scratch memory remains separately managed, not falsely
// covered by engine payload counters. Caller-retained video is external.
// Reference algorithms: ffplay n7.1.1 separate packet/codec owners; Unreal
// MediaSamples separate output queues; Godot AudioRBResampler PCM handoff.
// Design MCP: CP.26 owns joined workers, COPY.2 (guidelines/copy-move/
// COPY.2-mark-move-constructors-noexcept.md) moves payload ownership.
// Implementation MCP: CP.42 (#rconc-wait), CP.43 (#rconc-time) protect
// predicate waits and keep codec/conversion work outside locks; CONC.5
// (guidelines/ concurrency/CONC.5-spsc-no-cas-needed.md), MEM.9
// (guidelines/memory/ MEM.9-embedded-allocate-at-init.md) keep PCM callback
// storage fixed and SPSC. See docs/video-decoder.md for exact limits, PTS
// provenance and verification.
#include "decoder.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstddef>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>

#include "video/pcm_buffer.hpp"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/error.h>
#include <libavutil/opt.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
}

namespace bw::video {
namespace {
constexpr int kMaxDimension = 4096;
constexpr std::int64_t kMaxPixels = 4096LL * 2160;
constexpr int kMaxAudioSamples = 65536;
constexpr int kAudioRate = static_cast<int>(kAudioSampleRate);
constexpr int kAudioChannels = static_cast<int>(kAudioChannelCount);
constexpr int kMaxPacketsWithoutOutput = 4096;
constexpr int kMaxInputAudioRate = 192000;
constexpr int kMaxInputChannels = 8;
constexpr std::int64_t kProbeBytes = 4LL * 1024 * 1024;
constexpr std::int64_t kProbeDuration = 2LL * AV_TIME_BASE;

void checked(int code, const char* operation) {
  if (code >= 0) {
    return;
  }
  std::array<char, AV_ERROR_MAX_STRING_SIZE> message{};
  av_strerror(code, message.data(), message.size());
  throw std::runtime_error(std::string(operation) + ": " + message.data());
}
struct FormatDelete {
  void operator()(AVFormatContext* value) const {
    avformat_close_input(&value);
  }
};
struct CodecDelete {
  void operator()(AVCodecContext* value) const { avcodec_free_context(&value); }
};
struct FrameDelete {
  void operator()(AVFrame* value) const { av_frame_free(&value); }
};
struct PacketDelete {
  void operator()(AVPacket* value) const { av_packet_free(&value); }
};
struct ScaleDelete {
  void operator()(SwsContext* value) const { sws_freeContext(value); }
};
struct ResampleDelete {
  void operator()(SwrContext* value) const { swr_free(&value); }
};
using Format = std::unique_ptr<AVFormatContext, FormatDelete>;
using Codec = std::unique_ptr<AVCodecContext, CodecDelete>;
using Frame = std::unique_ptr<AVFrame, FrameDelete>;
using Packet = std::unique_ptr<AVPacket, PacketDelete>;
using Scale = std::unique_ptr<SwsContext, ScaleDelete>;
using Resample = std::unique_ptr<SwrContext, ResampleDelete>;

void validate_video(int width, int height) {
  if (width <= 0 || height <= 0 || width > kMaxDimension ||
      height > kMaxDimension ||
      static_cast<std::int64_t>(width) * height > kMaxPixels) {
    throw std::runtime_error("movie video dimensions exceed supported bounds");
  }
}
Codec open_codec(const AVStream& stream, bool video) {
  const auto& parameters = *stream.codecpar;
  if (video) {
    if (parameters.codec_id != AV_CODEC_ID_WMV3 &&
        parameters.codec_id != AV_CODEC_ID_VC1) {
      throw std::runtime_error("movie requires WMV3 or VC1 video");
    }
    validate_video(parameters.width, parameters.height);
  } else if (parameters.codec_id != AV_CODEC_ID_WMAPRO ||
             parameters.sample_rate <= 0 ||
             parameters.sample_rate > kMaxInputAudioRate ||
             parameters.ch_layout.nb_channels <= 0 ||
             parameters.ch_layout.nb_channels > kMaxInputChannels) {
    throw std::runtime_error("movie audio exceeds supported WMAPro bounds");
  }
  const auto* implementation = avcodec_find_decoder(parameters.codec_id);
  Codec codec(avcodec_alloc_context3(implementation));
  if (!implementation || !codec) {
    throw std::runtime_error("movie codec is unavailable");
  }
  checked(avcodec_parameters_to_context(codec.get(), &parameters),
          "copy movie codec parameters");
  codec->max_pixels = kMaxPixels;
  codec->pkt_timebase = stream.time_base;
  codec->thread_count = 1;
  codec->err_recognition = AV_EF_EXPLODE;
  checked(avcodec_open2(codec.get(), implementation, nullptr),
          "open movie codec");
  return codec;
}

struct AudioChunk {
  double timestamp_seconds = 0;
  std::vector<float> samples;
};
// ffplay AV_NOSYNC_THRESHOLD: discontinuities larger than ten seconds are not
// treated as ordinary A/V skew. This non-seeking backend rejects them rather
// than guessing a new clock epoch. Smaller explicit PTS remain authoritative.
constexpr double kMaximumTimestampDiscontinuity = 10.0;
constexpr auto kProducerPollInterval = std::chrono::milliseconds(2);
constexpr std::size_t kPacketReadAhead = 25;
class CodecStream {
 public:
  CodecStream(const AVStream& stream, bool video, double origin)
      : codec_(open_codec(stream, video)),
        frame_(av_frame_alloc()),
        time_base_(stream.time_base),
        frame_rate_(av_q2d(stream.avg_frame_rate)),
        origin_seconds_(origin) {
    if (!frame_) {
      throw std::runtime_error("cannot allocate codec frame");
    }
    video_width_ = codec_->width;
    video_height_ = codec_->height;
    const double start =
        stream.start_time == AV_NOPTS_VALUE
            ? 0
            : static_cast<double>(stream.start_time) * av_q2d(time_base_) -
                  origin;
    if (start < 0 || !std::isfinite(start)) {
      throw std::runtime_error(
          "unsupported negative or invalid movie stream origin");
    }
    video_end_seconds_ = audio_input_end_seconds_ = audio_end_seconds_ = start;
  }
  int receive() { return avcodec_receive_frame(codec_.get(), frame_.get()); }
  void send(const AVPacket* packet) {
    checked(avcodec_send_packet(codec_.get(), packet), "submit movie packet");
  }
  void release_frame() { av_frame_unref(frame_.get()); }
  double start() const { return video_end_seconds_; }
  std::uint64_t anchors() const { return anchors_; }
  double anchor_delta() const { return last_anchor_delta_; }
  double max_anchor_delta() const { return max_anchor_delta_; }
  double timestamp(bool audio, double fallback) const {
    const auto stamp = audio ? frame_->pts : frame_->best_effort_timestamp;
    return stamp == AV_NOPTS_VALUE
               ? fallback
               : static_cast<double>(stamp) * av_q2d(time_base_) -
                     origin_seconds_;
  }
  VideoFrame convert_video() {
    validate_video(frame_->width, frame_->height);
    if (frame_->width != video_width_ || frame_->height != video_height_) {
      throw std::runtime_error("movie video dimensions changed");
    }
    if (!scale_) {
      scale_.reset(sws_getContext(frame_->width, frame_->height,
                                  static_cast<AVPixelFormat>(frame_->format),
                                  frame_->width, frame_->height,
                                  AV_PIX_FMT_BGRA, SWS_BILINEAR, nullptr,
                                  nullptr, nullptr));
      video_format_ = frame_->format;
      if (!scale_) {
        throw std::runtime_error("cannot create movie pixel conversion");
      }
    }
    if (frame_->format != video_format_) {
      throw std::runtime_error("movie video pixel format changed");
    }
    VideoFrame output;
    output.width = static_cast<std::uint32_t>(frame_->width);
    output.height = static_cast<std::uint32_t>(frame_->height);
    output.timestamp_seconds = timestamp(false, video_end_seconds_);
    if (output.timestamp_seconds < 0 ||
        !std::isfinite(output.timestamp_seconds)) {
      throw std::runtime_error(
          "unsupported negative or invalid movie video timestamp");
    }
    const double frame_rate = frame_rate_;
    output.duration_seconds =
        frame_->duration > 0
            ? static_cast<double>(frame_->duration) * av_q2d(time_base_)
            : (frame_rate > 0 ? 1.0 / frame_rate : 0);
    if (output.duration_seconds <= 0 ||
        !std::isfinite(output.duration_seconds)) {
      throw std::runtime_error("movie has no usable frame duration");
    }
    video_end_seconds_ = output.timestamp_seconds + output.duration_seconds;
    output.bgra.resize(static_cast<std::size_t>(output.width) * output.height *
                       4);
    std::array<std::uint8_t*, 4> destination{output.bgra.data(), nullptr,
                                             nullptr, nullptr};
    std::array<int, 4> strides{frame_->width * 4, 0, 0, 0};
    checked(sws_scale(scale_.get(), std::data(frame_->data),
                      std::data(frame_->linesize), 0, frame_->height,
                      destination.data(), strides.data()),
            "convert movie pixels");
    return output;
  }
  AudioChunk convert_audio() {
    if (frame_->nb_samples <= 0 || frame_->nb_samples > kMaxAudioSamples ||
        frame_->sample_rate != codec_->sample_rate ||
        av_channel_layout_compare(&frame_->ch_layout, &codec_->ch_layout) !=
            0) {
      throw std::runtime_error(
          "movie audio parameters changed or exceed bounds");
    }
    if (!resample_) {
      AVChannelLayout stereo = AV_CHANNEL_LAYOUT_STEREO;
      SwrContext* context = nullptr;
      const int allocated = swr_alloc_set_opts2(
          &context, &stereo, AV_SAMPLE_FMT_FLT, kAudioRate, &frame_->ch_layout,
          static_cast<AVSampleFormat>(frame_->format), frame_->sample_rate, 0,
          nullptr);
      resample_.reset(context);
      checked(allocated, "allocate movie audio conversion");
      checked(swr_init(resample_.get()), "initialize movie audio conversion");
      audio_format_ = frame_->format;
    }
    if (frame_->format != audio_format_) {
      throw std::runtime_error("movie audio sample format changed");
    }
    const auto delay = swr_get_delay(resample_.get(), frame_->sample_rate);
    const double input_start = timestamp(true, audio_input_end_seconds_);
    if (frame_->pts != AV_NOPTS_VALUE) {
      const double error = input_start - audio_input_end_seconds_;
      if (std::abs(error) > kMaximumTimestampDiscontinuity) {
        throw std::runtime_error(
            "unsupported movie audio timestamp discontinuity");
      }
      ++anchors_;
      last_anchor_delta_ = error;
      max_anchor_delta_ = std::max(max_anchor_delta_, std::abs(error));
    }
    audio_input_end_seconds_ =
        input_start +
        static_cast<double>(frame_->nb_samples) / frame_->sample_rate;
    const double start =
        input_start - static_cast<double>(delay) / frame_->sample_rate;
    return resample_audio(frame_->nb_samples, frame_->extended_data, start);
  }
  AudioChunk resample_audio(int count, std::uint8_t* const* input,
                            double start) {
    const int capacity = swr_get_out_samples(resample_.get(), count);
    checked(capacity, "size movie audio conversion");
    if (capacity > kMaxAudioSamples) {
      throw std::runtime_error(
          "movie audio conversion exceeds supported bounds");
    }
    AudioChunk output;
    if (start < 0 || !std::isfinite(start)) {
      throw std::runtime_error(
          "unsupported negative or invalid movie audio origin");
    }
    output.timestamp_seconds = start;
    output.samples.resize(static_cast<std::size_t>(capacity) * kAudioChannels);
    // FFmpeg addresses packed float samples through a byte pointer. Storage is
    // float-aligned and sized above for capacity stereo frames.
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    auto* destination = reinterpret_cast<std::uint8_t*>(output.samples.data());
    // C's const-pointer API requires a read-only view of its own plane table.
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-const-cast)
    const auto* const* source = const_cast<const std::uint8_t* const*>(input);
    const int produced =
        swr_convert(resample_.get(), &destination, capacity, source, count);
    checked(produced, "convert movie audio");
    output.samples.resize(static_cast<std::size_t>(produced) * kAudioChannels);
    audio_end_seconds_ = start + static_cast<double>(produced) / kAudioRate;
    return output;
  }
  std::optional<AudioChunk> drain_audio() {
    if (!resample_ || resampler_eof_) {
      return std::nullopt;
    }
    auto output = resample_audio(0, nullptr, audio_end_seconds_);
    if (output.samples.empty()) {
      resampler_eof_ = true;
      return std::nullopt;
    }
    return output;
  }

 private:
  Codec codec_;
  Frame frame_;
  Scale scale_;
  Resample resample_;
  AVRational time_base_{};
  double frame_rate_ = 0;
  int video_format_ = -1, audio_format_ = -1;
  int video_width_ = 0, video_height_ = 0;
  double origin_seconds_ = 0, video_end_seconds_ = 0;
  double audio_input_end_seconds_ = 0, audio_end_seconds_ = 0;
  bool resampler_eof_ = false;
  std::uint64_t anchors_ = 0;
  double last_anchor_delta_ = 0, max_anchor_delta_ = 0;
};
struct CodecProgress {
  bool draining = false;
  int without_output = 0;
};
struct PacketEntry {
  Packet packet;
  std::size_t bytes = 0;
};
struct PacketQueue {
  explicit PacketQueue(std::size_t capacity) : slots(capacity) {}
  std::vector<PacketEntry> slots;
  std::size_t head = 0, count = 0, bytes = 0, in_flight_bytes = 0;
  bool waiting_input = false, waiting_output = false;
};
DecoderLimits valid_limits(DecoderLimits limits) {
  const DecoderLimits maximum;
  if (limits.compressed_bytes == 0 ||
      limits.compressed_bytes > maximum.compressed_bytes ||
      limits.stream_compressed_bytes == 0 ||
      limits.stream_compressed_bytes > limits.compressed_bytes ||
      limits.compressed_packets == 0 ||
      limits.compressed_packets > maximum.compressed_packets ||
      limits.stream_compressed_packets == 0 ||
      limits.stream_compressed_packets > limits.compressed_packets ||
      limits.single_packet_bytes == 0 ||
      limits.single_packet_bytes > limits.stream_compressed_bytes ||
      limits.video_slots == 0 || limits.video_slots > maximum.video_slots ||
      limits.video_bytes == 0 || limits.video_bytes > maximum.video_bytes ||
      limits.audio_frames == 0 || limits.audio_frames > maximum.audio_frames) {
    throw std::invalid_argument(
        "movie limits exceed the supported resource profile");
  }
  return limits;
}
std::size_t packet_bytes(const AVPacket& packet, std::size_t limit) {
  if (packet.size < 0 || packet.side_data_elems < 0) {
    throw std::runtime_error("invalid compressed movie packet");
  }
  std::size_t bytes = sizeof(AVPacket) + sizeof(PacketEntry);
  const auto add = [&](std::size_t amount) {
    if (bytes > limit || amount > limit - bytes) {
      throw std::runtime_error(
          "movie packet exceeds single-packet byte budget");
    }
    bytes += amount;
  };
  add(packet.buf ? packet.buf->size : static_cast<std::size_t>(packet.size));
  for (int index = 0; index < packet.side_data_elems; ++index) {
    add(sizeof(AVPacketSideData));
    // FFmpeg owns the side-data table and supplies its validated element count.
    // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic)
    add(packet.side_data[index].size);
  }
  return bytes;
}
class FfmpegDecoder final : public Decoder {
 public:
  FfmpegDecoder(const std::filesystem::path& path, DecoderLimits limits)
      : limits_(valid_limits(limits)),
        pcm_(limits_.audio_frames),
        video_packets_(limits_.stream_compressed_packets),
        audio_packets_(limits_.stream_compressed_packets),
        video_output_(limits_.video_slots) {
    if (!std::filesystem::is_regular_file(path)) {
      throw std::runtime_error("movie input must be a local regular file");
    }
    Format format(avformat_alloc_context());
    if (!format) {
      throw std::runtime_error("cannot allocate movie demuxer");
    }
    format->probesize = kProbeBytes;
    format->max_analyze_duration = kProbeDuration;
    format->interrupt_callback = {interrupt, this};
    checked(av_opt_set_int(format.get(), "max_streams", 2, 0),
            "bound movie stream count");
    const auto local_path = std::filesystem::absolute(path).string();
    auto* context = format.release();
    const int opened = avformat_open_input(
        &context, local_path.c_str(), av_find_input_format("asf"), nullptr);
    format.reset(context);
    checked(opened, "open movie file");
    checked(avformat_find_stream_info(format.get(), nullptr), "probe movie");
    video_index_ = av_find_best_stream(format.get(), AVMEDIA_TYPE_VIDEO, -1, -1,
                                       nullptr, 0);
    checked(video_index_, "find movie video stream");
    audio_index_ = av_find_best_stream(format.get(), AVMEDIA_TYPE_AUDIO, -1, -1,
                                       nullptr, 0);
    if (audio_index_ < 0 && audio_index_ != AVERROR_STREAM_NOT_FOUND) {
      checked(audio_index_, "find movie audio stream");
    }
    const double origin =
        format->start_time == AV_NOPTS_VALUE
            ? 0
            : static_cast<double>(format->start_time) / AV_TIME_BASE;
    // av_find_best_stream validates indices into FFmpeg's owned stream table.
    // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic)
    const auto& video_stream = *format->streams[video_index_];
    auto video = std::make_unique<CodecStream>(video_stream, true, origin);
    metadata_.width = static_cast<std::uint32_t>(video_stream.codecpar->width);
    metadata_.height =
        static_cast<std::uint32_t>(video_stream.codecpar->height);
    metadata_.video_start_seconds = video->start();
    frame_bytes_ =
        static_cast<std::size_t>(metadata_.width) * metadata_.height * 4;
    if (frame_bytes_ > limits_.video_bytes) {
      throw std::runtime_error("movie frame exceeds decoded video byte budget");
    }
    metadata_.has_audio = audio_index_ >= 0;
    std::unique_ptr<CodecStream> audio;
    if (metadata_.has_audio) {
      // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic)
      audio = std::make_unique<CodecStream>(*format->streams[audio_index_],
                                            false, origin);
      metadata_.audio_start_seconds = audio->start();
    } else {
      audio_finished_.store(true, std::memory_order_release);
    }
    try {
      video_worker_ = std::thread([this, stream = std::move(video)]() mutable {
        guarded([&] { run_video(*stream); });
        stream.reset();
      });
      if (audio) {
        audio_worker_ =
            std::thread([this, stream = std::move(audio)]() mutable {
              guarded([&] { run_audio(*stream); });
              stream.reset();
            });
      }
      demux_worker_ = std::thread([this, input = std::move(format)]() mutable {
        guarded([&] { run_demux(*input); });
        input.reset();
      });
    } catch (...) {
      stop();
      throw;
    }
  }
  ~FfmpegDecoder() override { stop(); }
  FfmpegDecoder(const FfmpegDecoder&) = delete;
  FfmpegDecoder& operator=(const FfmpegDecoder&) = delete;
  FfmpegDecoder(FfmpegDecoder&&) = delete;
  FfmpegDecoder& operator=(FfmpegDecoder&&) = delete;
  const DecoderMetadata& metadata() const noexcept override {
    return metadata_;
  }
  VideoPoll poll_video() override {
    VideoPoll result;
    {
      const std::lock_guard lock(mutex_);
      if (failure_) {
        return {DecodeState::failed, {}, failure_};
      }
      if (stopping_.load(std::memory_order_acquire)) {
        return {DecodeState::stopped, {}, {}};
      }
      if (video_count_ == 0) {
        return {
            video_finished_ ? DecodeState::end : DecodeState::pending, {}, {}};
      }
      result = {DecodeState::sample, std::move(video_output_[video_head_]), {}};
      video_output_[video_head_].reset();
      video_head_ = (video_head_ + 1) % video_output_.size();
      --video_count_;
    }
    changed_.notify_all();
    return result;
  }
  AudioRead read_audio(std::span<float> destination) noexcept override {
    const auto terminal = terminal_.load(std::memory_order_acquire);
    if (terminal == DecodeState::failed || terminal == DecodeState::stopped) {
      return {terminal, 0, 0};
    }
    // Acquire completion before reading; if done was observed, all final writes
    // precede the read. Observing done after an empty read could skip a tail.
    const bool ended = audio_finished_.load(std::memory_order_acquire);
    const auto read = pcm_.read(destination);
    if (read.frames != 0) {
      return {DecodeState::sample, read.frames, read.timestamp_seconds};
    }
    return {ended && pcm_.available() == 0 ? DecodeState::end
                                           : DecodeState::pending,
            0, 0};
  }
  std::size_t buffered_audio_frames() const noexcept override {
    return pcm_.available();
  }
  bool audio_ended() const noexcept override {
    return audio_finished_.load(std::memory_order_acquire);
  }
  void wait_until_ready(std::size_t audio_frames) override {
    if (audio_frames > pcm_.capacity()) {
      throw std::invalid_argument("movie prime target exceeds PCM capacity");
    }
    std::unique_lock lock(mutex_);
    prime_target_ = audio_frames;
    priming_ = true;
    changed_.notify_all();
    changed_.wait(lock, [&] {
      return stopping_.load(std::memory_order_acquire) ||
             ((video_count_ != 0 || video_finished_) &&
              (!metadata_.has_audio || audio_ended() ||
               pcm_.available() >= audio_frames));
    });
    priming_ = false;
    if (failure_) {
      std::rethrow_exception(failure_);
    }
    if (stopping_.load(std::memory_order_acquire)) {
      throw std::runtime_error("movie priming was cancelled");
    }
  }
  std::exception_ptr failure() const override {
    const std::lock_guard lock(mutex_);
    return failure_;
  }
  DecoderDiagnostics diagnostics() const override {
    const std::lock_guard lock(mutex_);
    return diagnostics_;
  }
  void stop() noexcept override {
    stopping_.store(true, std::memory_order_release);
    terminal_.store(DecodeState::stopped, std::memory_order_release);
    changed_.notify_all();
    if (demux_worker_.joinable()) {
      demux_worker_.join();
    }
    if (video_worker_.joinable()) {
      video_worker_.join();
    }
    if (audio_worker_.joinable()) {
      audio_worker_.join();
    }
  }

 private:
  static int interrupt(void* opaque) {
    const auto* owner = static_cast<FfmpegDecoder*>(opaque);
    return owner->stopping_.load(std::memory_order_acquire) ? 1 : 0;
  }
  template <typename Work>
  void guarded(Work&& work) noexcept {
    try {
      std::forward<Work>(work)();
    } catch (...) {
      {
        const std::lock_guard lock(mutex_);
        if (!stopping_.load(std::memory_order_acquire)) {
          failure_ = std::current_exception();
          terminal_.store(DecodeState::failed, std::memory_order_release);
          stopping_.store(true, std::memory_order_release);
        }
      }
      changed_.notify_all();
    }
  }
  std::size_t queued_bytes() const {
    return video_packets_.bytes + audio_packets_.bytes +
           video_packets_.in_flight_bytes + audio_packets_.in_flight_bytes;
  }
  std::size_t queued_packets() const {
    return video_packets_.count + audio_packets_.count +
           (video_packets_.in_flight_bytes != 0 ? 1U : 0U) +
           (audio_packets_.in_flight_bytes != 0 ? 1U : 0U);
  }
  void reject_stall() const {
    const auto audio_needed = priming_ ? prime_target_ : std::size_t{1};
    const bool video_full =
        video_count_ == video_output_.size() ||
        frame_bytes_ > limits_.video_bytes - video_count_ * frame_bytes_;
    if ((video_packets_.waiting_output && video_full &&
         audio_packets_.waiting_input && audio_packets_.count == 0 &&
         pcm_.available() < audio_needed) ||
        (priming_ && audio_packets_.waiting_output && !pcm_.writable() &&
         video_packets_.waiting_input && video_packets_.count == 0 &&
         video_count_ == 0)) {
      throw std::runtime_error(
          "movie interleave_budget_exhausted: compressed capacity blocks a "
          "required stream");
    }
  }
  void wait_demux_capacity(std::unique_lock<std::mutex>& lock,
                           const PacketQueue* stream, std::size_t bytes) {
    const auto enough = [&] {
      if (queued_packets() >= limits_.compressed_packets ||
          bytes > limits_.compressed_bytes - queued_bytes()) {
        return false;
      }
      return !stream ||
             (stream->count + (stream->in_flight_bytes != 0 ? 1U : 0U) <
                  limits_.stream_compressed_packets &&
              bytes <= limits_.stream_compressed_bytes - stream->bytes -
                           stream->in_flight_bytes);
    };
    while (!stopping_.load(std::memory_order_acquire) && !enough()) {
      reject_stall();
      changed_.wait_for(lock, kProducerPollInterval, [&] {
        return stopping_.load(std::memory_order_acquire) || enough();
      });
    }
  }
  void run_demux(AVFormatContext& format) {
    for (;;) {
      {
        std::unique_lock lock(mutex_);
        // Reserve one worst-case packet before invoking a demuxer allocation.
        // Internal probe/parser storage remains separately documented FFmpeg
        // memory.
        wait_demux_capacity(lock, nullptr, limits_.single_packet_bytes);
        changed_.wait(lock, [&] {
          return stopping_.load(std::memory_order_acquire) ||
                 video_packets_.count < kPacketReadAhead ||
                 (metadata_.has_audio &&
                  audio_packets_.count < kPacketReadAhead);
        });
        if (stopping_.load(std::memory_order_acquire)) {
          return;
        }
      }
      Packet packet(av_packet_alloc());
      if (!packet) {
        throw std::runtime_error("cannot allocate movie packet");
      }
      const int read = av_read_frame(&format, packet.get());
      if (read == AVERROR_EOF) {
        {
          const std::lock_guard lock(mutex_);
          demux_finished_ = true;
        }
        changed_.notify_all();
        return;
      }
      checked(read, "read movie packet");
      const auto bytes = packet_bytes(*packet, limits_.single_packet_bytes);
      PacketQueue* queue =
          packet->stream_index == video_index_
              ? &video_packets_
              : (packet->stream_index == audio_index_ ? &audio_packets_
                                                      : nullptr);
      if (!queue) {
        continue;
      }
      {
        std::unique_lock lock(mutex_);
        diagnostics_.compressed_peak_bytes = std::max(
            diagnostics_.compressed_peak_bytes, queued_bytes() + bytes);
        diagnostics_.compressed_peak_packets = std::max(
            diagnostics_.compressed_peak_packets, queued_packets() + 1);
        wait_demux_capacity(lock, queue, bytes);
        if (stopping_.load(std::memory_order_acquire)) {
          return;
        }
        auto& slot =
            queue->slots[(queue->head + queue->count) % queue->slots.size()];
        slot = {std::move(packet), bytes};
        ++queue->count;
        queue->bytes += bytes;
      }
      changed_.notify_all();
    }
  }
  Packet next_packet(PacketQueue& queue) {
    PacketEntry entry;
    {
      std::unique_lock lock(mutex_);
      queue.waiting_input = queue.count == 0 && !demux_finished_;
      changed_.notify_all();
      changed_.wait(lock, [&] {
        return stopping_.load(std::memory_order_acquire) || demux_finished_ ||
               queue.count != 0;
      });
      queue.waiting_input = false;
      if (stopping_.load(std::memory_order_acquire) || queue.count == 0) {
        return {};
      }
      auto& slot = queue.slots[queue.head];
      entry = {std::move(slot.packet), std::exchange(slot.bytes, 0)};
      queue.head = (queue.head + 1) % queue.slots.size();
      --queue.count;
      queue.bytes -= entry.bytes;
      queue.in_flight_bytes = entry.bytes;
    }
    changed_.notify_all();
    return std::move(entry.packet);
  }
  bool advance_codec(CodecStream& stream, PacketQueue& queue,
                     CodecProgress& progress) {
    if (progress.draining) {
      throw std::runtime_error("movie codec failed to drain");
    }
    if (++progress.without_output > kMaxPacketsWithoutOutput) {
      throw std::runtime_error(
          "movie codec exceeded packet budget without output");
    }
    auto packet = next_packet(queue);
    if (stopping_.load(std::memory_order_acquire)) {
      return false;
    }
    progress.draining = !packet;
    stream.send(packet.get());
    packet.reset();
    {
      const std::lock_guard lock(mutex_);
      queue.in_flight_bytes = 0;
    }
    changed_.notify_all();
    return true;
  }
  void run_video(CodecStream& stream) {
    CodecProgress progress;
    while (!stopping_.load(std::memory_order_acquire)) {
      const int received = stream.receive();
      if (received == AVERROR_EOF) {
        {
          const std::lock_guard lock(mutex_);
          video_finished_ = true;
        }
        changed_.notify_all();
        return;
      }
      if (received == AVERROR(EAGAIN)) {
        if (!advance_codec(stream, video_packets_, progress)) {
          return;
        }
        continue;
      }
      checked(received, "decode movie video");
      progress.without_output = 0;
      {
        std::unique_lock lock(mutex_);
        video_packets_.waiting_output = true;
        changed_.notify_all();
        changed_.wait(lock, [&] {
          return stopping_.load(std::memory_order_acquire) ||
                 (video_count_ < video_output_.size() &&
                  frame_bytes_ <=
                      limits_.video_bytes - video_count_ * frame_bytes_);
        });
        video_packets_.waiting_output = false;
        if (stopping_.load(std::memory_order_acquire)) {
          return;
        }
        diagnostics_.video_peak_bytes = std::max(
            diagnostics_.video_peak_bytes, (video_count_ + 1) * frame_bytes_);
      }
      auto frame = stream.convert_video();
      stream.release_frame();
      {
        const std::lock_guard lock(mutex_);
        video_output_[(video_head_ + video_count_) % video_output_.size()] =
            std::move(frame);
        ++video_count_;
        ++diagnostics_.video_frames;
      }
      changed_.notify_all();
    }
  }
  bool publish_audio(const AudioChunk& chunk) {
    std::size_t offset = 0;
    const auto samples = std::span<const float>(chunk.samples);
    while (offset < samples.size()) {
      if (stopping_.load(std::memory_order_acquire)) {
        return false;
      }
      const auto written = pcm_.write(
          samples.subspan(offset),
          chunk.timestamp_seconds + static_cast<double>(offset) /
                                        kAudioChannelCount / kAudioSampleRate);
      offset += written * kAudioChannelCount;
      {
        std::unique_lock lock(mutex_);
        diagnostics_.audio_frames += written;
        audio_packets_.waiting_output = offset != samples.size();
        changed_.notify_all();
        if (audio_packets_.waiting_output) {
          // Native read changes only atomics. Periodic predicate recheck avoids
          // a lost-CV-wakeup dependency and keeps callbacks free of
          // notification.
          changed_.wait_for(lock, kProducerPollInterval, [&] {
            return stopping_.load(std::memory_order_acquire) || pcm_.writable();
          });
        }
      }
    }
    return true;
  }
  void run_audio(CodecStream& stream) {
    CodecProgress progress;
    while (!stopping_.load(std::memory_order_acquire)) {
      const int received = stream.receive();
      if (received == AVERROR_EOF) {
        while (auto tail = stream.drain_audio()) {
          if (!publish_audio(*tail)) {
            return;
          }
        }
        {
          const std::lock_guard lock(mutex_);
          audio_finished_.store(true, std::memory_order_release);
        }
        changed_.notify_all();
        return;
      }
      if (received == AVERROR(EAGAIN)) {
        if (!advance_codec(stream, audio_packets_, progress)) {
          return;
        }
        continue;
      }
      checked(received, "decode movie audio");
      progress.without_output = 0;
      auto chunk = stream.convert_audio();
      stream.release_frame();
      {
        const std::lock_guard lock(mutex_);
        diagnostics_.audio_timestamp_anchors = stream.anchors();
        diagnostics_.audio_anchor_bias_seconds = stream.anchor_delta();
        diagnostics_.audio_anchor_max_error_seconds = stream.max_anchor_delta();
      }
      if (!publish_audio(chunk)) {
        return;
      }
    }
  }
  const DecoderLimits limits_;
  DecoderMetadata metadata_;
  PcmBuffer pcm_;
  PacketQueue video_packets_, audio_packets_;
  std::vector<std::optional<VideoFrame>> video_output_;
  mutable std::mutex mutex_;
  std::condition_variable changed_;
  std::exception_ptr failure_;
  DecoderDiagnostics diagnostics_;
  std::atomic<bool> stopping_{false}, audio_finished_{false};
  std::atomic<DecodeState> terminal_{DecodeState::pending};
  std::size_t video_head_ = 0, video_count_ = 0, frame_bytes_ = 0;
  std::size_t prime_target_ = 0;
  bool priming_ = false, demux_finished_ = false, video_finished_ = false;
  int video_index_ = -1, audio_index_ = -1;
  std::thread demux_worker_, video_worker_, audio_worker_;
};
}  // namespace
std::unique_ptr<Decoder> ffmpeg::open_decoder(
    const std::filesystem::path& path) {
  return open_decoder_with_limits(path, {});
}
std::unique_ptr<Decoder> ffmpeg::open_decoder_with_limits(
    const std::filesystem::path& path, DecoderLimits limits) {
  return std::make_unique<FfmpegDecoder>(path, limits);
}
}  // namespace bw::video
