// Source game receiver policy, independent from FACT cue scheduling. Prepare
// bounded GVTD/talk references before publication, then retain only typed cue
// IDs, fixed history and bounded channel handles. Core SL.con.3 (#rsl-bounds)
// checks all tables before indexing; Performance MEM.9 (guidelines/memory/
// MEM.9-embedded-allocate-at-init.md) keeps this policy's dispatch storage
// fixed. Native FACT cue allocation remains the backend's separate measured
// boundary. Both implementation corpora consulted; see
// animation-audio-presentation.md.
#include "character_audio.hpp"

#include <algorithm>
#include <limits>

#include "bw/content/formats/binary_reader.hpp"

namespace bw::game {
namespace {
#include "character_audio_compatibility.inc"
constexpr std::uint32_t kMissingCue = 0xffffffffU, kRetainedCue = 0x80000000U;
constexpr unsigned kBankShift = 16;
constexpr std::uint32_t kBankMask = 255, kCueMask = 0x7fff;
constexpr std::size_t kVoiceGroup = 22, kWeightRecordBytes = 24,
                      kWeightRecordCount = 8, kWeightGroupCount = 12,
                      kWeightActionCount = 16, kWeightMaximum = 2,
                      kWeightValues = 3, kWeightExtra = 20;
constexpr std::uint32_t kWeightVersion = 0x10000, kBodySoundFirst = 2039,
                        kBodySoundLast = 2053, kBodySoundOther = 22;
constexpr std::uint16_t kVoiceBank = 4, kStreamedVoiceBank = 50,
                        kPriorityMask = 0x7fff;
constexpr float kRetainedMinimumFrames = 6;
constexpr std::size_t kVariantDigits = 2, kDecimalRadix = 10,
                      kHistoryResetCandidates = 3,
                      kHistoryRememberCandidates = 2;
std::optional<CharacterSoundCue> unpack(std::uint32_t packed) noexcept {
  if (packed == kMissingCue) return {};
  return CharacterSoundCue{
      static_cast<std::uint16_t>((packed >> kBankShift) & kBankMask),
      static_cast<std::uint16_t>(packed & kCueMask),
      (packed & kRetainedCue) != 0};
}
std::optional<std::size_t> talk_cue(const formats::TextTable& talk,
                                    std::string_view action,
                                    std::size_t variant,
                                    const formats::AudioBankDefinition& bank) {
  // Native names are action_00; wav is a distinct authored property and may
  // deliberately name a different spell family. Never synthesize that cue.
  const auto name_size = action.size() + 1 + kVariantDigits;
  const auto children = talk.node(0, 0).children;
  for (std::size_t i = 0; i < children.count; ++i) {
    const auto entry = talk.node(1, children.first + i);
    if (entry.text.size() != name_size || !entry.text.starts_with(action) ||
        entry.text[action.size()] != '_' ||
        entry.text[action.size() + 1] != '0' + variant / kDecimalRadix ||
        entry.text[action.size() + 2] != '0' + variant % kDecimalRadix) {
      continue;
    }
    if (entry.children.count != 1) {
      resources::detail::unsupported(
          "Voice variation has multiple talk branches");
    }
    const auto branch = talk.node(2, entry.children.first);
    if (branch.children.count != 1) {
      resources::detail::unsupported(
          "Voice variation has multiple talk leaves");
    }
    const auto leaf = talk.node(3, branch.children.first);
    for (std::size_t j = 0; j < leaf.properties.count; ++j) {
      const auto property = talk.property(leaf.properties.first + j);
      if (property.name == "wav") return bank.find_cue(property.value);
    }
    return {};
  }
  return {};
}
}  // namespace
PreparedCharacterAudio::PreparedCharacterAudio(
    std::span<const std::uint8_t> voice_weights,
    const formats::TextTable& alicia_talk,
    const formats::AudioBankDefinition& voice_bank,
    const formats::AudioBankDefinition& streamed_voice_bank) {
  const resources::detail::Reader reader{voice_weights};
  if (reader.tag_view(0) != "GVTD" || reader.u32(4) != kWeightVersion ||
      alicia_talk.records(0) != 1 || alicia_talk.node(0, 0).text != "alicia" ||
      voice_bank.name() != "VOICE" ||
      streamed_voice_bank.name() != "GAME_VOICE") {
    resources::detail::malformed("Character voice inputs do not match Alicia");
  }
  const auto records = reader.u32(kWeightRecordCount);
  const auto groups = reader.u32(kWeightGroupCount);
  const auto actions = reader.u32(kWeightActionCount);
  if (records == 0 || groups <= kVoiceGroup || actions != kVoiceActions) {
    resources::detail::malformed("Invalid voice weight table dimensions");
  }
  const std::uint64_t directory =
      static_cast<std::uint64_t>(records) * kWeightRecordBytes;
  reader.range(0, directory);
  reader.range(directory, static_cast<std::uint64_t>(groups) * actions * 2);
  for (std::size_t action = 0; action < voices_.size(); ++action) {
    const auto record =
        reader.u16(directory + (kVoiceGroup * actions + action) * 2);
    if (record == 0) {
      continue;  // Original missing actions are intentional silence.
    }
    if (record >= records) {
      resources::detail::malformed("Voice weight index exceeds table");
    }
    const auto offset = static_cast<std::uint64_t>(record) * kWeightRecordBytes;
    const auto maximum = reader.u8(offset + kWeightMaximum);
    if (maximum >= kVoiceVariants || reader.u32(offset + kWeightExtra) != 0) {
      resources::detail::unsupported(
          "Unsupported character voice weight record");
    }
    auto& prepared = voices_.at(action);
    prepared.priority = reader.u16(offset) & kPriorityMask;
    prepared.available = maximum != 0;
    for (std::size_t variant = 1; variant <= maximum; ++variant) {
      auto& choice = prepared.variants.at(variant);
      choice.weight = reader.u8(offset + kWeightValues + variant);
      if (choice.weight == 0) continue;
      auto cue = talk_cue(alicia_talk, kVoiceNames.at(action), variant - 1,
                          voice_bank);
      choice.bank = kVoiceBank;
      if (!cue) {
        cue = talk_cue(alicia_talk, kVoiceNames.at(action), variant - 1,
                       streamed_voice_bank);
        choice.bank = kStreamedVoiceBank;
      }
      if (!cue) continue;  // Missing authored talk/cue stays unavailable.
      if (*cue > std::numeric_limits<std::uint16_t>::max()) {
        resources::detail::malformed("Voice cue index exceeds native width");
      }
      choice.cue = static_cast<std::uint16_t>(*cue);
      choice.available = true;
    }
  }
}
std::optional<CharacterSoundCue> PreparedCharacterAudio::sound(
    std::uint32_t action, std::optional<std::uint8_t> surface) noexcept {
  for (const auto& range : kDirectRanges) {
    if (action >= range.start && action - range.start < range.cues.size()) {
      return unpack(range.cues[action - range.start]);
    }
  }
  if (action >= kPlayerActions.size() || !surface) return {};
  const auto& entry = std::span{kPlayerActions}[action];
  for (const auto& choice : entry.surfaces) {
    if (choice.surface == *surface) return unpack(choice.cue);
  }
  return unpack(entry.fallback);
}
const CharacterVoiceAction* PreparedCharacterAudio::voice(
    std::uint32_t action) const noexcept {
  if (action >= voices_.size() || !std::span{voices_}[action].available) {
    return nullptr;
  }
  return &std::span{voices_}[action];
}
CharacterAudioResult CharacterAudio::voice(
    std::uint32_t action, CharacterAudioEnvironment& environment) noexcept {
  try {
    const auto* definition = prepared_.voice(action);
    if (definition == nullptr) return CharacterAudioResult::unavailable;
    if (voice_ && output_.active(*voice_) &&
        voice_priority_ <= definition->priority) {
      return CharacterAudioResult::suppressed;
    }
    std::array<bool, kVoiceVariants> excluded{};
    for (const auto& history : history_) {
      if (history.action == action) excluded.at(history.variant) = true;
    }
    std::uint32_t sum = 0;
    std::size_t candidates = 0;
    for (std::size_t i = 1; i < definition->variants.size(); ++i) {
      const auto& choice = definition->variants.at(i);
      if (!excluded.at(i) && choice.weight != 0) {
        sum += choice.weight;
        ++candidates;
      }
    }
    if (sum == 0) return CharacterAudioResult::suppressed;
    auto draw = environment.random_below(sum);
    if (draw >= sum) return CharacterAudioResult::backend_failure;
    std::size_t selected = 0;
    for (std::size_t i = 1; i < definition->variants.size(); ++i) {
      const auto& choice = definition->variants.at(i);
      if (excluded.at(i) || choice.weight == 0) continue;
      if (draw < choice.weight) {
        selected = i;
        break;
      }
      draw -= choice.weight;
    }
    const auto& choice = definition->variants.at(selected);
    if (!choice.available) return CharacterAudioResult::unavailable;
    const auto next =
        output_.play({choice.bank, choice.cue, environment.voice_position(),
                      audio::PauseDomain::world, audio::MixBus::voice});
    if (!next) return CharacterAudioResult::capacity;
    if (voice_) output_.stop(*voice_);
    voice_ = next;
    voice_priority_ = definition->priority;
    // Source clears exhausted alternatives before recording the accepted one.
    if (candidates < kHistoryResetCandidates) {
      for (auto& history : history_) {
        if (history.action == action) history.variant = 0;
      }
    }
    if (candidates >= kHistoryRememberCandidates) {
      history_.at(next_history_) = {action,
                                    static_cast<std::uint8_t>(selected)};
      next_history_ = (next_history_ + 1) % history_.size();
    }
    return CharacterAudioResult::played;
  } catch (const std::exception&) {
    return CharacterAudioResult::backend_failure;
  }
}
CharacterAudioResult CharacterAudio::stop_voice() noexcept {
  try {
    if (voice_) output_.stop(*voice_);
  } catch (const std::exception&) {
    return CharacterAudioResult::backend_failure;
  }
  voice_.reset();
  return CharacterAudioResult::stopped;
}
CharacterAudioChannel::CharacterAudioChannel(
    CharacterAudio& owner, CharacterAudioBank bank,
    std::size_t retained_capacity,
    const std::shared_ptr<memory::Domain>& domain)
    : owner_(owner),
      bank_(bank),
      retained_(retained_capacity, Retained{},
                memory::Allocator<Retained>(domain)) {}
CharacterAudioResult CharacterAudioChannel::voice(
    VoiceMotionCommand command,
    CharacterAudioEnvironment& environment) noexcept {
  return owner_.voice(command.identifier, environment);
}
CharacterAudioResult CharacterAudioChannel::sound(
    SoundMotionCommand command, animation::MotionEventPhase phase,
    const animation::MotionEventContext& context,
    CharacterAudioEnvironment& environment) noexcept {
  try {
    auto& output = owner_.output();
    if (phase != animation::MotionEventPhase::enter) {
      for (auto& retained : retained_) {
        if (!retained.handle || retained.action != command.action) continue;
        if (phase == animation::MotionEventPhase::update) {
          output.set_position(*retained.handle,
                              environment.bone_position(retained.bone));
        } else {
          output.stop(*retained.handle);
          retained.handle.reset();
        }
      }
      return phase == animation::MotionEventPhase::update
                 ? CharacterAudioResult::updated
                 : CharacterAudioResult::stopped;
    }
    if (bank_ == CharacterAudioBank::body &&
        (command.action == kBodySoundOther ||
         (command.action >= kBodySoundFirst &&
          command.action <= kBodySoundLast))) {
      return CharacterAudioResult::suppressed;
    }
    const auto emission = environment.sound_emission(command);
    if (emission.suppressed) return CharacterAudioResult::suppressed;
    const auto cue =
        PreparedCharacterAudio::sound(command.action, emission.surface);
    const auto baseline =
        PreparedCharacterAudio::sound(command.action, emission.default_surface);
    if (!cue || !baseline) return CharacterAudioResult::unavailable;
    const bool retained =
        baseline->retained &&
        static_cast<float>(context.event.end_frame) - context.current_frame >
            kRetainedMinimumFrames;
    if (baseline->retained && !retained) {
      return CharacterAudioResult::suppressed;
    }
    auto slot = retained_.end();
    if (retained) {
      slot = std::find_if(retained_.begin(), retained_.end(),
                          [](const auto& entry) { return !entry.handle; });
      if (slot == retained_.end()) return CharacterAudioResult::capacity;
    }
    const auto handle = output.play({cue->bank, cue->cue, emission.position});
    if (!handle) return CharacterAudioResult::capacity;
    if (retained) {
      *slot = {handle, command.action, command.bone};
    }
    return CharacterAudioResult::played;
  } catch (const std::exception&) {
    return CharacterAudioResult::backend_failure;
  }
}
CharacterAudioResult CharacterAudioChannel::reset() noexcept {
  auto result = CharacterAudioResult::stopped;
  for (auto& retained : retained_) {
    try {
      if (retained.handle) owner_.output().stop(*retained.handle);
    } catch (const std::exception&) {
      result = CharacterAudioResult::backend_failure;
      continue;
    }
    retained.handle.reset();
  }
  return result;
}
}  // namespace bw::game
