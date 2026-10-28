// Global voice implementation reread Core I.1 and performance MEM.9:
// explicit world ownership, fixed source slot/history capacity and no local
// clock. FACT GetState supplies completion only at an admitted pool update.
// Source game receiver policy, independent from FACT cue scheduling. Prepare
// bounded GVTD/talk references before publication, then retain only typed cue
// IDs, fixed history and bounded channel handles. Core SL.con.3 (#rsl-bounds)
// checks all tables before indexing; Performance MEM.9 (guidelines/memory/
// MEM.9-embedded-allocate-at-init.md) keeps this policy's dispatch storage
// fixed. Native FACT cue allocation remains the backend's separate measured
// boundary. Both implementation corpora consulted; see
// animation-audio-presentation.md. Generic profiles reread E.4/MEM.9:
// validate namespace/group before publication and retain fixed cue metadata.
#include "character_audio.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>

#include "bw/content/formats/binary_reader.hpp"

namespace bw::game {
namespace {
#include "character_audio_compatibility.inc"
constexpr std::uint32_t kMissingCue = 0xffffffffU, kRetainedCue = 0x80000000U;
constexpr unsigned kBankShift = 16;
constexpr std::uint32_t kBankMask = 255, kCueMask = 0x7fff;
constexpr std::size_t kWeightRecordBytes = 24, kWeightRecordCount = 8,
                      kWeightGroupCount = 12, kWeightActionCount = 16,
                      kWeightMaximum = 2, kWeightValues = 3, kWeightExtra = 20;
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
CharacterAudioProfile original_character_audio_profile(
    std::string_view model, std::uint32_t actor_flags,
    std::uint32_t voice_group) {
  constexpr std::uint32_t kTransformed = 0x8000;
  constexpr std::uint8_t kTransformedClass = 9;
  std::uint8_t sound_class{};
  if ((actor_flags & kTransformed) != 0) {
    sound_class = kTransformedClass;
  } else if (model.starts_with("p")) {
    sound_class = 0;
  } else if (model.size() >= 3 && model.front() == 'h') {
    const int variant = std::clamp(static_cast<int>(model[2]) - '0', 0, 2);
    sound_class =
        static_cast<std::uint8_t>(variant * 2 + (model[1] == 'w' ? 2 : 1));
  } else if (model.size() >= 2 && model.front() == 'e') {
    constexpr std::array<std::uint8_t, 7> classes{8, 12, 10, 11, 9, 14, 13};
    const auto kind = model[1] - 'a';
    if (kind < 0 || static_cast<std::size_t>(kind) >= classes.size()) {
      resources::detail::unsupported("Unknown original character sound class");
    }
    sound_class = classes.at(static_cast<std::size_t>(kind));
  } else {
    resources::detail::unsupported("Unknown original character sound class");
  }
  // Original22-entry voice identity table contains groups1..22. Transformed
  // actors use wormman_a/b, selected by the table's original variant byte.
  if (voice_group == 0 || voice_group >= kVoiceGroups.size()) voice_group = 0;
  if (voice_group != 0 && (actor_flags & kTransformed) != 0) {
    constexpr std::uint32_t kWormman = 19;
    constexpr std::array<bool, 23> alternate_groups{
        false, false, false, false, false, false, false, false,
        false, false, false, false, false, true,  true,  true,
        false, true,  false, false, true,  false, true};
    const bool alternate = alternate_groups.at(voice_group);
    voice_group = kWormman + static_cast<std::uint32_t>(alternate);
  }
  return {sound_class, static_cast<std::uint8_t>(voice_group)};
}
std::string_view character_voice_group_name(std::uint8_t group) {
  return kVoiceGroups.at(group);
}
PreparedCharacterAudio PreparedCharacterAudio::silent(
    CharacterAudioProfile profile) {
  if (profile.voice_group != 0) {
    throw std::invalid_argument("Nonzero actor voice group is not silence");
  }
  return PreparedCharacterAudio(profile);
}
PreparedCharacterAudio::PreparedCharacterAudio(
    std::span<const std::uint8_t> voice_weights, const formats::TextTable& talk,
    const formats::AudioBankDefinition& voice_bank,
    const formats::AudioBankDefinition& streamed_voice_bank,
    CharacterAudioProfile profile)
    : profile_(profile) {
  const resources::detail::Reader reader{voice_weights};
  if (reader.tag_view(0) != "GVTD" || reader.u32(4) != kWeightVersion ||
      talk.records(0) != 1 || profile.voice_group == 0 ||
      talk.node(0, 0).text != character_voice_group_name(profile.voice_group) ||
      voice_bank.name() != "VOICE" ||
      streamed_voice_bank.name() != "GAME_VOICE") {
    resources::detail::malformed(
        "Character voice inputs do not match original profile");
  }
  const auto records = reader.u32(kWeightRecordCount);
  const auto groups = reader.u32(kWeightGroupCount);
  const auto actions = reader.u32(kWeightActionCount);
  if (records == 0 || groups <= profile.voice_group ||
      actions != kVoiceActions) {
    resources::detail::malformed("Invalid voice weight table dimensions");
  }
  const std::uint64_t directory =
      static_cast<std::uint64_t>(records) * kWeightRecordBytes;
  reader.range(0, directory);
  reader.range(directory, static_cast<std::uint64_t>(groups) * actions * 2);
  for (std::size_t action = 0; action < voices_.size(); ++action) {
    const auto record = reader.u16(
        directory +
        (static_cast<std::size_t>(profile.voice_group) * actions + action) * 2);
    if (record == 0) {
      continue;  // Original missing actions are intentional silence.
    }
    if (record >= records) {
      resources::detail::malformed("Voice weight index exceeds table");
    }
    const auto offset = static_cast<std::uint64_t>(record) * kWeightRecordBytes;
    const auto maximum = reader.u8(offset + kWeightMaximum);
    if (maximum >= kVoiceVariants) {
      resources::detail::unsupported(
          "Unsupported character voice weight record");
    }
    auto& prepared = voices_.at(action);
    prepared.maximum_delay_seconds =
        std::bit_cast<float>(reader.u32(offset + kWeightExtra));
    if (!std::isfinite(prepared.maximum_delay_seconds) ||
        prepared.maximum_delay_seconds < 0) {
      resources::detail::malformed("Invalid character voice delay");
    }
    prepared.priority = reader.u16(offset) & kPriorityMask;
    prepared.available = maximum != 0;
    for (std::size_t variant = 1; variant <= maximum; ++variant) {
      auto& choice = prepared.variants.at(variant);
      choice.weight = reader.u8(offset + kWeightValues + variant);
      if (choice.weight == 0) continue;
      auto cue =
          talk_cue(talk, kVoiceNames.at(action), variant - 1, voice_bank);
      choice.bank = kVoiceBank;
      if (!cue) {
        cue = talk_cue(talk, kVoiceNames.at(action), variant - 1,
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
    std::uint32_t action, std::optional<std::uint8_t> surface,
    std::uint8_t source_class) noexcept {
  for (const auto& range : kDirectRanges) {
    if (action >= range.start && action - range.start < range.cues.size()) {
      return unpack(range.cues[action - range.start]);
    }
  }
  if (action >= kActorActions.size() || !surface) return {};
  const auto groups = std::span{kActorActions}[action];
  const auto selected = groups.size() == 1 ? 0 : source_class;
  if (static_cast<std::size_t>(selected) >= groups.size()) return {};
  const auto& entry = groups[static_cast<std::size_t>(selected)];
  for (const auto& choice : entry.surfaces) {
    if (choice.surface == *surface) return unpack(choice.cue);
  }
  return unpack(entry.fallback);
}
std::optional<CharacterSoundCue> PreparedCharacterAudio::actor_sound(
    std::uint32_t action, std::optional<std::uint8_t> surface) const noexcept {
  return sound(action, surface, profile_.sound_class);
}
const CharacterVoiceAction* PreparedCharacterAudio::voice(
    std::uint32_t action) const noexcept {
  if (action >= voices_.size() || !std::span{voices_}[action].available) {
    return nullptr;
  }
  return &std::span{voices_}[action];
}
CharacterAudio::CharacterAudio(const PreparedCharacterAudio& prepared,
                               audio::CueOutput& output,
                               CharacterVoicePool& pool,
                               CharacterVoiceRole role)
    : prepared_(prepared),
      output_(output),
      pool_(pool),
      role_(role),
      identity_(pool.identify()) {}
CharacterAudio::~CharacterAudio() { (void)stop_voice(); }
std::uint64_t CharacterVoicePool::identify() {
  if (next_identity_ == std::numeric_limits<std::uint64_t>::max()) {
    throw std::overflow_error("Actor voice identity exhausted");
  }
  return ++next_identity_;
}
void CharacterVoicePool::release(CharacterAudio& actor) noexcept {
  for (auto& slot : slots_) {
    if (slot == &actor) slot = nullptr;
  }
  actor.admitted_ = false;
  actor.environment_ = nullptr;
}
CharacterAudioResult CharacterVoicePool::stop_all() noexcept {
  auto result = CharacterAudioResult::stopped;
  for (auto* actor : slots_) {
    if (actor && actor->stop_voice() == CharacterAudioResult::backend_failure) {
      result = CharacterAudioResult::backend_failure;
    }
  }
  return result;
}
void CharacterVoicePool::reset() {
  if (std::any_of(slots_.begin(), slots_.end(),
                  [](const auto* actor) { return actor != nullptr; })) {
    throw std::logic_error("Voice pool reset before actor retirement");
  }
  history_ = {};
  next_history_ = 0;
  player_position_ = {};
}
CharacterAudioResult CharacterVoicePool::advance(
    float delta, math::Vector3<float> player_position) noexcept {
  if (!std::isfinite(delta) || delta < 0 ||
      !math::all_finite(player_position)) {
    return CharacterAudioResult::backend_failure;
  }
  player_position_ = player_position;
  // Source manager walks stream slots first, then memory slots. This is also
  // the order of delay RNG draws; individual actors cannot advance it twice.
  for (auto* actor : slots_) {
    if (actor == nullptr) continue;
    const auto result = actor->advance_voice(delta);
    if (result == CharacterAudioResult::capacity ||
        result == CharacterAudioResult::backend_failure) {
      return result;
    }
  }
  return CharacterAudioResult::updated;
}
CharacterAudioResult CharacterAudio::voice(
    std::uint32_t action, CharacterAudioEnvironment& environment) noexcept {
  try {
    if (voice_generation_ == std::numeric_limits<std::uint64_t>::max()) {
      return CharacterAudioResult::capacity;
    }
    const auto* definition = prepared_.voice(action);
    if (definition == nullptr) return CharacterAudioResult::unavailable;
    if (admitted_ && voice_priority_ <= definition->priority) {
      return CharacterAudioResult::suppressed;
    }
    const auto group = prepared_.profile().voice_group;
    // Every ordinary actor request enters before the bank is resolved. The
    // original repeat gate excludes the player, but shares history globally.
    if (role_ != CharacterVoiceRole::player) {
      for (std::size_t i = 0; i < pool_.history_.size(); ++i) {
        const auto index =
            (pool_.next_history_ + pool_.history_.size() - i - 1) %
            pool_.history_.size();
        const auto& previous = pool_.history_.at(index);
        if (previous.actor != identity_) continue;
        if (previous.action == action) return CharacterAudioResult::suppressed;
        break;
      }
    }
    std::array<bool, kVoiceVariants> excluded{};
    for (const auto& history : pool_.history_) {
      if (history.group == group && history.action == action) {
        excluded.at(history.variant) = true;
      }
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
    const auto begin =
        choice.bank == kStreamedVoiceBank ? 0U : kStreamVoiceSlots;
    const auto end = choice.bank == kStreamedVoiceBank ? kStreamVoiceSlots
                                                       : pool_.slots_.size();
    std::size_t selected_slot = end;
    for (std::size_t i = begin; i < end; ++i) {
      if (pool_.slots_.at(i) == nullptr) {
        selected_slot = i;
        break;
      }
    }
    const auto voice_class = [](std::uint8_t value) {
      constexpr std::uint8_t kPlayerGroup = 22, kEnemyGroupBegin = 16,
                             kCivilianGroupEnd = 4;
      return value == kPlayerGroup
                 ? 2
                 : (value <= kCivilianGroupEnd || value >= kEnemyGroupBegin
                        ? 1
                        : 0);
    };
    if (selected_slot == end) {
      // Select a strictly lower-ranked occupied slot. Equal ranks retain the
      // incumbent. Same action/class compares proximity rather than priority.
      const CharacterAudio* candidate = this;
      auto candidate_action = action;
      auto candidate_priority = definition->priority;
      auto candidate_position = environment.voice_position();
      for (std::size_t i = begin; i < end; ++i) {
        const auto& occupied = *pool_.slots_.at(i);
        bool displace = candidate_priority < occupied.voice_priority_;
        if (candidate != &occupied &&
            candidate_action == occupied.voice_action_ &&
            voice_class(candidate->prepared_.profile().voice_group) ==
                voice_class(occupied.prepared_.profile().voice_group)) {
          const auto near =
              math::subtract(candidate_position, pool_.player_position_);
          const auto far = math::subtract(
              occupied.environment_->voice_position(), pool_.player_position_);
          displace = math::dot(near, near) < math::dot(far, far);
        }
        if (displace) {
          selected_slot = i;
          candidate = &occupied;
          candidate_action = occupied.voice_action_;
          candidate_priority = occupied.voice_priority_;
          candidate_position = occupied.environment_->voice_position();
        }
      }
      if (selected_slot == end) return CharacterAudioResult::suppressed;
    }
    auto* displaced = pool_.slots_.at(selected_slot);
    if (displaced &&
        displaced->stop_voice() == CharacterAudioResult::backend_failure) {
      return CharacterAudioResult::backend_failure;
    }
    if (stop_voice() == CharacterAudioResult::backend_failure) {
      return CharacterAudioResult::backend_failure;
    }
    voice_priority_ = definition->priority;
    voice_action_ = action;
    environment_ = &environment;
    pending_voice_ = PendingVoice{choice, definition->maximum_delay_seconds};
    admitted_ = true;
    pool_.slots_.at(selected_slot) = this;
    ++voice_generation_;
    if (candidates < kHistoryResetCandidates) {
      for (auto& history : pool_.history_) {
        if (history.group == group && history.action == action) {
          history.variant = 0;
        }
      }
    }
    if (candidates >= kHistoryRememberCandidates) {
      pool_.history_.at(pool_.next_history_) = {
          identity_, action, group, static_cast<std::uint8_t>(selected)};
      pool_.next_history_ = (pool_.next_history_ + 1) % pool_.history_.size();
    }
    return CharacterAudioResult::played;
  } catch (const std::exception&) {
    return CharacterAudioResult::backend_failure;
  }
}
CharacterAudioResult CharacterAudio::advance_voice(float delta) noexcept {
  try {
    if (environment_ == nullptr) return CharacterAudioResult::updated;
    if (!pending_voice_) {
      if (voice_ && output_.active(*voice_)) {
        output_.set_position(*voice_, environment_->voice_position());
      } else {
        return stop_voice();
      }
      return CharacterAudioResult::updated;
    }
    auto& pending = *pending_voice_;
    if (!pending.initialized) {
      if (pending.maximum_delay > 0) {
        constexpr std::uint32_t kPositiveRange = 0x7fffffff;
        constexpr float kUnitScale = 0x1p-31F;
        const auto draw = environment_->random_below(kPositiveRange);
        if (draw >= kPositiveRange) {
          return CharacterAudioResult::backend_failure;
        }
        pending.remaining =
            static_cast<float>(draw) * kUnitScale * pending.maximum_delay;
      }
      pending.initialized = true;
      if (pending.choice.bank == kStreamedVoiceBank) {
        return CharacterAudioResult::updated;
      }
    }
    if (pending.remaining > 0) {
      pending.remaining -= delta;
      return CharacterAudioResult::updated;
    }
    const auto next =
        output_.play({pending.choice.bank, pending.choice.cue,
                      environment_->voice_position(), audio::PauseDomain::world,
                      audio::MixBus::voice});
    if (!next) return CharacterAudioResult::capacity;
    voice_ = next;
    pending_voice_.reset();
    return CharacterAudioResult::played;
  } catch (const std::exception&) {
    return CharacterAudioResult::backend_failure;
  }
}
bool CharacterAudio::voice_active() const { return admitted_; }
bool CharacterAudio::voice_active(CharacterVoiceRequest request) const {
  if (request.owner_ != this) {
    throw std::invalid_argument("Voice request belongs to another actor");
  }
  return request.generation_ != 0 && request.generation_ == voice_generation_ &&
         admitted_;
}
CharacterAudioResult CharacterAudio::stop_voice() noexcept {
  const auto handle = voice_;
  voice_.reset();
  pending_voice_.reset();
  pool_.release(*this);
  try {
    if (handle) output_.stop(*handle);
  } catch (const std::exception&) {
    return CharacterAudioResult::backend_failure;
  }
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
        owner_.prepared().actor_sound(command.action, emission.surface);
    const auto baseline =
        owner_.prepared().actor_sound(command.action, emission.default_surface);
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
    // Source actor context is emitted after cue resolution and before backend
    // creation, so device capacity/failure cannot erase audible-world intent.
    constexpr std::uint32_t kActorSoundActions = 22;
    if (command.action < kActorSoundActions) {
      environment.emit_actor_sound(command.action, emission.position);
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
