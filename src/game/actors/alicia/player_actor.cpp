// Ownership extraction: design I.1/C.9 and implementation E.6 (#re-raii)
// retain stable actor/controller borrows. Both-phase MEM.9
// (guidelines/memory/MEM.9-embedded-allocate-at-init.md) keeps existing
// prepared buffers; update-only publication cannot replace draws, lights or
// scene view. Actor publication borrows ActorPublisher only; it cannot upload,
// bind or retire mission resources. Both-phase C.121 (#rh-abstract), I.11
// (#ri-raw), COPY.5
// (guidelines/copy-move/COPY.5-resource-owning-types-are-move-only.md) enforce
// the existing owner split without changing frame work or lifetimes. Complete
// CPU pose/events before the next parent tick; GPU publication only submits
// retained output. Implementation E.4/MEM.9 preserve phase invariants and fixed
// storage, following the header and actor-model-clock-order.md. Raised facing
// samples its raw authored reference; movement retains its separate corrected
// basis. Both-phase I.1/MEM.9 keep consumer meaning explicit and reuse prepared
// animation data without new per-frame storage. Secondary motion consumes the
// clean pose once per completed model update, before attachments/palettes. The
// header records ordering and both-phase I.11/MEM.9 ownership/allocation
// contracts; costume-secondary-motion.md records recovered solver policy and
// the inspected Unreal reset/update boundary. Assemble original selected
// costume, prepared clips and bone-driven weapon. Contract/algorithm and
// both-phase I.11/CDSA.32/CACHE.4 plus implementation SL.con.3 are in
// player_actor.hpp. Model/clip ownership remains with mission construction;
// runtime evaluates reusable data and never fabricates collision. E.4/MEM.9
// retain prepared event timelines and independent source controller clocks
// without hot-path parsing. Reference bases are prepared once for verified
// traversal clips; live model height comes from the validated body radius. I.1
// keeps these game semantics out of generic sampling. Weapon root suppression
// follows the model constructor's source policy; I.1/MEM.9 (both phases) make
// the attachment space explicit without extra storage or clip-specific
// corrections. See weapon-discharge-attachment.md.
#include "player_actor.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>
#include <numbers>
#include <stdexcept>

#include "animation/alicia_motion_bindings.hpp"
#include "animation/pose_semantics.hpp"
#include "bw/content/formats/binary_reader.hpp"
#include "bw/math/vector.hpp"
#include "bw/spatial/static_collision.hpp"
#include "game/actors/alicia/animation/aim_posture.hpp"
#include "game/actors/alicia/animation/facial_animation.hpp"
#include "game/actors/alicia/audio/character_audio.hpp"
#include "game/session/compatibility/source_random.hpp"

namespace bw::game {
namespace {
const formats::SkeletonDefinition& skeleton(const ResidentModelView& model) {
  if (!model.definition.get().skeleton()) {
    throw std::runtime_error("Player model requires an authored skeleton");
  }
  return *model.definition.get().skeleton();
}
constexpr float kInitialModelVerticalOffset = -0.5F;
animation::Matrix model_world(
    const ScriptedPlayerPose& placement,
    float vertical_offset = kInitialModelVerticalOffset) {
  const float radians =
      placement.yaw_degrees * std::numbers::pi_v<float> / 180.0F;
  formats::SkeletonTransform transform;
  transform.rotation = {0, std::sin(radians / 2), 0, std::cos(radians / 2)};
  transform.translation = placement.position;
  transform.translation[1] += vertical_offset;
  transform.scale = {1, 1, 1};
  formats::SkeletonTransform basis;
  basis.rotation = {0, 1, 0, 0};
  basis.scale = {1, 1, 1};
  // The original model child changes facing basis before its actor transform.
  return animation::multiply(animation::transform_matrix(basis),
                             animation::transform_matrix(transform));
}
animation::Matrix root_basis(const formats::SkeletonTransform& transform) {
  const auto reference = animation::transform_matrix(transform);
  const math::Vector3<float> authored_up{reference[1][0], reference[1][1],
                                         reference[1][2]};
  auto forward =
      math::Vector3<float>{reference[2][0], reference[2][1], -reference[2][2]};
  auto right = math::cross(authored_up, forward);
  const auto right_length = math::length(right),
             forward_length = math::length(forward);
  if (!std::isfinite(right_length) || !std::isfinite(forward_length) ||
      right_length <= 0 || forward_length <= 0) {
    throw std::runtime_error("Invalid player root reference basis");
  }
  right = math::divide(right, right_length);
  forward = math::divide(forward, forward_length);
  const auto up = math::cross(forward, right);
  auto result = animation::kIdentityMatrix;
  for (std::size_t axis = 0; axis < right.size(); ++axis) {
    result[0].at(axis) = right.at(axis);
    result[1].at(axis) = up.at(axis);
    result[2].at(axis) = forward.at(axis);
  }
  return result;
}
// Traversal actions bind native slots; the catalog owns all publication data.
constexpr std::array<std::size_t, 17> kMotionSlots{1,   0,   16,  721, 704, 709,
                                                   710, 711, 712, 713, 714, 718,
                                                   719, 720, 715, 716, 717};
void validate_appearance(const PlayerActorSource& source,
                         const PlayerActorSource& current) {
  const auto& before = current.body.definition.get();
  const auto& after = source.body.definition.get();
  const auto& old_bones = skeleton(current.body).bones();
  const auto& new_bones = skeleton(source.body).bones();
  if (source.weapon.handle != current.weapon.handle ||
      &source.support.get() != &current.support.get() ||
      &source.weapon_support.get() != &current.weapon_support.get() ||
      old_bones.size() != new_bones.size() ||
      !std::ranges::equal(before.bone_names(), after.bone_names(),
                          [](const auto& left, const auto& right) {
                            return left.bone == right.bone &&
                                   left.name == right.name;
                          }) ||
      !std::ranges::equal(old_bones, new_bones,
                          [](const auto& left, const auto& right) {
                            return left.parent == right.parent;
                          })) {
    throw std::invalid_argument(
        "Costume skeleton, support or weapon is incompatible");
  }
}
}  // namespace
// Stable appearance storage keeps the prepared-name/pose borrowing valid when
// a paused candidate is adopted into the persistent actor address.
struct PlayerActor::ShapeState {
  ShapeState(const PlayerActorSource& source,
             const std::shared_ptr<memory::Domain>& domain)
      : prepared(source.body.definition.get(), domain),
        pose(prepared, domain),
        face(source.support.get(), prepared, domain),
        updates(memory::Allocator<render::WorldShapePositionUpdate>(domain)) {
    updates.reserve(prepared.buffers().size());
    for (std::size_t buffer = 0; buffer < prepared.buffers().size(); ++buffer) {
      updates.push_back({source.body.handle, buffer, pose.positions(buffer)});
    }
  }
  void evaluate(double seconds) {
    face.advance(seconds);
    pose.evaluate(face.weights());
    for (auto& update : updates) {
      update.positions = pose.positions(update.buffer);
    }
  }
  animation::PreparedModelShapes prepared;
  animation::ShapePose pose;
  FacialAnimation face;
  memory::Vector<render::WorldShapePositionUpdate> updates;
};
// Source sound emission queries use current evaluated bones and authored
// support metadata. The manager's query override starts false; ordinary
// Alicia descriptor flags start1, so only the recovered near-action set
// queries support here. No visibility/frustum substitute is introduced.
struct PlayerActor::AudioState final : CharacterAudioEnvironment {
  static constexpr std::size_t kRetainedSounds = 128;
  AudioState(PlayerActor& player, PlayerActorAudioSource source,
             const std::shared_ptr<memory::Domain>& domain)
      : actor(player),
        sources(source),
        owner(source.prepared, source.output),
        body(owner, CharacterAudioBank::body, kRetainedSounds, domain),
        upper(owner, CharacterAudioBank::upper, kRetainedSounds, domain) {}
  AudioState(const AudioState&) = delete;
  AudioState& operator=(const AudioState&) = delete;
  AudioState(AudioState&&) = delete;
  AudioState& operator=(AudioState&&) = delete;
  ~AudioState() override {
    (void)body.reset();
    (void)upper.reset();
    (void)owner.stop_voice();
  }
  math::Vector3<float> bone_position(std::int32_t bone) const override {
    const auto bones = actor.body_bones();
    if (bone > 0 && static_cast<std::size_t>(bone) < bones.size()) {
      return math::translation(bones[static_cast<std::size_t>(bone)]);
    }
    return math::translation(actor.model_world_);
  }
  math::Vector3<float> voice_position() const override {
    return actor.source_.placement.position;
  }
  std::uint32_t random_below(std::uint32_t upper_bound) override {
    return sources.random.get().next(upper_bound);
  }
  CharacterSoundEmission sound_emission(
      SoundMotionCommand command) const override {
    const auto action = command.action;
    CharacterSoundEmission result{.position = bone_position(command.bone),
                                  .surface = 0,
                                  .default_surface = 0};
    constexpr float kNearSquared = 225;
    constexpr std::uint32_t kNearFirstEnd = 5, kNearSecondBegin = 17,
                            kNearSecondEnd = 19;
    const bool near_action =
        action <= kNearFirstEnd ||
        (action >= kNearSecondBegin && action <= kNearSecondEnd);
    const auto difference = math::subtract(result.position, listener.position);
    if (!near_action || math::dot(difference, difference) >= kNearSquared) {
      result.surface = 1;
      return result;
    }
    auto origin = result.position, target = result.position;
    origin[1] += actor.sound_probe_half_extent_;
    target[1] -= actor.sound_probe_half_extent_;
    const auto contact =
        sources.collision.get().clip_segment({origin, target, .01F, 0});
    if (contact.clipped) {
      result.position = contact.position;
      result.surface = contact.surface.value_or(0);
    }
    return result;
  }
  PlayerActor& actor;
  PlayerActorAudioSource sources;
  CharacterAudio owner;
  CharacterAudioChannel body, upper;
  audio::Listener listener;
};
// The action clocks/effects have stable ownership across appearance swaps.
// Until an effect's concrete consumer is installed, retain a visible diagnostic
// count rather than claiming that a decoded event produced gameplay behavior.
struct PlayerActor::Playback {
  static constexpr std::size_t kBodyCapacity = 4, kUpperCapacity = 2,
                               kAdditiveCapacity = 1, kWeaponCapacity = 4;
  struct Catalogs {
    std::reference_wrapper<const CharacterAnimations> body, weapon;
  };
  struct Sink final : MotionPresentationSink {
    static constexpr std::size_t kVisibilityChannel = 8, kFacialChannel = 3,
                                 kVoiceChannel = 1;
    static constexpr std::int32_t kWeaponListener = 1000;
    static constexpr std::uint32_t kWeaponGroupOne = 2, kWeaponGroupsOneTwo = 6;
    Sink(CharacterPresentationStatus& state, std::uint32_t& groups,
         FacialAnimation*& face_owner)
        : status(state), weapon_groups(groups), face(face_owner) {}
    void visibility(VisibilityMotionCommand command) noexcept override {
      if (command.target == 1) return;  // Registered source no-op listener.
      if (command.target != kWeaponListener) {
        ++status.unresolved_entries[kVisibilityChannel];
        return;
      }
      if (command.enabled) {
        weapon_groups |= kWeaponGroupOne;
      } else {
        weapon_groups &= ~kWeaponGroupsOneTwo;
        // Weapon classes may also request an attachment effect. The group
        // operation is proven; effect creation still needs its owning service.
        ++status.unresolved_entries[kVisibilityChannel];
      }
    }
    void facial(FacialMotionCommand command) noexcept override {
      if (face == nullptr) {
        ++status.unresolved_entries[kFacialChannel];
        return;
      }
      const auto admission = face->command(command, true);
      if (admission != FacialAdmission::applied &&
          admission != FacialAdmission::retired &&
          admission != FacialAdmission::absent_shapes) {
        ++status.unresolved_entries[kFacialChannel];
      }
    }
    void audio_result(CharacterAudioResult result,
                      std::size_t channel) noexcept {
      if (result == CharacterAudioResult::unavailable ||
          result == CharacterAudioResult::capacity ||
          result == CharacterAudioResult::backend_failure) {
        ++std::span(status.unresolved_entries)[channel];
      }
    }
    void voice(VoiceMotionCommand command) noexcept override {
      if (audio_channel != nullptr && audio_environment != nullptr) {
        audio_result(audio_channel->voice(command, *audio_environment),
                     kVoiceChannel);
      } else {
        ++status.unresolved_entries[kVoiceChannel];
      }
    }
    void sound(SoundMotionCommand command, animation::MotionEventPhase phase,
               const animation::MotionEventContext& context) noexcept override {
      if (audio_channel != nullptr && audio_environment != nullptr) {
        audio_result(
            audio_channel->sound(command, phase, context, *audio_environment),
            0);
      } else {
        unsupported(phase, context);
      }
    }
    void unsupported(
        animation::MotionEventPhase phase,
        const animation::MotionEventContext& context) noexcept override {
      if (phase == animation::MotionEventPhase::enter &&
          context.channel < status.unresolved_entries.size()) {
        ++std::span(status.unresolved_entries)[context.channel];
      }
    }
    void reset_presentation() noexcept override {
      // Source resets every registered listener, not only acquired positives.
      weapon_groups |= kWeaponGroupOne;
      if (face != nullptr) face->reset_motion_banks();
      if (audio_channel != nullptr) audio_result(audio_channel->reset(), 0);
    }
    CharacterAudioChannel* audio_channel{};
    CharacterAudioEnvironment* audio_environment{};
    CharacterPresentationStatus& status;
    std::uint32_t& weapon_groups;
    FacialAnimation*& face;
  };
  Playback(Catalogs catalogs, const std::shared_ptr<memory::Domain>& domain)
      : body_sink(status, weapon_groups, face),
        upper_sink(status, weapon_groups, face),
        additive_sink(status, weapon_groups, face),
        weapon_sink(status, weapon_groups, face),
        body(catalogs.body.get(), body_sink, kBodyCapacity, domain),
        upper(catalogs.body.get(), upper_sink, kUpperCapacity, domain),
        additive(catalogs.body.get(), additive_sink, kAdditiveCapacity,
                 domain) {}
  CharacterMotionPlayer& bank(CharacterMotionBank value) {
    switch (value) {
      case CharacterMotionBank::body:
        return body;
      case CharacterMotionBank::upper_body:
        return upper;
      case CharacterMotionBank::additive:
        return additive;
      case CharacterMotionBank::weapon:
        break;
    }
    throw std::invalid_argument("Unknown character motion bank");
  }
  std::shared_ptr<AudioState> audio;
  AimPostureBlend aim_blend;
  math::Vector3<float> aim_target{0, 0, 1};
  bool aim_enabled{};
  bool support_arm_permitted{};
  CharacterPresentationStatus status;
  std::uint32_t weapon_groups{std::numeric_limits<std::uint32_t>::max()};
  FacialAnimation* face{};
  Sink body_sink, upper_sink, additive_sink, weapon_sink;
  CharacterMotionPlayer body, upper, additive;
};
PlayerActor::~PlayerActor() = default;
PlayerActor::PlayerActor(PlayerActorSource source,
                         const std::shared_ptr<memory::Domain>& domain,
                         const std::function<void()>& progress)
    : PlayerActor(source, domain, nullptr, progress) {}
PlayerActor::PlayerActor(PlayerActorSource source,
                         const std::shared_ptr<memory::Domain>& domain,
                         const PlayerActor* shared_appearance,
                         const std::function<void()>& progress,
                         PlayerAppearanceChange change, bool weapon_raised)
    : source_(source),
      model_world_(model_world(source.placement)),
      body_pose_(skeleton(source.body), domain),
      weapon_pose_(skeleton(source.weapon), domain),
      body_release_(skeleton(source.body).bones().size(), domain),
      modifier_overrides_(
          memory::Allocator<animation::BoneWorldOverride>(domain)),
      palettes_(memory::Allocator<Palette>(domain)),
      updates_(memory::Allocator<render::WorldPoseUpdate>(domain)),
      visibility_updates_(
          memory::Allocator<render::WorldVisibilityUpdate>(domain)) {
  const auto& body = source.body.definition.get();
  // Source geometry extent controls the support-sound ray, independently from
  // capsule dimensions. Cache each appearance's own authored value once.
  constexpr std::uint64_t kGeometryHeight = 0x40;
  const resources::detail::Reader geometry_reader{body.header()};
  sound_probe_half_extent_ =
      std::bit_cast<float>(geometry_reader.u32(kGeometryHeight)) / 2;
  if (!std::isfinite(sound_probe_half_extent_) ||
      sound_probe_half_extent_ <= 0) {
    throw std::runtime_error("Invalid authored audio support probe extent");
  }

  const auto attachment = std::ranges::find(body.bone_names(), "RightWeapon",
                                            &formats::ModelBoneName::name);
  if (attachment == body.bone_names().end()) {
    throw std::runtime_error("Player model lacks the weapon attachment bone");
  }
  attachment_bone_ = attachment->bone;
  constexpr std::array<std::string_view, kAimChainCapacity> aim_names{
      "Hipsbone", "Spine", "Spine1", "Spine2", "Spine3", "Shoots_direction"};
  for (std::size_t index = 0; index < aim_names.size(); ++index) {
    const auto bone = std::ranges::find(body.bone_names(), aim_names.at(index),
                                        &formats::ModelBoneName::name);
    // Original case-sensitive name binding skips missing names. The installed
    // table's HipsBone does not alias the source Hipsbone request.
    if (bone != body.bone_names().end()) {
      aim_chain_.at(aim_chain_size_++) = bone->bone;
    }
  }
  if (aim_chain_size_ < 2) {
    throw std::runtime_error("Player model has no usable authored aim chain");
  }
  const auto root_reference = std::ranges::find(
      body.bone_names(), "Waist_direction", &formats::ModelBoneName::name);
  if (root_reference == body.bone_names().end()) {
    throw std::runtime_error("Player model lacks its movement reference bone");
  }
  if (change != PlayerAppearanceChange::costume &&
      change != PlayerAppearanceChange::weapon &&
      change != PlayerAppearanceChange::live_weapon) {
    throw std::invalid_argument("Unknown appearance change");
  }
  const bool retain_playback =
      shared_appearance != nullptr && change != PlayerAppearanceChange::weapon;
  const bool retain_weapon =
      shared_appearance != nullptr && change == PlayerAppearanceChange::costume;
  if (shared_appearance != nullptr) {
    if (retain_weapon) {
      validate_appearance(source, shared_appearance->source_);
    } else if (source.body.handle != shared_appearance->source_.body.handle ||
               &source.body.definition.get() !=
                   &shared_appearance->source_.body.definition.get() ||
               &source.support.get() !=
                   &shared_appearance->source_.support.get()) {
      throw std::invalid_argument("Weapon replacement changed body resources");
    }
    body_catalog_ = shared_appearance->body_catalog_;
  } else {
    body_catalog_ = std::allocate_shared<CharacterAnimations>(
        memory::Allocator<CharacterAnimations>(domain), source.support.get(),
        skeleton(source.body), "pw0", kOriginalCharacterMotionSlots,
        alicia_motion_aliases(), domain, alicia_pose_semantics, progress);
  }
  if (retain_weapon) {
    weapon_catalog_ = shared_appearance->weapon_catalog_;
  } else {
    weapon_catalog_ = std::allocate_shared<CharacterAnimations>(
        memory::Allocator<CharacterAnimations>(domain),
        source.weapon_support.get(), skeleton(source.weapon),
        source.weapon.name, 0, std::span<const animation::ClipAliasRequest>{},
        domain, alicia_pose_semantics, progress);
  }
  direction_reference_.bone = root_reference->bone;
  for (std::size_t action = 0; action < kMotionSlots.size(); ++action) {
    const auto& alias = body_catalog_->motions().alias(kMotionSlots.at(action));
    if (!alias.clip) {
      throw std::runtime_error("Player motion binding is absent");
    }
    motion_indices_.at(action) = *alias.clip;
    motion_parameters_.at(action) =
        body_catalog_->parameter_for_clip(*alias.clip);
  }
  const auto weapon_stand = weapon_catalog_->motions().find("stand_l");
  if (!weapon_stand) throw std::runtime_error("Weapon stand motion is absent");
  weapon_stand_ = *weapon_stand;
  constexpr std::array<std::string_view, 7> weapon_names{
      "defaultmotion", "stand_l",   "shoot_l",    "stand_reload",
      "shoot_reload",  "shoot_end", "shoot_after"};
  for (std::size_t slot = 0; slot < weapon_names.size(); ++slot) {
    weapon_slots_.at(slot) =
        weapon_catalog_->motions().find(weapon_names.at(slot));
  }
  shapes_ = std::allocate_shared<ShapeState>(
      memory::Allocator<ShapeState>(domain), source, domain);
  if (shared_appearance != nullptr) {
    shapes_->face.copy_state_from(shared_appearance->shapes_->face);
  }
  if (progress) progress();
  if (retain_playback) {
    playback_ = shared_appearance->playback_;
  } else {
    playback_ = std::allocate_shared<Playback>(
        memory::Allocator<Playback>(domain),
        Playback::Catalogs{*body_catalog_, *weapon_catalog_}, domain);
    playback_->face = &shapes_->face;
    playback_->body.request({.clip = motion_indices_[0],
                             .binding = kMotionSlots[0],
                             .parameter = motion_parameters_[0]});
  }
  if (retain_weapon) {
    weapon_playback_ = shared_appearance->weapon_playback_;
    weapon_rest_slot_ = shared_appearance->weapon_rest_slot_;
  } else {
    weapon_playback_ = std::allocate_shared<CharacterMotionPlayer>(
        memory::Allocator<CharacterMotionPlayer>(domain), *weapon_catalog_,
        playback_->weapon_sink, Playback::kWeaponCapacity, domain);
    weapon_rest_slot_ = weapon_raised ? 2U : 1U;
    const auto rest = weapon_slots_.at(weapon_rest_slot_);
    if (!rest) {
      throw std::runtime_error("Selected weapon rest motion is absent");
    }
    // The original weapon model has no body/upper motion-parameter receiver.
    const auto base_motion = weapon_slots_[0];
    if (change == PlayerAppearanceChange::live_weapon && base_motion) {
      weapon_playback_->request(
          {.clip = *base_motion, .binding = 0, .fresh = true});
    }
    constexpr double kWeaponRestBlendSeconds = .3;
    weapon_playback_->request({.clip = *rest,
                               .binding = weapon_rest_slot_,
                               .blend_seconds = kWeaponRestBlendSeconds,
                               .fresh = true});
  }
  live_weapon_candidate_ = change == PlayerAppearanceChange::live_weapon;
  palettes_.reserve(body.draws().size() +
                    source.weapon.definition.get().draws().size());
  prepare_palettes(false);
  prepare_palettes(true);
  updates_.reserve(palettes_.size());
  for (auto& palette : palettes_) {
    updates_.push_back({palette.model, palette.draw,
                        std::span(palette.matrices).first(palette.count)});
  }
  visibility_updates_.reserve(source.weapon.definition.get().draws().size());
  for (const auto& palette : palettes_) {
    if (palette.weapon) {
      visibility_updates_.push_back({palette.model, palette.draw, true});
    }
  }
  secondary_motion_.emplace(body, source.body.control_points, domain);
  support_arm_.emplace(
      SupportArmSource{.body = body,
                       .weapon = source.weapon.definition,
                       .weapon_control_points = source.weapon.control_points},
      domain);
  modifier_overrides_.reserve(skeleton(source.body).bones().size());
  body_pose_.bind_pose(model_world_);
  secondary_motion_->reset(body_pose_);
  evaluate();
}
// Costume replacement is an appearance transaction: reconstruct costume-local
// defaults while retaining the active action's independent clocks and rates.
PlayerActor::PlayerActor(PlayerActorSource source, const PlayerActor& current,
                         const std::shared_ptr<memory::Domain>& domain)
    : PlayerActor(source, current, domain, PlayerAppearanceChange::costume) {}
// A weapon review reset gets independent clocks, but retains the currently
// displayed body until the next consumed model update. Only the weapon draws
// change at paused publication, so attachments must use that same body pose.
PlayerActor::PlayerActor(PlayerActorSource source, const PlayerActor& current,
                         const std::shared_ptr<memory::Domain>& domain,
                         PlayerAppearanceChange change, bool weapon_raised)
    : PlayerActor(source, domain, &current, {}, change, weapon_raised) {
  model_world_ = current.model_world_;
  source_.placement = current.source_.placement;
  if (!secondary_motion_) {
    throw std::logic_error(
        "Appearance candidate has no prepared secondary state");
  }
  auto& secondary = *secondary_motion_;
  if (change != PlayerAppearanceChange::weapon) {
    current_ = current.current_;
    body_release_.copy_from(current.body_release_);
    consume_root_motion_ = current.consume_root_motion_;
    relative_root_rotation_ = current.relative_root_rotation_;
    pending_root_translation_ = current.pending_root_translation_;
    pending_root_seconds_ = current.pending_root_seconds_;
    if (change == PlayerAppearanceChange::costume) {
      evaluate_body();
      secondary.reset(body_pose_);
      evaluate();
      return;
    }
  }
  memory::Vector<animation::BoneWorldOverride> frozen{
      memory::Allocator<animation::BoneWorldOverride>(domain)};
  const auto bones = current.body_pose_.bone_transforms();
  frozen.reserve(bones.size());
  for (std::size_t index = 0; index < bones.size(); ++index) {
    frozen.push_back({index, bones[index]});
  }
  body_pose_.apply_world_overrides(frozen);
  secondary.reset(body_pose_);
  evaluate_attachment_palettes();
}
void PlayerActor::bind_audio(PlayerActorAudioSource source,
                             const std::shared_ptr<memory::Domain>& domain) {
  if (playback_->audio) throw std::logic_error("Player audio is already bound");
  auto prepared = std::allocate_shared<AudioState>(
      memory::Allocator<AudioState>(domain), *this, source, domain);
  playback_->audio = std::move(prepared);
  playback_->body_sink.audio_channel = &playback_->audio->body;
  playback_->upper_sink.audio_channel = &playback_->audio->upper;
  playback_->body_sink.audio_environment = playback_->audio.get();
  playback_->upper_sink.audio_environment = playback_->audio.get();
}
void PlayerActor::set_audio_listener(const audio::Listener& listener) {
  if (!math::all_finite(listener.position) ||
      !math::all_finite(listener.forward) || !math::all_finite(listener.up)) {
    throw std::invalid_argument("Invalid player audio listener");
  }
  if (playback_->audio) playback_->audio->listener = listener;
}
void PlayerActor::adopt_appearance(PlayerActor&& candidate) noexcept {
  // Keep old catalogs alive until their playback borrowers have retired.
  struct RetiredPlayback {
    std::shared_ptr<const CharacterAnimations> body, weapon;
    std::shared_ptr<Playback> playback;
    std::shared_ptr<CharacterMotionPlayer> weapon_motion;
  } retired{body_catalog_, weapon_catalog_, playback_, weapon_playback_};
  if (playback_ != candidate.playback_) {
    playback_->body.clear();
    playback_->upper.clear();
    playback_->additive.clear();
    weapon_playback_->clear();
    if (playback_->audio) (void)playback_->audio->owner.stop_voice();
    candidate.playback_->audio = std::move(playback_->audio);
    playback_->face = nullptr;
    playback_->body_sink.audio_channel = nullptr;
    playback_->upper_sink.audio_channel = nullptr;
    playback_->body_sink.audio_environment = nullptr;
    playback_->upper_sink.audio_environment = nullptr;
  }
  if (candidate.live_weapon_candidate_) {
    // Body appearance is identical: retain secondary velocities and facial
    // banks as well as its shared animation/event clocks.
    candidate.secondary_motion_ = std::move(secondary_motion_);
    candidate.secondary_delta_ = secondary_delta_;
    candidate.shapes_ = shapes_;
    weapon_playback_->clear();
    playback_->weapon_groups = std::numeric_limits<std::uint32_t>::max();
    candidate.live_weapon_candidate_ = false;
  }
  *this = std::move(candidate);
  playback_->face = &shapes_->face;
  if (playback_->audio) {
    playback_->body_sink.audio_channel = &playback_->audio->body;
    playback_->upper_sink.audio_channel = &playback_->audio->upper;
    playback_->body_sink.audio_environment = playback_->audio.get();
    playback_->upper_sink.audio_environment = playback_->audio.get();
  }
  retired.weapon_motion.reset();
  retired.playback.reset();
}
bool PlayerActor::includes_draw(bool weapon, std::size_t draw) const {
  const auto& model = (weapon ? source_.weapon : source_.body).definition.get();
  const auto group = model.draws().at(draw).group;
  // Player construction disables the auxiliary group. Further source action
  // masks are kept with game policy rather than inferred from material names.
  return weapon || group != 1;
}
bool PlayerActor::draw_visible(bool weapon, std::size_t draw) const {
  if (!weapon || live_weapon_candidate_) return true;
  const auto group = source_.weapon.definition.get().draws().at(draw).group;
  constexpr std::uint32_t kWordBits = 32;
  return group >= kWordBits || (playback_->weapon_groups & (1U << group)) != 0;
}
void PlayerActor::publish_visibility(render::ActorPublisher& output) {
  if (published_weapon_groups_ == playback_->weapon_groups) return;
  for (auto& update : visibility_updates_) {
    update.visible = draw_visible(true, update.draw);
  }
  output.update_visibility(visibility_updates_);
  published_weapon_groups_ = playback_->weapon_groups;
}
void PlayerActor::prepare_palettes(bool weapon) {
  const auto& resident = weapon ? source_.weapon : source_.body;
  const auto& model = resident.definition.get();
  for (std::size_t draw = 0; draw < model.draws().size(); ++draw) {
    if (!includes_draw(weapon, draw)) continue;
    Palette palette{.model = resident.handle, .draw = draw, .weapon = weapon};
    std::array<bool, kPaletteCapacity> assigned{};
    const auto& material =
        model.materials().at(model.draws().at(draw).material);
    for (const auto& reference : material.matrices) {
      constexpr std::uint32_t kJointSemantic = 0x4a494e54;
      if (reference.semantic != kJointSemantic ||
          reference.destination_matrix >= kPaletteCapacity ||
          reference.source_matrix >= skeleton(resident).bones().size()) {
        throw std::runtime_error("Player draw has an unresolved joint binding");
      }
      palette.bones.at(reference.destination_matrix) = reference.source_matrix;
      assigned.at(reference.destination_matrix) = true;
      palette.count =
          std::max(palette.count,
                   static_cast<std::size_t>(reference.destination_matrix) + 1);
    }
    if (palette.count == 0 ||
        !std::ranges::all_of(std::span(assigned).first(palette.count),
                             [](bool value) { return value; })) {
      throw std::runtime_error("Player draw contains an incomplete palette");
    }
    palettes_.push_back(palette);
  }
}
std::span<const render::WorldJointMatrix> PlayerActor::draw_palette(
    bool weapon, std::size_t draw) const {
  const auto found = std::ranges::find_if(palettes_, [&](const auto& palette) {
    return palette.weapon == weapon && palette.draw == draw;
  });
  if (found == palettes_.end()) {
    throw std::logic_error("Player draw is not selected");
  }
  return std::span(found->matrices).first(found->count);
}
const animation::PreparedClip& PlayerActor::clip(PlayerMotion motion) const {
  return body_catalog_->motions().clip(
      motion_indices_.at(static_cast<std::size_t>(motion)));
}
void PlayerActor::select_motion(PlayerMotion motion, float blend_seconds) {
  if (static_cast<std::size_t>(motion) >= motion_indices_.size() ||
      !std::isfinite(blend_seconds) || blend_seconds < 0) {
    throw std::invalid_argument("Invalid player motion transition");
  }
  if (motion == current_ && playback_->body.current() != nullptr &&
      playback_->body.current()->clip == &clip(motion)) {
    return;
  }
  const auto index = static_cast<std::size_t>(motion);
  playback_->body.request({.clip = motion_indices_.at(index),
                           .binding = kMotionSlots.at(index),
                           .parameter = motion_parameters_.at(index),
                           .blend_seconds = blend_seconds});
  current_ = motion;
}
animation::PlaybackAdmission PlayerActor::request_animation(
    CharacterMotionBank bank, const CharacterMotionRequest& request) {
  if (bank == CharacterMotionBank::additive && request.parameter) {
    throw std::invalid_argument("Additive pose owner has no event receiver");
  }
  return motion_bank(bank).request(request);
}
ActionPresentationAdmission PlayerActor::apply_action_plan(
    const ActionMotionPlan& plan) {
  if (plan.count > plan.requests.size() ||
      (plan.weapon_rest_slot && *plan.weapon_rest_slot != 1 &&
       *plan.weapon_rest_slot != 2)) {
    throw std::invalid_argument("Invalid action presentation plan");
  }
  struct Resolved {
    CharacterMotionBank bank{};
    std::optional<CharacterMotionRequest> request;
  };
  std::array<Resolved, 3> resolved{};
  std::size_t index = 0;
  constexpr std::uint32_t kMotionChannels = 32;
  // Resolve and validate all public request values before side effects. Missing
  // source slots are ordinary unavailable results, not invented substitutions.
  for (const auto& request : plan.motions()) {
    auto& item = resolved.at(index++);
    switch (request.bank) {
      case ActionMotionBank::body:
        item.bank = CharacterMotionBank::body;
        break;
      case ActionMotionBank::upper_body:
        item.bank = CharacterMotionBank::upper_body;
        break;
      case ActionMotionBank::additive:
        item.bank = CharacterMotionBank::additive;
        break;
      case ActionMotionBank::weapon:
        item.bank = CharacterMotionBank::weapon;
        break;
      default:
        throw std::invalid_argument("Unknown source action bank");
    }
    if (!std::isfinite(request.rate) || request.rate < 0 ||
        !std::isfinite(request.blend_seconds) || request.blend_seconds < 0 ||
        !std::isfinite(request.start_seconds) || request.start_seconds < 0 ||
        !std::isfinite(request.start_frames) || request.start_frames < 0 ||
        request.channel >= kMotionChannels ||
        (request.start != ActionMotionStart::seconds &&
         request.start != ActionMotionStart::frames &&
         request.start != ActionMotionStart::preserve_phase)) {
      throw std::invalid_argument("Invalid source action timing/channel");
    }
    const bool weapon = item.bank == CharacterMotionBank::weapon;
    const auto clip_index =
        weapon ? weapon_slots_.at(request.slot)
               : body_catalog_->motions().alias(request.slot).clip;
    if (!clip_index) continue;
    const auto& catalog = weapon ? *weapon_catalog_ : *body_catalog_;
    auto start =
        request.start == ActionMotionStart::frames
            ? request.start_frames /
                  catalog.motions().clip(*clip_index).frames_per_second()
            : request.start_seconds;
    if (request.start == ActionMotionStart::preserve_phase) {
      const auto* current = motion_bank(item.bank).current();
      const auto duration =
          catalog.motions().clip(*clip_index).duration_seconds();
      start =
          current != nullptr && current->clip->duration_seconds() > 0
              ? current->seconds / current->clip->duration_seconds() * duration
              : 0;
    }
    const auto parameter = item.bank == CharacterMotionBank::body ||
                                   item.bank == CharacterMotionBank::upper_body
                               ? body_catalog_->parameter_for_clip(*clip_index)
                               : std::nullopt;
    item.request = CharacterMotionRequest{
        .clip = *clip_index,
        .binding = request.slot,
        .parameter = parameter,
        .channel = request.channel,
        .start_seconds = start,
        .blend_seconds = request.blend_seconds,
        .rate = request.rate,
        .fresh = request.fresh,
        .preserve_phase = request.start == ActionMotionStart::preserve_phase};
  }
  if (plan.clear_upper) playback_->upper.clear();
  if (plan.weapon_rest_slot) {
    weapon_rest_slot_ = *plan.weapon_rest_slot;
  }
  ActionPresentationAdmission result;
  for (const auto& item : std::span(resolved).first(plan.count)) {
    if (!item.request) {
      ++result.unavailable;
      continue;
    }
    auto& bank = motion_bank(item.bank);
    bank.request(*item.request);
    bank.set_rate(item.request->rate);
    ++result.admitted;
  }
  return result;
}
void PlayerActor::clear_animation(CharacterMotionBank bank) {
  motion_bank(bank).clear();
}
// Review resets the retained controllers, not model residency or actor
// identity.
void PlayerActor::reset_animation_presentation() {
  playback_->body.clear();
  playback_->upper.clear();
  playback_->additive.clear();
  weapon_playback_->clear();
  shapes_->face.reset();
  playback_->aim_blend = {};
  playback_->aim_enabled = false;
  playback_->support_arm_permitted = false;
  weapon_rest_slot_ = 1;
  pending_root_translation_ = {};
  pending_root_seconds_ = 0;
  relative_root_rotation_ = animation::kIdentityMatrix;
  select_motion(PlayerMotion::stand, 0);
  weapon_playback_->request({.clip = weapon_stand_, .binding = 1});
}
bool PlayerActor::has_facial_shapes() const noexcept {
  return shapes_->face.available();
}
bool PlayerActor::preview_facial(std::size_t expression) {
  constexpr std::size_t kExpressionCount = 7;
  if (expression >= kExpressionCount || expression == 2 ||
      !shapes_->face.available()) {
    return false;
  }
  const auto result = shapes_->face.command(
      {static_cast<std::int32_t>(expression), expression < 2 ? 2 : 1, 1, 1},
      true);
  if (result == FacialAdmission::applied) {
    playback_->body.set_rate(0);
  }
  return result == FacialAdmission::applied;
}
CharacterMotionPlayer& PlayerActor::motion_bank(
    CharacterMotionBank bank) const {
  return bank == CharacterMotionBank::weapon ? *weapon_playback_
                                             : playback_->bank(bank);
}
const animation::ClipPlaybackState* PlayerActor::animation_state(
    CharacterMotionBank bank) const {
  return motion_bank(bank).current();
}
const CharacterPresentationStatus& PlayerActor::presentation_status() const {
  return playback_->status;
}
void PlayerActor::evaluate_body() {
  // Source ring capacities bound assembly; ordinary banks replace by channel
  // and additive controllers compose before the local reference modifier.
  std::array<animation::PoseLayer,
             Playback::kBodyCapacity + Playback::kUpperCapacity>
      ordinary{};
  std::size_t count = 0;
  for (const auto& layer : playback_->body.layers()) {
    ordinary.at(count++) = layer;
  }
  for (const auto& layer : playback_->upper.layers()) {
    ordinary.at(count++) = layer;
  }
  std::ranges::sort(std::span(ordinary).first(count),
                    [](const auto& left, const auto& right) {
                      return left.channel < right.channel;
                    });
  const std::array basis_overrides{direction_reference_};
  constexpr float kUpperReleaseDecayPerSecond = 3;
  body_pose_.evaluate_layers(
      {std::span<const animation::PoseLayer>(ordinary).first(count),
       playback_->additive.layers()},
      model_world_,
      consume_root_motion_ ? animation::RootPosePolicy::identity
                           : animation::RootPosePolicy::animated,
      basis_overrides,
      {&body_release_, secondary_delta_, kUpperReleaseDecayPerSecond},
      {std::span(aim_chain_).first(aim_chain_size_), playback_->aim_target,
       playback_->aim_blend.weight()});
}
void PlayerActor::evaluate() {
  shapes_->evaluate(secondary_delta_);
  evaluate_body();
  evaluate_weapon_pose();
  modifier_overrides_.clear();
  // Animated anchors come from this clean evaluation, never last frame's
  // secondary output. Ground policy belongs to Alicia, not the shared solver.
  if (secondary_motion_) {
    constexpr float kSecondaryGroundOffset = 0.03F;
    const auto overrides = secondary_motion_->advance(
        static_cast<float>(secondary_delta_),
        model_world_[3][1] + kSecondaryGroundOffset, body_pose_);
    modifier_overrides_.insert(modifier_overrides_.end(), overrides.begin(),
                               overrides.end());
    secondary_delta_ = 0;
  }
  // Arm and costume solvers both consume the clean pose. Their disjoint world
  // edits must publish together: separate calls would recompose prior edits
  // from sampled local transforms. Weapon targets depend on the right chain.
  const auto* upper = playback_->upper.current();
  const std::optional<std::uint32_t> upper_slot =
      upper ? std::optional(static_cast<std::uint32_t>(upper->binding))
            : std::nullopt;
  if (support_arm_ && playback_->support_arm_permitted &&
      permits_support_hand(upper_slot)) {
    if (const auto arm =
            support_arm_->evaluate({.body = body_pose_, .weapon = weapon_pose_},
                                   playback_->aim_blend.weight())) {
      modifier_overrides_.insert(modifier_overrides_.end(), arm->begin(),
                                 arm->end());
    }
  }
  body_pose_.apply_world_overrides(modifier_overrides_);
  fill_draw_palettes();
}
animation::Matrix PlayerActor::model_transform(bool weapon) const {
  if (!weapon) return model_world_;
  formats::SkeletonTransform local;
  local.rotation = {0, 1, 0, 0};
  local.scale = {1, 1, 1};
  const auto half_turn = animation::transform_matrix(local);
  return animation::multiply(
      half_turn,
      animation::multiply(half_turn,
                          body_pose_.bone_transforms()[attachment_bone_]));
}
void PlayerActor::evaluate_attachment_palettes() {
  // Paused appearance snapshots are already complete. Reattach the weapon
  // without applying a second partial constraint to that retained body pose.
  evaluate_weapon_pose();
  fill_draw_palettes();
}
void PlayerActor::evaluate_weapon_pose() {
  // Attach using the completed bone transform, not its inverse-bind skin form.
  // Wrapper-local activation and common render conversion are distinct.
  const auto weapon_world = model_transform(true);
  // The weapon model discards its exported root before applying the hand
  // attachment. Internal joints still animate normally through every blend.
  weapon_pose_.evaluate_layers({weapon_playback_->layers(), {}}, weapon_world,
                               animation::RootPosePolicy::identity);
}
void PlayerActor::fill_draw_palettes() {
  for (auto& palette : palettes_) {
    const auto matrices = palette.weapon ? weapon_pose_.skin_transforms()
                                         : body_pose_.skin_transforms();
    for (std::size_t joint = 0; joint < palette.count; ++joint) {
      const auto& source = matrices[palette.bones.at(joint)];
      auto& destination = palette.matrices.at(joint);
      for (std::size_t row = 0; row < destination.size(); ++row) {
        for (std::size_t column = 0; column < destination.size(); ++column) {
          destination.at(row).at(column) = source.at(column).at(row);
        }
      }
    }
  }
}
void PlayerActor::set_playback_rate(float rate) {
  if (!std::isfinite(rate) || rate < 0) {
    throw std::invalid_argument("Invalid player animation playback rate");
  }
  playback_->body.set_rate(rate);
}
void PlayerActor::set_aim_posture(const math::Vector3<float>& direction,
                                  bool enabled) {
  constexpr float kUnitTolerance = .001F;
  if (!math::all_finite(direction) ||
      std::abs(math::dot(direction, direction) - 1) > kUnitTolerance) {
    throw std::invalid_argument("Player aim target must be a unit direction");
  }
  if (enabled) playback_->aim_target = direction;
  playback_->aim_enabled = enabled;
}
void PlayerActor::set_support_arm_permission(bool permitted) noexcept {
  playback_->support_arm_permitted = permitted;
}
float PlayerActor::aim_posture_weight() const {
  return playback_->aim_blend.weight();
}
animation::Matrix PlayerActor::movement_reference() const {
  const auto* selected = playback_->body.current();
  return selected != nullptr
             ? root_basis(selected->clip->local_transform(
                   direction_reference_.bone, selected->seconds))
             : animation::kIdentityMatrix;
}
animation::Matrix PlayerActor::authored_facing_reference() const {
  const auto* selected = playback_->body.current();
  // The facing receiver reads the sampled local matrix before the separate
  // displacement consumer negates/rebuilds its forward direction.
  return selected != nullptr
             ? animation::transform_matrix(selected->clip->local_transform(
                   direction_reference_.bone, selected->seconds))
             : animation::kIdentityMatrix;
}
math::Vector3<float> PlayerActor::pending_root_displacement() const {
  return math::transform_direction<float>(pending_root_translation_,
                                          movement_reference());
}
std::array<float, 3> PlayerActor::advance_motion(double delta_seconds) {
  if (!std::isfinite(delta_seconds) || delta_seconds < 0 ||
      !std::isfinite(secondary_delta_ + delta_seconds)) {
    throw std::invalid_argument("Invalid player animation delta");
  }
  if (const auto* weapon = weapon_playback_->current()) {
    const auto rest = completed_weapon_motion(
        weapon->clip->repeating(), weapon->completed, weapon_rest_slot_);
    if (rest) {
      ActionMotionPlan plan;
      plan.requests[0] = *rest;
      plan.count = 1;
      (void)apply_action_plan(plan);
    }
  }
  // One source model tick owns the posture clock; publication only samples it.
  playback_->aim_blend.advance(static_cast<float>(delta_seconds),
                               playback_->aim_enabled);
  playback_->aim_enabled = false;
  playback_->body.advance(delta_seconds);
  playback_->upper.advance(delta_seconds);
  playback_->additive.advance(delta_seconds);
  weapon_playback_->advance(delta_seconds);
  (void)playback_->upper.retire_completed_owner();
  secondary_delta_ += delta_seconds;
  const auto* selected = playback_->body.current();
  relative_root_rotation_ = animation::kIdentityMatrix;
  pending_root_translation_ = {};
  pending_root_seconds_ = delta_seconds;
  if (selected == nullptr) return {};
  const auto delta = selected->clip->root_relative_transform(
      {selected->previous_seconds, selected->seconds,
       selected->unwrapped_seconds - selected->previous_unwrapped_seconds});
  relative_root_rotation_ = delta;
  relative_root_rotation_[3] = {0, 0, 0, 1};
  pending_root_translation_ = math::translation(delta);
  return pending_root_displacement();
}
bool PlayerActor::motion_finished() const {
  const auto* selected = playback_->body.current();
  return selected != nullptr && !selected->clip->repeating() &&
         selected->completed;
}
bool PlayerActor::motion_cancellable() const {
  return playback_->body.cancellable();
}
void PlayerActor::publish_pose(const ScriptedPlayerPose& placement,
                               float body_radius,
                               render::ActorPublisher& output) {
  const auto radians = placement.yaw_degrees * std::numbers::pi_v<float> / 180;
  formats::SkeletonTransform rotation;
  rotation.rotation = {0, std::sin(radians / 2), 0, std::cos(radians / 2)};
  rotation.scale = {1, 1, 1};
  publish_pose(placement, animation::transform_matrix(rotation), body_radius,
               output);
}
void PlayerActor::publish_pose(const ScriptedPlayerPose& placement,
                               const animation::Matrix& orientation,
                               float body_radius,
                               render::ActorPublisher& output) {
  complete_model_step(placement, orientation, body_radius);
  publish_completed_pose(output);
}
void PlayerActor::complete_model_step(const ScriptedPlayerPose& placement,
                                      const animation::Matrix& orientation,
                                      float body_radius) {
  if (!std::isfinite(body_radius) || body_radius <= 0) {
    throw std::invalid_argument("Invalid player model body radius");
  }
  if (!math::all_finite(orientation)) {
    throw std::invalid_argument("Invalid player model orientation");
  }
  if (orientation[3] != std::array<float, 4>{0, 0, 0, 1} ||
      orientation[0][3] != 0 || orientation[1][3] != 0 ||
      orientation[2][3] != 0) {
    throw std::invalid_argument("Player orientation contains translation");
  }
  // A live actor replaces the constructor height with its effective radius.
  auto physical = orientation;
  physical[3] = {placement.position[0], placement.position[1] - body_radius,
                 placement.position[2], 1};
  formats::SkeletonTransform basis;
  basis.rotation = {0, 1, 0, 0};
  basis.scale = {1, 1, 1};
  model_world_ =
      animation::multiply(animation::transform_matrix(basis), physical);
  source_.placement = placement;
  consume_root_motion_ = true;
  finish_model_step();
}
void PlayerActor::finish_model_step() {
  // Every source child update completes pose and event latches before the next
  // parent observes them. Retained renderer submission has no simulation role.
  evaluate();
  secondary_delta_ = 0;
  playback_->body.dispatch_events();
  playback_->upper.dispatch_events();
  playback_->additive.dispatch_events();
  weapon_playback_->dispatch_events();
  playback_->body.finish_frame();
  playback_->upper.finish_frame();
  playback_->additive.finish_frame();
  weapon_playback_->finish_frame();
}
void PlayerActor::publish_completed_pose(render::ActorPublisher& output) {
  output.update_shapes(shapes_->updates);
  output.update_poses(updates_);
  publish_visibility(output);
}
void PlayerActor::advance(double delta_seconds,
                          render::ActorPublisher& output) {
  (void)advance_motion(delta_seconds);
  finish_model_step();
  publish_completed_pose(output);
}
}  // namespace bw::game
