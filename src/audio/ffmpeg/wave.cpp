// XACT WMAv2 preparation using pinned FFmpeg's xWMA format contract.
// Decode once during loading; native callbacks read immutable frame ranges.
// E.6 (#re-raii) owns every C handle across failure; MEM.9 (see wave.hpp)
// bounds retained PCM before allocation. Both implementation corpora queried.
#include "bw/audio/wave.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <stdexcept>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/channel_layout.h>
#include <libavutil/mem.h>
#include <libswresample/swresample.h>
}

namespace bw::audio {
namespace {
constexpr std::array<std::uint32_t, 7> kAverageBytes{12000, 24000, 4000, 6000,
                                                     8000,  20000, 2500};
constexpr std::array<std::uint32_t, 17> kBlockBytes{
    929,  1487, 1280, 2230, 8917, 8192, 4459, 5945, 2304,
    1536, 1485, 1008, 2731, 4096, 6827, 5462, 1280};
constexpr unsigned kChannelShift = 2, kRateShift = 5, kBlockShift = 23;
constexpr std::uint32_t kChannelMask = 7, kRateMask = 0x3ffff;
constexpr std::uint32_t kWmaTag = 3, kProFlag = 0x80000000;
constexpr std::size_t kExtraBytes = 6, kExtraFlagOffset = 4;
constexpr std::uint8_t kExtraFlag = 31;
constexpr int kMaximumRate = 48000;
struct CodecDelete {
  void operator()(AVCodecContext* value) const { avcodec_free_context(&value); }
};
struct FrameDelete {
  void operator()(AVFrame* value) const { av_frame_free(&value); }
};
struct PacketDelete {
  void operator()(AVPacket* value) const { av_packet_free(&value); }
};
struct ResamplerDelete {
  void operator()(SwrContext* value) const { swr_free(&value); }
};
void checked(int result, const char* operation) {
  if (result < 0) throw std::runtime_error(operation);
}
std::uint32_t word(std::span<const std::uint8_t> bytes, std::size_t index) {
  constexpr std::size_t kWordBytes = 4;
  constexpr unsigned kByteBits = 8;
  if (index > bytes.size() || bytes.size() - index < kWordBytes) {
    throw std::invalid_argument("Truncated WMA seek entry");
  }
  std::uint32_t result = 0;
  for (unsigned byte = 0; byte < kWordBytes; ++byte) {
    result |= static_cast<std::uint32_t>(bytes[index + byte])
              << (byte * kByteBits);
  }
  return result;
}
}  // namespace

PreparedWave::PreparedWave(const std::shared_ptr<memory::Domain>& domain)
    : samples_(memory::Allocator<float>(domain)) {}

PreparedWave PreparedWave::decode(
    const formats::AudioWave& source, std::size_t maximum_samples,
    const std::shared_ptr<memory::Domain>& domain) {
  const auto channels = (source.format >> kChannelShift) & kChannelMask;
  const auto rate = (source.format >> kRateShift) & kRateMask;
  constexpr unsigned kAverageShift = 5;
  constexpr std::uint32_t kBlockIndexMask = 31, kPackedMask = 255;
  const auto packed = (source.format >> kBlockShift) & kPackedMask;
  const auto block_index = packed & kBlockIndexMask;
  const auto average_index = packed >> kAverageShift;
  if ((source.format & kWmaTag) != kWmaTag || (source.format & kProFlag) != 0 ||
      channels == 0 || channels > 2 || rate == 0 || rate > kMaximumRate ||
      block_index >= kBlockBytes.size() ||
      average_index >= kAverageBytes.size() || source.duration == 0 ||
      source.duration > maximum_samples / channels ||
      source.loop_start > source.duration ||
      source.loop_length > source.duration - source.loop_start) {
    throw std::invalid_argument("Unsupported or over-budget XACT WMAv2 wave");
  }
  const auto block = kBlockBytes.at(block_index);
  if (source.encoded.empty() || source.encoded.size() % block != 0) {
    throw std::invalid_argument("Truncated XACT WMAv2 packet data");
  }
  const auto packets = source.encoded.size() / block;
  constexpr std::size_t kSeekWord = 4;
  if (source.seek_entries.size() != packets * kSeekWord) {
    throw std::invalid_argument("WMA packet/seek count mismatch");
  }
  std::uint32_t previous = 0;
  for (std::size_t index = 0; index < packets; ++index) {
    const auto current = word(source.seek_entries, index * kSeekWord);
    if (current < previous || current % (channels * 2) != 0) {
      throw std::invalid_argument("Invalid WMA cumulative decoded bytes");
    }
    previous = current;
  }
  if (previous / (channels * 2) != source.duration) {
    throw std::invalid_argument("WMA duration/seek mismatch");
  }
  PreparedWave result(domain);
  result.channels_ = channels;
  result.sample_rate_ = rate;
  result.samples_.resize(static_cast<std::size_t>(source.duration) * channels);
  const auto* implementation = avcodec_find_decoder(AV_CODEC_ID_WMAV2);
  std::unique_ptr<AVCodecContext, CodecDelete> codec(
      implementation ? avcodec_alloc_context3(implementation) : nullptr);
  std::unique_ptr<AVFrame, FrameDelete> frame(av_frame_alloc());
  std::unique_ptr<AVPacket, PacketDelete> packet(av_packet_alloc());
  if (!codec || !frame || !packet) throw std::bad_alloc();
  codec->sample_rate = static_cast<int>(rate);
  // xWMA stream timing is expressed in decoded sample frames.
  codec->pkt_timebase = {1, static_cast<int>(rate)};
  av_channel_layout_default(&codec->ch_layout, static_cast<int>(channels));
  codec->block_align = static_cast<int>(block);
  auto average = kAverageBytes.at(average_index);
  // FFmpeg7.1.1 xwma_read_header corrects documented xWMA encoder rates.
  constexpr std::uint32_t kLowRate = 22050, kMidRate = 32000, kMonoRate = 44100,
                          kLowAverage = 6000, kHighAverage = 24000,
                          kStereoAverage = 4000, kMonoAverage = 2500,
                          kWrongMonoAverage = 12000;
  if (channels == 1) {
    if ((rate == kLowRate || rate == kMidRate) &&
        (average == kLowAverage || average == kHighAverage)) {
      average = kMonoAverage;
    } else if (rate == kMonoRate &&
               (average == kWrongMonoAverage || average == kHighAverage)) {
      average = kLowAverage;
    }
  } else if (rate == kLowRate &&
             (average == kLowAverage || average == kHighAverage)) {
    average = kStereoAverage;
  } else if (rate == kMidRate && average == kHighAverage) {
    average = kLowAverage;
  }
  constexpr std::uint32_t kBitsPerByte = 8;
  codec->bit_rate = static_cast<std::int64_t>(average) * kBitsPerByte;
  codec->extradata = static_cast<std::uint8_t*>(
      av_mallocz(kExtraBytes + AV_INPUT_BUFFER_PADDING_SIZE));
  if (!codec->extradata) throw std::bad_alloc();
  codec->extradata_size = static_cast<int>(kExtraBytes);
  std::span(codec->extradata, kExtraBytes)[kExtraFlagOffset] = kExtraFlag;
  codec->thread_count = 1;
  codec->err_recognition = AV_EF_EXPLODE;
  checked(avcodec_open2(codec.get(), implementation, nullptr),
          "Open WMAv2 codec");
  SwrContext* raw = nullptr;
  checked(
      swr_alloc_set_opts2(&raw, &codec->ch_layout, AV_SAMPLE_FMT_FLT,
                          codec->sample_rate, &codec->ch_layout,
                          codec->sample_fmt, codec->sample_rate, 0, nullptr),
      "Prepare WMA sample layout");
  std::unique_ptr<SwrContext, ResamplerDelete> resampler(raw);
  if (!resampler) throw std::bad_alloc();
  checked(swr_init(resampler.get()), "Initialize WMA sample layout");
  std::size_t written = 0;
  auto receive = [&] {
    for (;;) {
      const auto status = avcodec_receive_frame(codec.get(), frame.get());
      if (status == AVERROR(EAGAIN) || status == AVERROR_EOF) break;
      checked(status, "Decode WMA packet");
      if (frame->nb_samples < 0 || static_cast<std::size_t>(frame->nb_samples) >
                                       source.duration - written) {
        throw std::runtime_error("WMA decoded beyond authored duration");
      }
      // FFmpeg accepts byte pointers for float PCM; the destination remains
      // aligned float storage with validated sample capacity.
      // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
      auto* destination = reinterpret_cast<std::uint8_t*>(
          std::span(result.samples_).subspan(written * channels).data());
      const std::span decoded_planes(frame->extended_data, channels);
      const std::array<const std::uint8_t*, 2> planes{
          decoded_planes[0], channels > 1 ? decoded_planes[1] : nullptr};
      const auto converted =
          swr_convert(resampler.get(), &destination, frame->nb_samples,
                      planes.data(), frame->nb_samples);
      checked(converted, "Interleave decoded WMA samples");
      if (converted != frame->nb_samples) {
        throw std::runtime_error("Unexpected WMA sample conversion delay");
      }
      written += static_cast<std::size_t>(converted);
      av_frame_unref(frame.get());
    }
  };
  for (std::size_t index = 0; index < packets; ++index) {
    checked(av_new_packet(packet.get(), static_cast<int>(block)),
            "Allocate padded WMA packet");
    std::memcpy(packet->data,
                source.encoded.subspan(index * block, block).data(), block);
    checked(avcodec_send_packet(codec.get(), packet.get()),
            "Submit WMA packet");
    av_packet_unref(packet.get());
    receive();
  }
  checked(avcodec_send_packet(codec.get(), nullptr), "Drain WMA codec");
  receive();
  // XACT exposes the seek-table duration even when the final coded frame is
  // shorter. The reference WMA callback zero-fills its unavailable EOF tail.
  // Bound this to one codec frame; a larger shortage is malformed input.
  // FFmpeg wma_common.c: WMAv2 frame length depends only on sample rate;
  // AVCodecContext::frame_size is an encoder field and remains zero here.
  constexpr std::uint32_t kShortFrameRate = 16000, kMediumFrameRate = 22050;
  constexpr std::size_t kShortFrame = 512, kMediumFrame = 1024,
                        kLongFrame = 2048;
  const auto frame_length = rate <= kShortFrameRate    ? kShortFrame
                            : rate <= kMediumFrameRate ? kMediumFrame
                                                       : kLongFrame;
  if (source.duration - written > frame_length) {
    throw std::runtime_error("WMA decoded duration mismatch");
  }
  return result;
}

bool PreparedWave::read(std::size_t first_frame,
                        std::span<float> destination) const noexcept {
  if (destination.size() % channels_ != 0 || first_frame > frames() ||
      destination.size() / channels_ > frames() - first_frame) {
    return false;
  }
  std::copy_n(
      samples_.begin() + static_cast<std::ptrdiff_t>(first_frame * channels_),
      destination.size(), destination.begin());
  return true;
}
}  // namespace bw::audio
