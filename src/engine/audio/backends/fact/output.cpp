// Pinned FACT cue ownership with immutable shared WMAv2 sample preparation.
// Loading publishes immutable lookup generations with release/acquire ordering;
// old generations survive until native shutdown. Native bank admission uses
// FACT's API lock; append-only decoded waves never move existing sample owners.
// CP.2 (#rconc-races), I.11 and MEM.9 were read for design/implementation:
// no callback locks, allocations or reclamation; loading work remains explicit.
// Direct prepared music shares FACT's FAudio device and its injected allocator.
// E.6 (#re-raii) scopes native teardown and unpublished bank failures; MEM.2
// bounds cue handles and MEM.9 keeps sample callbacks allocation-free. Vendor
// runtime allocations remain domain-accounted, not claimed allocation-free.
// SL.con.3 (#rsl-bounds) checks selective cue/track/wave references before
// publication. Both implementation corpora queried; see
// animation-audio-presentation.md.
// I.1 makes each cue's pause policy explicit; the fixed retained slot owns it
// for the cue lifetime. This translates original pause-slot admission rather
// than muting categories shared by gameplay and interface sounds.
// Mix gain is a separate original request role, applied through the pinned
// native wave-volume boundary. The C bridge holds FACT's API lock (CP.2,
// #rconc-races); no game update writes mutable native state without that lock.
#include "output.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cmath>
#include <cstring>
#include <functional>
#include <limits>
#include <stdexcept>

#include "bw/audio/wave.hpp"
#include "bw/math/vector.hpp"
#include "wma_bridge.h"

extern "C" {
#include <FACT.h>
#include <FACT3D.h>
}

namespace bw::audio::fact {
namespace {
struct SampleEntry {
  const std::uint8_t* encoded{};
  std::size_t encoded_bytes{};
  const PreparedWave* wave{};
};
struct SampleTable {
  explicit SampleTable(const std::shared_ptr<memory::Domain>& owner)
      : entries(memory::Allocator<SampleEntry>(owner)) {}
  memory::Vector<SampleEntry> entries;
};
struct Bridge {
  explicit Bridge(const std::shared_ptr<memory::Domain>& owner)
      : domain(owner),
        generations(memory::Allocator<std::shared_ptr<SampleTable>>(owner)) {}
  std::shared_ptr<memory::Domain> domain;
  memory::Vector<std::shared_ptr<SampleTable>> generations;
  std::atomic<const SampleTable*> published{};
  std::atomic<std::size_t> failures{};
};
std::atomic<Bridge*>& bridge_slot() {
  static std::atomic<Bridge*> value;
  return value;
}
struct alignas(std::max_align_t) Allocation {
  std::size_t size;
};
void* allocate(std::size_t size) noexcept {
  auto* owner = bridge_slot().load(std::memory_order_acquire);
  if (!owner ||
      size > std::numeric_limits<std::size_t>::max() - sizeof(Allocation)) {
    return nullptr;
  }
  auto* block = static_cast<Allocation*>(
      owner->domain->try_allocate_zeroed(size + sizeof(Allocation)));
  if (!block) return nullptr;
  block->size = size;
  return std::as_writable_bytes(std::span(block, 1))
      .subspan(sizeof(Allocation))
      .data();
}
Allocation* allocation_header(void* pointer) noexcept {
  // C allocator ABI returns the byte immediately following our aligned header.
  const auto address =
      std::bit_cast<std::uintptr_t>(pointer) - sizeof(Allocation);
  return std::bit_cast<Allocation*>(address);
}
void release(void* pointer) noexcept {
  if (pointer) memory::Domain::release(allocation_header(pointer));
}
void* reallocate(void* pointer, std::size_t size) noexcept {
  if (!pointer) return allocate(size);
  if (size == 0) {
    release(pointer);
    return nullptr;
  }
  void* replacement = allocate(size);
  if (!replacement) return nullptr;
  const auto previous = (allocation_header(pointer))->size;
  std::memcpy(replacement, pointer, std::min(size, previous));
  release(pointer);
  return replacement;
}
void checked(std::uint32_t code, const char* message) {
  if (code != 0) throw std::runtime_error(message);
}
std::uint32_t word(std::span<const std::uint8_t> bytes, std::size_t at) {
  constexpr std::size_t kWordBytes = 4;
  constexpr unsigned kByteBits = 8;
  if (at > bytes.size() || bytes.size() - at < kWordBytes) {
    throw std::invalid_argument("Truncated audio bank wrapper");
  }
  std::uint32_t result = 0;
  for (unsigned index = 0; index < kWordBytes; ++index) {
    result |= static_cast<std::uint32_t>(bytes[at + index])
              << (index * kByteBits);
  }
  return result;
}
std::span<const std::uint8_t> sound_payload(
    std::span<const std::uint8_t> bytes) {
  constexpr std::uint32_t kWrapperTag = 0x43534246;
  constexpr std::size_t kHeaderBytes = 16, kNameFlagsOffset = 8;
  if (word(bytes, 0) != kWrapperTag) {
    throw std::invalid_argument("Unsupported game sound wrapper");
  }
  const auto count = word(bytes, 4);
  const std::size_t stride = (word(bytes, kNameFlagsOffset) & 1) != 0 ? 12 : 8;
  if (bytes.size() < kHeaderBytes ||
      count > (bytes.size() - kHeaderBytes) / stride) {
    throw std::invalid_argument("Sound wrapper descriptor range");
  }
  return bytes.subspan(kHeaderBytes + count * stride);
}
void position_valid(const math::Vector3<float>& position) {
  if (!math::all_finite(position)) {
    throw std::invalid_argument("Nonfinite audio position");
  }
}
F3DAUDIO_VECTOR vector(const math::Vector3<float>& value) {
  return {value[0], value[1], value[2]};
}
struct Bank {
  explicit Bank(const std::shared_ptr<memory::Domain>& domain)
      : sound(memory::Allocator<std::uint8_t>(domain)),
        encoded(memory::Allocator<std::uint8_t>(domain)),
        decoded(memory::Allocator<std::optional<PreparedWave>>(domain)),
        admitted(memory::Allocator<std::uint8_t>(domain)) {}
  ~Bank() { retire_native(); }
  void retire_native() noexcept {
    if (sound_bank) bw_fact_destroy_sound_bank(sound_bank);
    if (wave_bank) bw_fact_destroy_wave_bank(wave_bank);
    sound_bank = nullptr;
    wave_bank = nullptr;
  }
  Bank(const Bank&) = delete;
  Bank& operator=(const Bank&) = delete;
  Bank(Bank&&) = delete;
  Bank& operator=(Bank&&) = delete;
  std::uint16_t slot{}, cues{};
  memory::Vector<std::uint8_t> sound, encoded;
  std::optional<formats::WaveBankDefinition> metadata;
  memory::Vector<std::optional<PreparedWave>> decoded;
  memory::Vector<std::uint8_t> admitted;
  FACTSoundBank* sound_bank{};
  FACTWaveBank* wave_bank{};
};
// Original cue/track/variation closure, prepared transactionally during load.
std::size_t prepare_cues(Bank& bank,
                         std::span<const std::uint16_t> admitted_cues,
                         std::size_t sample_budget,
                         const std::shared_ptr<memory::Domain>& domain) {
  if (!bank.metadata) {
    throw std::logic_error("Audio bank metadata is unprepared");
  }
  const auto& metadata = *bank.metadata;
  // Walk every admitted cue's sound/track/variation closure at setup. Bounds
  // are validated by the shared definition; no runtime cue-name lookup occurs.
  const auto definition =
      formats::AudioBankDefinition::parse({bank.sound, bank.encoded}, domain);
  auto admitted = bank.admitted;
  if (admitted.empty()) admitted.resize(definition.cues().size(), 0);
  if (admitted_cues.empty()) std::ranges::fill(admitted, 1);
  for (const auto cue : admitted_cues) {
    if (cue >= admitted.size()) {
      throw std::invalid_argument("Admitted audio cue exceeds bank");
    }
    admitted.at(cue) = 1;
  }
  memory::Vector<std::uint8_t> needed(definition.waves().size(), 0,
                                      memory::Allocator<std::uint8_t>(domain));
  for (std::size_t cue = 0; cue < definition.cues().size(); ++cue) {
    if (!admitted.at(cue)) continue;
    const auto& sound = definition.sounds().at(definition.cues().at(cue).sound);
    for (std::uint64_t track = sound.tracks.offset;
         track < sound.tracks.offset + sound.tracks.size; ++track) {
      const auto events = definition.tracks().at(track).events;
      for (std::uint64_t event = events.offset;
           event < events.offset + events.size; ++event) {
        const auto choices = definition.events().at(event).choices;
        for (std::uint64_t choice = choices.offset;
             choice < choices.offset + choices.size; ++choice) {
          const auto& value = definition.wave_choices().at(choice);
          if (value.bank != 0) {
            throw std::invalid_argument("External cue wave bank is unprepared");
          }
          needed.at(value.wave) = 1;
        }
      }
    }
  }
  memory::Vector<std::optional<PreparedWave>> additions(
      metadata.waves().size(),
      memory::Allocator<std::optional<PreparedWave>>(domain));
  std::size_t prepared_samples = 0;
  for (std::size_t index = 0; index < needed.size(); ++index) {
    if (!needed.at(index) || bank.decoded.at(index)) continue;
    const auto& wave = metadata.waves().at(index);
    // Pinned FACT streams WMA as one whole-wave cache and only supports
    // absent or complete-wave loops. Reject valid-but-unsupported subregions
    // before its native assertion, without rewriting authored loop metadata.
    if (metadata.streaming() &&
        (wave.loop_start != 0 ||
         (wave.loop_length != 0 && wave.loop_length != wave.duration))) {
      throw std::invalid_argument(
          "Streaming WMA partial loops are unsupported");
    }
    auto decoded =
        PreparedWave::decode(wave, sample_budget - prepared_samples, domain);
    prepared_samples += decoded.samples().size();
    additions.at(index).emplace(std::move(decoded));
  }
  // Publish only after every newly admitted wave decoded successfully.
  for (std::size_t index = 0; index < additions.size(); ++index) {
    if (additions.at(index)) {
      bank.decoded.at(index) = std::move(additions.at(index));
    }
  }
  bank.admitted = std::move(admitted);
  return prepared_samples;
}
struct CueSlot {
  FACTCue* cue{};
  std::uint32_t generation{};
  std::optional<math::Vector3<float>> position;
  PauseDomain pause_domain{PauseDomain::world};
  MixBus mix_bus{MixBus::effects};
};
float mix_gain(const MixGains& gains, MixBus bus) {
  switch (bus) {
    case MixBus::effects:
      return gains.effects;
    case MixBus::music:
      return gains.music;
    case MixBus::voice:
      return gains.voice;
  }
  throw std::invalid_argument("Invalid audio mix bus");
}
constexpr std::size_t kMaximumOutputChannels = 8;
}  // namespace

struct Output::State {
  State(Limits requested, const std::shared_ptr<memory::Domain>& owner)
      : registry(owner),
        limits(requested),
        settings(memory::Allocator<std::uint8_t>(owner)),
        banks(memory::Allocator<std::shared_ptr<Bank>>(owner)),
        slots(memory::Allocator<CueSlot>(owner)) {
    if (!limits.banks || !limits.cues || !limits.decoded_samples ||
        limits.cues > std::numeric_limits<std::uint32_t>::max()) {
      throw std::invalid_argument(
          "Audio capacities must be finite and nonzero");
    }
    banks.reserve(limits.banks);
    slots.resize(limits.cues);
  }
  State(const State&) = delete;
  State& operator=(const State&) = delete;
  State(State&&) = delete;
  State& operator=(State&&) = delete;
  ~State() {
    for (auto& slot : slots) {
      if (slot.cue) {
        FACTCue_Stop(slot.cue, FACT_FLAG_STOP_IMMEDIATE);
        FACTCue_Destroy(slot.cue);
      }
    }
    for (auto* voice : music_voices) {
      if (voice) FAudioVoice_DestroyVoice(voice);
    }
    for (auto& bank : banks) bank->retire_native();
    if (engine) {
      bw_fact_release_engine(engine);
    }
    // Callbacks are joined before encoded bytes, samples and lookup versions.
    banks.clear();
    if (owns_bridge) bridge_slot().store(nullptr, std::memory_order_release);
  }
  void initialize(std::span<const std::uint8_t> bytes) {
    if (bytes.empty() ||
        bytes.size() > std::numeric_limits<std::uint32_t>::max()) {
      throw std::invalid_argument("Invalid global audio settings");
    }
    Bridge* empty = nullptr;
    if (!bridge_slot().compare_exchange_strong(empty, &registry,
                                               std::memory_order_acq_rel)) {
      throw std::logic_error("A native FACT audio owner already exists");
    }
    owns_bridge = true;
    settings.assign(bytes.begin(), bytes.end());
    checked(FACTCreateEngineWithCustomAllocatorEXT(0, &engine, allocate,
                                                   release, reallocate),
            "Create FACT engine");
    FACTRuntimeParameters parameters{};
    parameters.lookAheadTime = FACT_ENGINE_LOOKAHEAD_DEFAULT;
    parameters.pGlobalSettingsBuffer = settings.data();
    parameters.globalSettingsBufferSize =
        static_cast<std::uint32_t>(settings.size());
    checked(bw_fact_initialize(engine, &parameters), "Initialize FACT engine");
    FAudioWaveFormatExtensible format{};
    checked(FACTAudioEngine_GetFinalMixFormat(engine, &format),
            "Read audio device layout");
    output_channels = format.Format.nChannels;
    if (!output_channels || output_channels > kMaximumOutputChannels) {
      throw std::runtime_error("Unsupported native audio channel count");
    }
    checked(FACT3DInitialize(engine, spatial.data()),
            "Initialize source spatial audio");
  }
  CueSlot* find(CueHandle handle) {
    if (handle.slot >= slots.size()) return nullptr;
    auto& slot = slots[handle.slot];
    return slot.cue && slot.generation == handle.generation ? &slot : nullptr;
  }
  void spatialize(CueSlot& slot) {
    if (!slot.position) return;
    // Point emitters use one spatial matrix row. FACTWave_SetMatrixCoefficients
    // expands it for stereo waves; FACT retains Distance/Orientation RPCs.
    F3DAUDIO_LISTENER native_listener{};
    native_listener.Position = vector(listener.position);
    native_listener.OrientFront = vector(listener.forward);
    native_listener.OrientTop = vector(listener.up);
    F3DAUDIO_EMITTER emitter{};
    emitter.Position = vector(*slot.position);
    emitter.OrientFront = {0, 0, 1};
    emitter.OrientTop = {0, 1, 0};
    emitter.ChannelCount = 1;
    emitter.CurveDistanceScaler = 1;
    emitter.DopplerScaler = 1;
    std::array<float, kMaximumOutputChannels> matrix{};
    F3DAUDIO_DSP_SETTINGS dsp{};
    dsp.pMatrixCoefficients = matrix.data();
    dsp.SrcChannelCount = 1;
    dsp.DstChannelCount = output_channels;
    checked(FACT3DCalculate(spatial.data(), &native_listener, &emitter, &dsp),
            "Calculate cue spatial audio");
    checked(FACT3DApply(&dsp, slot.cue), "Apply cue spatial audio");
  }
  // Original two-source transport: start the next segment at its authored
  // sample threshold while allowing the previous source's tail to finish.
  void start_music_segment(unsigned index, bool intro) {
    if (!music) throw std::logic_error("Music is not prepared");
    auto*& voice = music_voices.at(index);
    if (voice) {
      FAudioVoice_DestroyVoice(voice);
      voice = nullptr;
    }
    const auto& source = *music;
    constexpr std::uint32_t kBitsPerByte = 8;
    FAudioWaveFormatEx format{};
    format.wFormatTag = FAUDIO_FORMAT_IEEE_FLOAT;
    format.nChannels = static_cast<std::uint16_t>(source.channels);
    format.nSamplesPerSec = source.sample_rate;
    format.nBlockAlign =
        static_cast<std::uint16_t>(source.channels * sizeof(float));
    format.nAvgBytesPerSec = format.nSamplesPerSec * format.nBlockAlign;
    format.wBitsPerSample = sizeof(float) * kBitsPerByte;
    checked(bw_fact_create_music_voice(engine, &format, &voice),
            "Create prepared music voice");
    try {
      const auto all = std::span<const float>(source.samples);
      const auto pcm =
          source.loop_frame_count == 0
              ? all
              : (intro ? all.first(
                             static_cast<std::size_t>(source.loop_begin_frame) *
                             source.channels)
                       : all.subspan(
                             static_cast<std::size_t>(source.loop_begin_frame) *
                                 source.channels,
                             static_cast<std::size_t>(source.loop_frame_count) *
                                 source.channels));
      FAudioBuffer buffer{};
      buffer.Flags = FAUDIO_END_OF_STREAM;
      buffer.AudioBytes = static_cast<std::uint32_t>(pcm.size_bytes());
      // FAudio's byte-typed C ABI reads the float object representation.
      // The immutable PCM owner survives both native voices and their joins.
      // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
      buffer.pAudioData = reinterpret_cast<const std::uint8_t*>(pcm.data());
      checked(FAudioSourceVoice_SubmitSourceBuffer(voice, &buffer, nullptr),
              "Submit prepared music segment");
      checked(FAudioVoice_SetVolume(voice, gains.music * music_envelope,
                                    FAUDIO_COMMIT_NOW),
              "Apply prepared music gain");
      checked(FAudioSourceVoice_Start(voice, 0, FAUDIO_COMMIT_NOW),
              "Start prepared music segment");
    } catch (...) {
      FAudioVoice_DestroyVoice(voice);
      voice = nullptr;
      throw;
    }
  }
  void advance_music() {
    if (!music_playing || !music || music->loop_frame_count == 0) return;
    auto* current = music_voices.at(music_current);
    if (!current) {
      throw std::logic_error("Playing music lost its native source");
    }
    FAudioVoiceState observed{};
    FAudioSourceVoice_GetState(current, &observed, 0);
    const auto threshold =
        music_intro ? music->first_switch_frame : music->loop_period_frames;
    if (observed.SamplesPlayed < threshold) return;
    const unsigned next = music_current ^ 1U;
    start_music_segment(next, false);
    music_current = next;
    music_intro = false;
  }
  Bridge registry;
  Limits limits;
  memory::Vector<std::uint8_t> settings;
  memory::Vector<std::shared_ptr<Bank>> banks;
  memory::Vector<CueSlot> slots;
  FACTAudioEngine* engine{};
  std::array<std::uint8_t, F3DAUDIO_HANDLE_BYTESIZE> spatial{};
  Listener listener;
  MixGains gains;
  std::size_t decoded_samples{};
  std::uint32_t output_channels{};
  std::optional<MusicSamples> music;
  std::array<FAudioSourceVoice*, 2> music_voices{};
  unsigned music_current{};
  bool music_intro{}, music_playing{};
  float music_envelope{1};
  bool owns_bridge{}, paused{}, preparation_pending{true};
};

Output::Output(std::span<const std::uint8_t> settings, Limits limits,
               const std::shared_ptr<memory::Domain>& domain)
    : state_(std::allocate_shared<State>(memory::Allocator<State>(domain),
                                         limits, domain)) {
  state_->initialize(settings);
}
Output::~Output() = default;

void Output::register_bank(std::uint16_t slot,
                           std::span<const std::uint8_t> wrapped_sound,
                           std::span<const std::uint8_t> wave_bank,
                           std::span<const std::uint16_t> admitted_cues) {
  if (state_->banks.size() == state_->limits.banks) {
    throw std::logic_error("Audio bank registration exhausted");
  }
  for (const auto& bank : state_->banks) {
    if (bank->slot == slot) {
      throw std::invalid_argument("Duplicate audio bank slot");
    }
  }
  auto candidate = std::allocate_shared<Bank>(
      memory::Allocator<Bank>(state_->registry.domain),
      state_->registry.domain);
  candidate->slot = slot;
  candidate->sound.assign(wrapped_sound.begin(), wrapped_sound.end());
  candidate->encoded.assign(wave_bank.begin(), wave_bank.end());
  candidate->metadata = formats::WaveBankDefinition::parse(
      candidate->encoded, state_->registry.domain);
  candidate->decoded.resize(candidate->metadata->waves().size());
  const auto prepared_samples =
      prepare_cues(*candidate, admitted_cues,
                   state_->limits.decoded_samples - state_->decoded_samples -
                       (state_->music ? state_->music->samples.size() : 0),
                   state_->registry.domain);
  const auto payload = sound_payload(candidate->sound);
  if (payload.size() > std::numeric_limits<std::uint32_t>::max() ||
      candidate->encoded.size() > std::numeric_limits<std::uint32_t>::max()) {
    throw std::invalid_argument("Audio bank exceeds native byte range");
  }
  if (candidate->metadata->streaming()) {
    checked(bw_fact_create_stream_bank(
                state_->engine, candidate->encoded.data(),
                candidate->encoded.size(), &candidate->wave_bank),
            "Create native streaming wave bank");
  } else {
    checked(FACTAudioEngine_CreateInMemoryWaveBank(
                state_->engine, candidate->encoded.data(),
                static_cast<std::uint32_t>(candidate->encoded.size()), 0, 0,
                &candidate->wave_bank),
            "Create native wave bank");
  }
  checked(FACTAudioEngine_CreateSoundBank(
              state_->engine, payload.data(),
              static_cast<std::uint32_t>(payload.size()), 0, 0,
              &candidate->sound_bank),
          "Create native sound bank");
  checked(FACTSoundBank_GetNumCues(candidate->sound_bank, &candidate->cues),
          "Read native cue count");
  if (candidate->cues != word(candidate->sound, 4)) {
    throw std::invalid_argument("Native sound/wrapper cue count mismatch");
  }
  state_->banks.push_back(std::move(candidate));
  state_->decoded_samples += prepared_samples;
  state_->preparation_pending = true;
}

void Output::extend_bank(std::uint16_t slot,
                         std::span<const std::uint16_t> admitted_cues) {
  for (auto& bank : state_->banks) {
    if (bank->slot != slot) continue;
    const auto added =
        prepare_cues(*bank, admitted_cues,
                     state_->limits.decoded_samples - state_->decoded_samples -
                         (state_->music ? state_->music->samples.size() : 0),
                     state_->registry.domain);
    state_->decoded_samples += added;
    state_->preparation_pending |= added != 0;
    return;
  }
  throw std::invalid_argument("Audio bank extension requires a retained bank");
}
void Output::seal() {
  if (!state_->preparation_pending) return;
  auto& registry = state_->registry;
  auto table = std::allocate_shared<SampleTable>(
      memory::Allocator<SampleTable>(registry.domain), registry.domain);
  std::size_t count = 0;
  for (const auto& bank : state_->banks) count += bank->decoded.size();
  table->entries.reserve(count);
  for (const auto& bank : state_->banks) {
    if (!bank->metadata) {
      throw std::logic_error("Audio bank metadata is unprepared");
    }
    const auto& definition = bank->metadata.value();
    for (std::size_t index = 0; index < bank->decoded.size(); ++index) {
      const auto& decoded = bank->decoded.at(index);
      if (!decoded) continue;
      const auto& wave = definition.waves().at(index);
      table->entries.push_back(
          {wave.encoded.data(), wave.encoded.size(), &decoded.value()});
    }
  }
  std::ranges::sort(table->entries, [](const auto& left, const auto& right) {
    return std::less<const std::uint8_t*>{}(left.encoded, right.encoded);
  });
  registry.generations.push_back(std::move(table));
  registry.published.store(registry.generations.back().get(),
                           std::memory_order_release);
  state_->preparation_pending = false;
}

// Validate the complete immutable track before replacing its retained owner.
void Output::prepare_music(MusicSamples samples) {
  const auto frames =
      samples.channels == 0 ? 0 : samples.samples.size() / samples.channels;
  if (samples.channels == 0 || samples.channels > kMaximumOutputChannels ||
      samples.sample_rate < FAUDIO_MIN_SAMPLE_RATE ||
      samples.sample_rate > FAUDIO_MAX_SAMPLE_RATE || samples.samples.empty() ||
      samples.samples.size() % samples.channels != 0 ||
      samples.samples.size() >
          std::numeric_limits<std::uint32_t>::max() / sizeof(float) ||
      samples.samples.size() >
          state_->limits.decoded_samples - state_->decoded_samples ||
      samples.loop_begin_frame > frames ||
      samples.loop_frame_count > frames - samples.loop_begin_frame ||
      (samples.loop_frame_count == 0 &&
       (samples.loop_begin_frame != 0 || samples.first_switch_frame != 0 ||
        samples.loop_period_frames != 0)) ||
      (samples.loop_frame_count != 0 &&
       (samples.loop_begin_frame == 0 || samples.first_switch_frame == 0 ||
        samples.first_switch_frame > samples.loop_begin_frame ||
        samples.loop_period_frames == 0 ||
        samples.loop_period_frames > samples.loop_frame_count ||
        samples.loop_begin_frame - samples.first_switch_frame >
            samples.loop_period_frames ||
        samples.loop_frame_count - samples.loop_period_frames >
            samples.loop_period_frames)) ||
      !std::ranges::all_of(samples.samples,
                           [](float value) { return std::isfinite(value); })) {
    throw std::invalid_argument("Invalid prepared music samples or loop");
  }
  stop_music();
  state_->music.emplace(std::move(samples));
}
void Output::start_music() {
  if (!state_->music) throw std::logic_error("Music is not prepared");
  stop_music();
  state_->start_music_segment(0, true);
  state_->music_current = 0;
  state_->music_intro = true;
  state_->music_playing = true;
}
void Output::stop_music() noexcept {
  state_->music_playing = false;
  for (auto*& voice : state_->music_voices) {
    if (!voice) continue;
    FAudioVoice_DestroyVoice(voice);
    voice = nullptr;
  }
}
void Output::set_music_envelope(float gain) {
  if (!std::isfinite(gain) || gain < 0 || gain > 1) {
    throw std::invalid_argument("Invalid music envelope");
  }
  for (auto* voice : state_->music_voices) {
    if (voice) {
      checked(FAudioVoice_SetVolume(voice, state_->gains.music * gain,
                                    FAUDIO_COMMIT_NOW),
              "Set music envelope");
    }
  }
  state_->music_envelope = gain;
}
bool Output::music_active() const {
  for (auto* voice : state_->music_voices) {
    if (!voice) continue;
    FAudioVoiceState observed{};
    FAudioSourceVoice_GetState(voice, &observed, FAUDIO_VOICE_NOSAMPLESPLAYED);
    if (observed.BuffersQueued != 0) return true;
  }
  return false;
}

std::optional<CueHandle> Output::play(const CueRequest& request) {
  if (state_->preparation_pending) {
    throw std::logic_error("Audio banks must be sealed before playback");
  }
  if (request.position) position_valid(*request.position);
  if (request.pause_domain != PauseDomain::world &&
      request.pause_domain != PauseDomain::presentation) {
    throw std::invalid_argument("Invalid audio pause domain");
  }
  const auto gain = mix_gain(state_->gains, request.mix_bus);
  const auto bank = std::ranges::find_if(state_->banks, [&](const auto& value) {
    return value->slot == request.bank;
  });
  if (bank == state_->banks.end() || request.cue >= (*bank)->cues ||
      !(*bank)->admitted.at(request.cue)) {
    return {};
  }
  const auto available = std::ranges::find_if(
      state_->slots, [](const auto& slot) { return slot.cue == nullptr; });
  if (available == state_->slots.end()) return {};
  FACTCue* cue = nullptr;
  checked(FACTSoundBank_Prepare((*bank)->sound_bank, request.cue, 0, 0, &cue),
          "Prepare native cue");
  if (!cue) return {};
  try {
    available->cue = cue;
    available->position = request.position;
    available->pause_domain = request.pause_domain;
    available->mix_bus = request.mix_bus;
    checked(bw_fact_set_cue_gain(cue, gain), "Set new cue mix gain");
    state_->spatialize(*available);
    checked(FACTCue_Play(cue), "Play native cue");
    if (state_->paused && available->pause_domain == PauseDomain::world) {
      checked(FACTCue_Pause(cue, 1), "Pause new world cue");
    }
  } catch (...) {
    FACTCue_Destroy(cue);
    available->cue = nullptr;
    throw;
  }
  ++available->generation;
  if (!available->generation) ++available->generation;
  return CueHandle{
      static_cast<std::uint32_t>(available - state_->slots.begin()),
      available->generation};
}
bool Output::set_position(CueHandle handle,
                          const math::Vector3<float>& position) {
  position_valid(position);
  auto* slot = state_->find(handle);
  if (!slot) return false;
  slot->position = position;
  state_->spatialize(*slot);
  return true;
}
void Output::set_listener(const Listener& listener) {
  position_valid(listener.position);
  position_valid(listener.forward);
  position_valid(listener.up);
  const auto forward = math::length(listener.forward);
  const auto up = math::length(listener.up);
  constexpr float kOrthogonalTolerance = .001F;
  if (!(forward > 0) || !(up > 0) ||
      std::abs(math::dot(listener.forward, listener.up)) >
          forward * up * kOrthogonalTolerance) {
    throw std::invalid_argument("Invalid audio listener basis");
  }
  state_->listener = {listener.position,
                      math::divide(listener.forward, forward),
                      math::divide(listener.up, up)};
  for (auto& slot : state_->slots) {
    if (slot.cue) state_->spatialize(slot);
  }
}
bool Output::stop(CueHandle handle) {
  auto* slot = state_->find(handle);
  if (!slot) return false;
  checked(FACTCue_Stop(slot->cue, FACT_FLAG_STOP_IMMEDIATE), "Stop native cue");
  checked(FACTCue_Destroy(slot->cue), "Destroy native cue");
  slot->cue = nullptr;
  return true;
}
void Output::set_mix_gains(const MixGains& gains) {
  for (const float gain : {gains.music, gains.effects, gains.voice}) {
    if (!std::isfinite(gain) || gain < 0 || gain > 1) {
      throw std::invalid_argument("Invalid audio mix gain");
    }
  }
  // Native publication is under FACT's lock, including paused/upcoming waves.
  // Authored RPC/category gain remains owned by FACT and is not overwritten.
  state_->gains = gains;
  set_music_envelope(state_->music_envelope);
  for (auto& slot : state_->slots) {
    if (slot.cue) {
      checked(bw_fact_set_cue_gain(slot.cue, mix_gain(gains, slot.mix_bus)),
              "Set active cue mix gain");
    }
  }
}
void Output::pause(bool paused) {
  for (auto& slot : state_->slots) {
    if (slot.cue && slot.pause_domain == PauseDomain::world) {
      checked(FACTCue_Pause(slot.cue, paused), "Pause native world cues");
    }
  }
  state_->paused = paused;
}
void Output::retire_world() {
  for (auto& slot : state_->slots) {
    if (!slot.cue || slot.pause_domain != PauseDomain::world) continue;
    checked(FACTCue_Stop(slot.cue, FACT_FLAG_STOP_IMMEDIATE),
            "Retire world cue");
    checked(FACTCue_Destroy(slot.cue), "Destroy retired world cue");
    slot.cue = nullptr;
  }
  state_->paused = false;
}
void Output::update() {
  state_->advance_music();
  if (state_->registry.failures.load(std::memory_order_acquire)) {
    throw std::runtime_error("Native audio requested unprepared sample data");
  }
  checked(FACTAudioEngine_DoWork(state_->engine), "Update native audio engine");
  for (auto& slot : state_->slots) {
    if (!slot.cue) continue;
    std::uint32_t flags = 0;
    checked(FACTCue_GetState(slot.cue, &flags), "Read native cue state");
    if ((flags & FACT_STATE_STOPPED) != 0) {
      checked(FACTCue_Destroy(slot.cue), "Retire completed native cue");
      slot.cue = nullptr;
    }
  }
}
bool Output::active(CueHandle handle) const {
  return state_->find(handle) != nullptr;
}

}  // namespace bw::audio::fact

extern "C" int bw_fact_copy_samples(const std::uint8_t* encoded,
                                    std::size_t encoded_bytes,
                                    std::uint32_t channels, std::uint32_t rate,
                                    std::size_t first_frame, float* destination,
                                    std::size_t frames) {
  using bw::audio::fact::bridge_slot;
  auto* owner = bridge_slot().load(std::memory_order_acquire);
  if (!owner) return 0;
  if (const auto* table = owner->published.load(std::memory_order_acquire)) {
    const auto found = std::lower_bound(
        table->entries.begin(), table->entries.end(), encoded,
        [](const auto& entry, const auto* pointer) {
          return std::less<const std::uint8_t*>{}(entry.encoded, pointer);
        });
    if (found != table->entries.end() && found->encoded == encoded &&
        found->encoded_bytes == encoded_bytes &&
        found->wave->channels() == channels &&
        found->wave->sample_rate() == rate &&
        frames <= std::numeric_limits<std::size_t>::max() / channels &&
        found->wave->read(first_frame, {destination, frames * channels})) {
      return 1;
    }
  }
  owner->failures.fetch_add(1, std::memory_order_relaxed);
  return 0;
}
