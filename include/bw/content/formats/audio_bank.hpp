// Construct immutable sound-bank definitions at the original load boundary.
// Inputs are little-endian XACT 3.7 wave data and Bullet Witch's FBSC wrapper;
// the caller retains both byte owners until this definition is destroyed.
// Validate bank segments, sound/track/event records, cue names and wave links,
// then publish typed metadata together. Unknown record families fail
// explicitly; allocation failures unwind the unpublished result. No voice,
// mixer, decoder or native audio device is created. Exclusive construction;
// immutable reads thereafter require the byte owners to remain alive. Costs
// O(records + seek entries + name bytes), metadata proportional to records, no
// wave payload copy. Streaming flags are retained as metadata; this parser
// performs no streaming I/O or sample decoding. Design SL.con.3 (#rsl-bounds)
// checks every reference before access; MEM.6
// (guidelines/memory/MEM.6-inject-allocators-behind-one-interface.md) routes
// all variable metadata through the injected domain. Reference: pinned FACT
// parsers and original bank constructors mapped in audio-bank-reference.md.
#pragma once
#include "bw/memory/allocator.hpp"
#include "common.hpp"
namespace bw::formats {
struct AudioWave {
  std::uint32_t flags{}, duration{}, format{};
  std::span<const std::uint8_t> encoded;
  std::uint32_t loop_start{}, loop_length{};
  std::span<const std::uint8_t> seek_entries;
};
// Independent wave-bank metadata supports preparing compressed samples before
// the backend parses full cue variation/RPC schemas. Shares the same checked
// in-memory reader as AudioBankDefinition; no duplicate container parser.
class WaveBankDefinition {
 public:
  explicit WaveBankDefinition(const std::shared_ptr<memory::Domain>& domain);
  static WaveBankDefinition parse(
      std::span<const std::uint8_t> bytes,
      const std::shared_ptr<memory::Domain>& domain);
  std::string_view name() const { return name_; }
  bool streaming() const { return streaming_; }
  const auto& waves() const { return waves_; }

 private:
  void read(std::span<const std::uint8_t> bytes);
  bool streaming_{};
  std::string_view name_;
  memory::Vector<AudioWave> waves_;
};
struct AudioWaveChoice {
  std::uint16_t wave{};
  std::uint8_t bank{}, minimum_weight{}, maximum_weight{};
};
// Encoded volume bytes retain the original XACT conversion boundary. Parameters
// are definitions, never already evaluated gain/pitch or playback state.
struct AudioEvent {
  std::uint8_t type{}, flags{}, loops{};
  std::uint32_t timestamp{}, random_offset{}, variation{}, marker{};
  std::uint16_t position{}, angle{}, variation_flags{}, repeats{}, frequency{};
  std::int16_t minimum_pitch{}, maximum_pitch{};
  std::uint8_t minimum_volume{}, maximum_volume{}, settings{}, equation{};
  std::array<float, 4> effect{};
  std::array<float, 3> value{};
  resources::Range choices;
};
struct AudioTrack {
  std::uint8_t volume{};
  std::uint16_t filter{}, frequency{};
  resources::Range events, rpc_codes;
};
struct AudioSound {
  std::uint32_t code{};
  std::uint8_t flags{}, volume{}, priority{};
  std::uint16_t category{};
  std::int16_t pitch{};
  resources::Range tracks, rpc_codes, dsp_codes;
};
struct AudioCue {
  std::string_view name;
  std::size_t sound{};
  std::uint8_t flags{}, instance_limit{}, maximum_behavior{};
  std::uint16_t fade_in{}, fade_out{};
  // Nonpositive authored values request the native engine default, which is
  // intentionally unresolved until audio runtime initialization is recovered.
  std::optional<float> parameter;
  std::uint32_t wrapper_flags{};
  std::string_view wrapper_name;
};
struct AudioBankInputs {
  std::span<const std::uint8_t> wrapped_sound, wave_bank;
};
class AudioBankDefinition {
 public:
  static AudioBankDefinition parse(
      AudioBankInputs inputs, const std::shared_ptr<memory::Domain>& domain);
  std::string_view name() const { return name_; }
  const auto& waves() const { return wave_bank_.waves(); }
  const auto& cues() const { return cues_; }
  const auto& sounds() const { return sounds_; }
  const auto& tracks() const { return tracks_; }
  const auto& events() const { return events_; }
  const auto& wave_choices() const { return choices_; }
  const auto& parameter_codes() const { return codes_; }
  std::optional<std::size_t> find_cue(std::string_view name) const;

 private:
  explicit AudioBankDefinition(const std::shared_ptr<memory::Domain>& domain);
  void read_sounds(std::span<const std::uint8_t> bytes);
  resources::Range read_codes(std::span<const std::uint8_t> bytes,
                              std::uint64_t& cursor);
  void read_events(std::span<const std::uint8_t> bytes, std::uint64_t& cursor,
                   AudioTrack& track);
  std::string_view name_;
  WaveBankDefinition wave_bank_;
  memory::Vector<AudioCue> cues_;
  memory::Vector<AudioSound> sounds_;
  memory::Vector<AudioTrack> tracks_;
  memory::Vector<AudioEvent> events_;
  memory::Vector<AudioWaveChoice> choices_;
  memory::Vector<std::uint32_t> codes_;
};
}  // namespace bw::formats
