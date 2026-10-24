// Build typed load-time audio metadata; wave payloads remain in retained files.
// Validate every offset before borrowing it, and only publish after cue/sound/
// wave relationships close. Playback and global RPC evaluation are separate
// consumers. Algorithm/cost/lifetime are in audio_bank.hpp. Both implementation
// corpora were queried: SL.con.3 (#rsl-bounds) bounds references; MEM.6
// (guidelines/memory/MEM.6-inject-allocators-behind-one-interface.md) retains
// injected domain allocators. Schema evidence is in audio-bank-reference.md.
#include "bw/formats/audio_bank.hpp"

#include <algorithm>
#include <bit>
#include <cmath>

#include "reader.hpp"
namespace bw::formats {
namespace {
using resources::detail::malformed;
using resources::detail::Reader;
using resources::detail::unsupported;
constexpr std::uint64_t kWord = 4, kName = 64;
constexpr unsigned kByteBits = 8;
constexpr std::uint32_t kAbsent = 0xffffffff;
constexpr std::uint32_t kWaveFlagMask = 0xf, kVariationTypeMask = 7;
constexpr std::uint8_t kMaximumByte = 0xff, kUnknownRpc = 8;
constexpr std::size_t kSoundDirectoryEntries = 10;
constexpr std::uint16_t kContentVersion = 46, kHeaderVersion = 44,
                        kToolVersion = 43;
struct Cursor {
  Reader reader;
  std::uint64_t at{};
  std::span<const std::uint8_t> take(std::uint64_t count) {
    reader.range(at, count);
    const auto result = reader.b.subspan(static_cast<std::size_t>(at),
                                         static_cast<std::size_t>(count));
    at += count;
    return result;
  }
  std::uint8_t byte() { return take(1)[0]; }
  std::uint16_t word() {
    const auto bytes = take(2);
    return static_cast<std::uint16_t>(bytes[0] | (bytes[1] << kByteBits));
  }
  std::uint32_t integer() {
    const auto result = reader.u32(at);
    at += kWord;
    return result;
  }
  std::int16_t signed_word() { return std::bit_cast<std::int16_t>(word()); }
  float real() {
    const auto result = std::bit_cast<float>(integer());
    if (!std::isfinite(result)) malformed("Non-finite audio parameter");
    return result;
  }
};
std::span<const std::uint8_t> slice(Reader reader, resources::Range range) {
  reader.range(range.offset, range.size);
  return reader.b.subspan(static_cast<std::size_t>(range.offset),
                          static_cast<std::size_t>(range.size));
}
constexpr std::uint32_t kComplex = 1, kRpc = 2, kTrackRpc = 4;
constexpr std::uint32_t kRpcMask = 0xe, kDsp = 0x10;
constexpr std::uint8_t kStop = 0, kPlay = 1, kTrackVariation = 3;
constexpr std::uint8_t kEffectVariation = 4, kTrackEffect = 6;
constexpr std::uint8_t kPitch = 7, kVolume = 8, kMarker = 9;
constexpr std::uint8_t kRepeatPitch = 16, kRepeatVolume = 17,
                       kRepeatMarker = 18;
}  // namespace
AudioBankDefinition::AudioBankDefinition(
    const std::shared_ptr<memory::Domain>& domain)
    : wave_bank_(domain),
      cues_(memory::Allocator<AudioCue>(domain)),
      sounds_(memory::Allocator<AudioSound>(domain)),
      tracks_(memory::Allocator<AudioTrack>(domain)),
      events_(memory::Allocator<AudioEvent>(domain)),
      choices_(memory::Allocator<AudioWaveChoice>(domain)),
      codes_(memory::Allocator<std::uint32_t>(domain)) {}

// Resolve the wrapper before interpreting its embedded XACT bank. A nonpositive
// cue parameter retains the original request for an engine initialization
// default.
AudioBankDefinition AudioBankDefinition::parse(
    AudioBankInputs inputs, const std::shared_ptr<memory::Domain>& domain) {
  const auto wrapped_sound = inputs.wrapped_sound;
  const Reader wrapper{wrapped_sound};
  constexpr std::uint64_t kHeader = 16, kFlags = 8, kNames = 12;
  wrapper.range(0, kHeader);
  if (wrapper.tag_view(0) != "FBSC") {
    unsupported("Unsupported sound bank wrapper");
  }
  const auto count = wrapper.u32(kWord);
  const bool named = (wrapper.u32(kFlags) & 1) != 0;
  const std::uint64_t stride = named ? 3 * kWord : 2 * kWord;
  const auto sound_start = kHeader + static_cast<std::uint64_t>(count) * stride;
  wrapper.range(kHeader, sound_start - kHeader);
  AudioBankDefinition result(domain);
  result.wave_bank_ = WaveBankDefinition::parse(inputs.wave_bank, domain);
  result.read_sounds(
      wrapped_sound.subspan(static_cast<std::size_t>(sound_start)));
  if (result.cues_.size() != count) {
    malformed("Sound wrapper cue count mismatch");
  }
  const auto names_start = sound_start + wrapper.u32(kNames);
  for (std::size_t index = 0; index < count; ++index) {
    Cursor input{wrapper, kHeader + index * stride};
    const auto parameter = input.real();
    auto& cue = result.cues_.at(index);
    if (parameter > 0) cue.parameter = parameter;
    cue.wrapper_flags = input.integer();
    if (named) {
      wrapper.range(names_start, 0);
      cue.wrapper_name = wrapper.text_view(
          {names_start, wrapped_sound.size() - names_start}, input.integer());
    }
  }
  return result;
}

// Resolve wave byte ranges and per-wave seek tables without decoding samples.
WaveBankDefinition::WaveBankDefinition(
    const std::shared_ptr<memory::Domain>& domain)
    : waves_(memory::Allocator<AudioWave>(domain)) {}
WaveBankDefinition WaveBankDefinition::parse(
    std::span<const std::uint8_t> bytes,
    const std::shared_ptr<memory::Domain>& domain) {
  WaveBankDefinition result(domain);
  result.read(bytes);
  return result;
}
void WaveBankDefinition::read(std::span<const std::uint8_t> bytes) {
  const Reader reader{bytes};
  constexpr std::uint64_t kSegmentDirectory = 12, kSegmentCount = 5;
  constexpr std::uint32_t kStreaming = 1, kCompact = 0x20000;
  constexpr std::uint32_t kSeekTables = 0x80000, kEntryNames = 0x10000;
  constexpr std::uint64_t kInfoSize = 96, kInfoLayout = 72, kEntrySize = 24;
  if (reader.tag_view(0) != "WBND" || reader.u32(kWord) != kContentVersion ||
      reader.u32(2 * kWord) != kHeaderVersion) {
    unsupported("Unsupported wave bank version or endian");
  }
  std::array<resources::Range, kSegmentCount> segments{};
  for (std::size_t index = 0; index < segments.size(); ++index) {
    const auto offset = kSegmentDirectory + index * 2 * kWord;
    segments.at(index) = {reader.u32(offset), reader.u32(offset + kWord)};
    reader.range(segments.at(index).offset, segments.at(index).size);
  }
  const Reader info{slice(reader, segments.at(0))};
  info.range(0, kInfoSize);
  const auto flags = info.u32(0), count = info.u32(kWord);
  if ((flags & kCompact) != 0) {
    unsupported("Compact wave bank construction is unresolved");
  }
  // Streaming is an I/O policy, not a different full-entry metadata layout.
  // Retain the flag so the backend selects the matching native constructor.
  streaming_ = (flags & kStreaming) != 0;
  name_ = info.text_view({2 * kWord, kName}, 0);
  if (info.u32(kInfoLayout) != kEntrySize) {
    unsupported("Unsupported wave entry stride");
  }
  const Reader entries{slice(reader, segments.at(1))};
  entries.range(0, static_cast<std::uint64_t>(count) * kEntrySize);
  const Reader seeks{slice(reader, segments.at(2))};
  const Reader payload{slice(reader, segments.at(4))};
  waves_.reserve(count);
  for (std::uint32_t index = 0; index < count; ++index) {
    Cursor input{entries, index * kEntrySize};
    AudioWave wave;
    const auto packed = input.integer();
    wave.flags = packed & kWaveFlagMask;
    constexpr unsigned kDurationShift = 4;
    wave.duration = packed >> kDurationShift;
    wave.format = input.integer();
    const resources::Range play{input.integer(), input.integer()};
    wave.encoded = slice(payload, play);
    wave.loop_start = input.integer();
    wave.loop_length = input.integer();
    if (wave.loop_start > wave.duration ||
        wave.loop_length > wave.duration - wave.loop_start) {
      malformed("Wave loop exceeds duration");
    }
    if ((flags & kSeekTables) != 0 && !seeks.b.empty()) {
      const auto relative = seeks.u32(index * kWord);
      if (relative != kAbsent) {
        const auto table = static_cast<std::uint64_t>(count) * kWord + relative;
        wave.seek_entries = slice(
            seeks, {table + kWord,
                    static_cast<std::uint64_t>(seeks.u32(table)) * kWord});
      }
    }
    waves_.push_back(wave);
  }
  if ((flags & kEntryNames) != 0) {
    const Reader names{slice(reader, segments.at(3))};
    for (std::uint32_t index = 0; index < count; ++index) {
      (void)names.text_view({index * kName, kName}, 0);
    }
  }
}
resources::Range AudioBankDefinition::read_codes(
    std::span<const std::uint8_t> bytes, std::uint64_t& cursor) {
  Cursor input{Reader{bytes}, cursor};
  const auto count = input.byte();
  const resources::Range result{codes_.size(), count};
  for (std::uint32_t index = 0; index < count; ++index) {
    codes_.push_back(input.integer());
  }
  cursor = input.at;
  return result;
}

// Event metadata retains the original authored parameters and wave
// alternatives. It does not advance timestamps, choose randomness or create
// playback voices.
void AudioBankDefinition::read_events(std::span<const std::uint8_t> bytes,
                                      std::uint64_t& cursor,
                                      AudioTrack& track) {
  Cursor input{Reader{bytes}, cursor};
  const auto count = input.byte();
  track.events = {events_.size(), count};
  constexpr std::uint32_t kEventMask = 0x1f, kTimestampMask = 0xffff;
  constexpr unsigned kTimestampShift = 5, kTableShift = 19;
  for (std::uint32_t index = 0; index < count; ++index) {
    AudioEvent event;
    const auto packed = input.integer();
    event.type = static_cast<std::uint8_t>(packed & kEventMask);
    event.timestamp = (packed >> kTimestampShift) & kTimestampMask;
    event.random_offset = input.word();
    if (input.byte() != kMaximumByte) {
      malformed("Invalid audio event separator");
    }
    if (event.type == kStop) {
      event.flags = input.byte();
    } else if (event.type == kPlay || event.type == kTrackVariation ||
               event.type == kEffectVariation || event.type == kTrackEffect) {
      const bool choices =
          event.type == kTrackVariation || event.type == kTrackEffect;
      event.flags = input.byte();
      event.choices.offset = choices_.size();
      if (!choices) {
        choices_.push_back({input.word(), input.byte(), 0, kMaximumByte});
        event.choices.size = 1;
      }
      event.loops = input.byte();
      event.position = input.word();
      event.angle = input.word();
      if (event.type == kEffectVariation || event.type == kTrackEffect) {
        event.minimum_pitch = input.signed_word();
        event.maximum_pitch = input.signed_word();
        event.minimum_volume = input.byte();
        event.maximum_volume = input.byte();
        for (auto& value : event.effect) value = input.real();
        event.variation_flags = input.word();
      }
      if (choices) {
        event.variation = input.integer();
        if (((event.variation >> kTableShift) & kVariationTypeMask) != 0) {
          unsupported("Unsupported event variation table type");
        }
        (void)input.integer();
        event.choices.size = event.variation & kTimestampMask;
        for (std::uint64_t choice = 0; choice < event.choices.size; ++choice) {
          choices_.push_back(
              {input.word(), input.byte(), input.byte(), input.byte()});
        }
      }
    } else if (event.type == kPitch || event.type == kVolume ||
               event.type == kRepeatPitch || event.type == kRepeatVolume) {
      event.settings = input.byte();
      if ((event.settings & 1) != 0) {
        for (auto& value : event.value) value = input.real();
        event.frequency =
            input.word();  // Authored ramp duration, milliseconds.
      } else {
        event.equation = input.byte();
        constexpr std::uint8_t kValueOrRandom = 0xc;
        if ((event.equation & kValueOrRandom) == 0) {
          unsupported("Unsupported audio equation");
        }
        event.value.at(0) = input.real();
        event.value.at(1) = input.real();
        constexpr std::uint64_t kEquationPadding = 5;
        (void)input.take(kEquationPadding);
        if (event.type == kRepeatPitch || event.type == kRepeatVolume) {
          event.repeats = input.word();
          event.frequency = input.word();
        }
      }
    } else if (event.type == kMarker || event.type == kRepeatMarker) {
      event.marker = input.integer();
      if (event.type == kRepeatMarker) {
        event.repeats = input.word();
        event.frequency = input.word();
      }
    } else {
      unsupported("Unsupported audio track event");
    }
    events_.push_back(event);
  }
  cursor = input.at;
}

void AudioBankDefinition::read_sounds(std::span<const std::uint8_t> bytes) {
  const Reader reader{bytes};
  constexpr std::uint64_t kHeader = 138, kPlatform = 18, kCounts = 19;
  constexpr std::uint64_t kSoundCount = 28, kDirectory = 34, kBankName = 74;
  constexpr std::size_t kSimple = 0, kComplexCue = 1, kCueNames = 2;
  constexpr std::size_t kWaveNames = 6, kCueIndex = 8, kSounds = 9;
  constexpr std::uint64_t kCueNameEntry = 6;
  reader.range(0, kHeader);
  Cursor version{reader, kWord};
  if (reader.tag_view(0) != "SDBK" || version.word() != kContentVersion ||
      version.word() != kToolVersion || bytes[kPlatform] != 1) {
    unsupported("Unsupported sound bank version or platform");
  }
  name_ = reader.text_view({kBankName, kName}, 0);
  Cursor counts{reader, kCounts};
  const auto simple = counts.word(), complex = counts.word();
  (void)counts.word();
  (void)counts.word();
  if (counts.byte() != 1) {
    unsupported("Multi-wave-bank definitions are unresolved");
  }
  Cursor sound_count{reader, kSoundCount};
  const auto total_sounds = sound_count.word();
  std::array<std::uint32_t, kSoundDirectoryEntries> offsets{};
  for (std::size_t index = 0; index < offsets.size(); ++index) {
    offsets.at(index) = reader.u32(kDirectory + index * kWord);
  }
  if (reader.text_view({offsets.at(kWaveNames), kName}, 0) !=
      wave_bank_.name()) {
    malformed("Sound bank references a different wave bank");
  }
  Cursor input{reader, offsets.at(kSounds)};
  sounds_.reserve(total_sounds);
  for (std::uint32_t index = 0; index < total_sounds; ++index) {
    AudioSound sound;
    sound.code = static_cast<std::uint32_t>(input.at);
    sound.flags = input.byte();
    sound.category = input.word();
    sound.volume = input.byte();
    sound.pitch = input.signed_word();
    sound.priority = input.byte();
    const auto length = input.word();
    reader.range(sound.code, length);
    const auto end = static_cast<std::uint64_t>(sound.code) + length;
    if ((sound.flags & kComplex) != 0) {
      sound.tracks = {tracks_.size(), input.byte()};
      tracks_.resize(tracks_.size() +
                     static_cast<std::size_t>(sound.tracks.size));
    } else {
      AudioEvent event;
      event.type = kPlay;
      event.choices = {choices_.size(), 1};
      choices_.push_back({input.word(), input.byte(), 0, kMaximumByte});
      AudioTrack track;
      track.events = {events_.size(), 1};
      events_.push_back(event);
      sound.tracks = {tracks_.size(), 1};
      tracks_.push_back(track);
    }
    if ((sound.flags & kRpcMask) != 0) {
      if ((sound.flags & kUnknownRpc) != 0) {
        unsupported("Unknown sound RPC flags");
      }
      const auto begin = input.at;
      const auto size = input.word();
      if ((sound.flags & kRpc) != 0) {
        sound.rpc_codes = read_codes(bytes, input.at);
      }
      if ((sound.flags & kTrackRpc) != 0) {
        for (std::uint64_t track = 0; track < sound.tracks.size; ++track) {
          tracks_.at(static_cast<std::size_t>(sound.tracks.offset + track))
              .rpc_codes = read_codes(bytes, input.at);
        }
      }
      if (input.at - begin != size) {
        malformed("Sound RPC block length mismatch");
      }
    }
    if ((sound.flags & kDsp) != 0) {
      const auto begin = input.at;
      const auto size = input.word();
      sound.dsp_codes = read_codes(bytes, input.at);
      if (input.at - begin != size) {
        malformed("Sound DSP block length mismatch");
      }
    }
    if ((sound.flags & kComplex) != 0) {
      // Track headers precede all event blocks. Validate each event block
      // within this sound's extent before advancing to the next sound
      // definition.
      constexpr std::uint64_t kTrackHeader = 9;
      const auto headers = input.at;
      (void)input.take(sound.tracks.size * kTrackHeader);
      for (std::uint64_t track_index = 0; track_index < sound.tracks.size;
           ++track_index) {
        Cursor header{reader, headers + track_index * kTrackHeader};
        auto& track = tracks_.at(
            static_cast<std::size_t>(sound.tracks.offset + track_index));
        track.volume = header.byte();
        std::uint64_t events = header.integer();
        track.filter = header.word();
        track.frequency = header.word();
        if (events < input.at || events >= end) {
          malformed("Audio events exceed sound");
        }
        read_events(bytes.first(static_cast<std::size_t>(end)), events, track);
      }
    }
    if (input.at > end) malformed("Sound header exceeds declared size");
    input.at = end;
    sounds_.push_back(sound);
  }
  // Close event references before cues can expose these definitions by name.
  for (const auto& choice : choices_) {
    if (choice.bank != 0 || choice.wave >= wave_bank_.waves().size() ||
        choice.minimum_weight > choice.maximum_weight) {
      malformed("Invalid audio wave choice");
    }
  }
  constexpr std::uint8_t kSingleSound = 4;
  const auto total = static_cast<std::size_t>(simple) + complex;
  cues_.reserve(total);
  for (std::size_t index = 0; index < total; ++index) {
    constexpr std::uint64_t kSimpleStride = 5, kComplexStride = 15;
    Cursor cue_input{reader, index < simple
                                 ? offsets.at(kSimple) + index * kSimpleStride
                                 : offsets.at(kComplexCue) +
                                       (index - simple) * kComplexStride};
    AudioCue cue;
    cue.flags = cue_input.byte();
    const auto code = cue_input.integer();
    if ((cue.flags & kSingleSound) == 0) {
      unsupported("Cue variation construction is unresolved");
    }
    const auto sound = std::ranges::find(sounds_, code, &AudioSound::code);
    if (sound == sounds_.end()) malformed("Cue references missing sound");
    cue.sound = static_cast<std::size_t>(sound - sounds_.begin());
    cue.instance_limit = kMaximumByte;
    if (index >= simple) {
      const auto transition = cue_input.integer();
      if (transition != 0 && transition != kAbsent) {
        unsupported("Cue transitions are unresolved");
      }
      cue.instance_limit = cue_input.byte();
      cue.fade_in = cue_input.word();
      cue.fade_out = cue_input.word();
      constexpr unsigned kBehaviorShift = 3;
      cue.maximum_behavior = cue_input.byte() >> kBehaviorShift;
    }
    if (offsets.at(kCueNames) != kAbsent) {
      const auto name =
          reader.u32(offsets.at(kCueIndex) + index * kCueNameEntry);
      cue.name = reader.text_view({0, bytes.size()}, name);
    }
    cues_.push_back(cue);
  }
}
std::optional<std::size_t> AudioBankDefinition::find_cue(
    std::string_view name) const {
  const auto found = std::ranges::find(cues_, name, &AudioCue::name);
  if (found == cues_.end()) return std::nullopt;
  return static_cast<std::size_t>(found - cues_.begin());
}
}  // namespace bw::formats
