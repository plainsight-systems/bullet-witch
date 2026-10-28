// Compose the original actor-owned behavior, movement, weapon and presentation
// phases. The world remains the sole frame/physics owner. Native action
// requests use the last completed pose; presentation updates follow the next
// completed pose. Query adapters borrow immutable scene facts and narrow
// physics queries. Source-unclosed receivers reject explicitly instead of
// succeeding silently. Core E.4 validates admission/phase invariants; MEM.9
// prepares bounded actor storage once. Both implementation corpora read;
// first-combat-ai.md records source mappings and the remaining unclosed
// consumers.
#include "npc_combat_controller.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <numbers>
#include <stdexcept>

#include "bw/math/euler.hpp"
#include "contact_action_policy.hpp"
#include "game/actors/animation/combat_pose.hpp"
#include "game/actors/animation/weapon_motion_policy.hpp"
#include "game/combat/weapon_resources.hpp"
#include "game/content/original/motion_symbols.hpp"
#include "navigation_projection.hpp"

namespace bw::game {
namespace {
using Vector = math::Vector3<float>;
constexpr float kDegrees = 180.F;
constexpr std::uint32_t kCallbackWordBits = 32;
constexpr std::int32_t kCurrentEnemyAlternate = -12;
constexpr std::size_t kCandidateCapacity = 512, kAudioBanks = 3;
constexpr std::uint32_t kInitialCallbackMask = 0x1c0003, kSightFlags = 0x3200;
constexpr std::uint32_t kReadyGunFlags = 0x3200;
constexpr std::uint16_t kNoSound = 0xffff;
// Original unmodified BSS offset, confirmed by complete direct-reference
// census.
constexpr Vector kOriginalTargetOffset{};
void require_audio(CharacterAudioResult result) {
  if (result == CharacterAudioResult::capacity ||
      result == CharacterAudioResult::backend_failure) {
    throw std::runtime_error("NPC motion audio could not be admitted");
  }
}
math::Affine3<float> weapon_attachment(const NpcActor& actor,
                                       std::int32_t slot) {
  const auto* weapon = actor.weapon();
  if (!weapon) throw std::logic_error("NPC weapon is not admitted");
  return original_weapon_attachment_pose(
      weapon->prepared().attachments.get(), weapon->pose().skin_transforms(),
      math::affine_part(actor.model_transform(true)), slot);
}
}  // namespace
struct NpcCombatController::Impl final : BehaviorObservationSource,
                                         NpcActionQueries,
                                         NpcActionReceiver,
                                         PerceptionSight,
                                         NpcRouteQueries,
                                         NpcCollisionRouteQueries,
                                         NpcNavigationQueries {
  NpcActor& actor;
  NpcCombatMetadata metadata;
  NpcCombatServices services;
  NpcCombatResources resources;
  Vector initial, destination, heading, aim_direction;
  animation::Matrix aim_anchor;
  float ready_seconds{}, turn_rate{1};
  bool aim_completed{true};
  NpcSceneRouteFollower route;
  NpcSteeringHeading steering_heading;
  NpcNavigationController navigation;
  NpcAudio& audio;
  WeaponPresentation presentation;
  WeaponEmitter weapon;
  WeaponResources ammunition;
  MeleeMotionEvents melee;
  NpcBrain brain;
  NpcTargetRequests target_requests;
  std::array<PerceptionActor, kCandidateCapacity> candidates{};
  std::array<std::uint32_t, 4> callbacks{kInitialCallbackMask, 0, 0, 0};
  std::optional<world::WorldFrame> frame;
  std::optional<Vector> message_position;
  std::optional<CharacterVoiceRequest> reaction_voice;
  std::optional<std::string_view> pending_behavior;
  NpcNavigationObservation navigation_observation;
  NpcMovementSensor movement_sensor;
  std::uint64_t sequence{}, presented_sequence{}, navigation_job_sequence{};
  std::uint32_t posture{};
  std::int32_t message_id{};
  std::uint16_t sound_id{kNoSound};
  float playback_rate{1};
  enum class ContactAction : std::uint8_t { none, dodge, grapple };
  ContactAction contact_action{};
  std::uint64_t contact_target{};
  std::uint32_t contact_attack{};
  bool dodge_protected{};
  bool started{}, stopped{true};

  Impl(NpcActor& value, NpcCombatMetadata actor_metadata,
       NpcCombatServices dependencies, NpcCombatResources prepared,
       scene::LocalLightEmitter&& light,
       const std::shared_ptr<memory::Domain>& domain)
      : actor(value),
        metadata(actor_metadata),
        services(std::move(dependencies)),
        resources(prepared),
        initial(actor.state().placement.position),
        destination(initial),
        heading(actor.forward()),
        aim_direction(heading),
        aim_anchor(math::orthonormal_basis(heading, Vector{0, 1, 0})),
        route(services.navigation, services.navigation_group, *this,
              {services.random, *this, resources.body_radius,
               resources.project_approach},
              domain),
        navigation(*this, route),
        audio(resources.character_audio),
        presentation(services.clock, resources.weapon_presentation,
                     services.effects, std::move(light)),
        weapon(services.clock, resources.weapon, resources.weapon_sounds,
               services.random, services.projectiles, presentation,
               services.audio, actor.identity().value, services.scenery_layers,
               services.actor_layers),
        ammunition(resources.weapon.get().magazine_capacity,
                   resources.weapon.get().rate_per_second),
        melee(actor.identity().value, services.melee),
        brain(services.clock, services.behaviors, services.perceptions,
              services.random, services.scripts, services.actions,
              services.settings.get().perception(metadata.ai),
              static_cast<std::uint32_t>(actor.identity().value), *this, *this,
              *this, domain) {
    steering_heading.reset(actor.forward());
    if (actor.identity().value > std::numeric_limits<std::uint32_t>::max()) {
      throw std::invalid_argument("NPC Lua identity exceeds source width");
    }
  }
  Vector position() const { return actor.state().placement.position; }
  const NpcEncounterActor* find(std::uint64_t identity) const {
    for (const auto& entry : services.scene.get().actors()) {
      if (entry.perception.identity == identity) return &entry;
    }
    return nullptr;
  }
  const NpcEncounterActor* player() const {
    for (const auto& entry : services.scene.get().actors()) {
      if (entry.perception.player) return &entry;
    }
    return nullptr;
  }
  const NpcEncounterActor* enemy() const {
    const auto selected = brain.perception().target;
    return selected ? find(*selected) : nullptr;
  }
  const NpcEncounterActor* leader() const {
    for (const auto& entry : services.scene.get().actors()) {
      if (entry.group == metadata.ai.script.group_number && entry.leader &&
          !entry.perception.dead) {
        return &entry;
      }
    }
    return nullptr;
  }
  static Vector location(const NpcEncounterActor* entry) {
    return entry ? entry->perception.position : Vector{};
  }
  float distance_squared(const Vector& point) const {
    const auto delta = math::subtract(point, position());
    return math::dot(delta, delta);
  }
  float angle(const Vector& point) const {
    auto delta = math::subtract(point, position());
    delta[1] = 0;
    if (math::dot(delta, delta) < std::numeric_limits<float>::epsilon()) {
      return 0;
    }
    const auto forward = actor.forward();
    return std::atan2(math::cross(forward, delta)[1],
                      math::dot(forward, delta)) *
           kDegrees / std::numbers::pi_v<float>;
  }
  Vector near_path(std::int32_t selector) const {
    const auto points = patrol_points(selector);
    if (points.empty()) return initial;
    const auto closest = std::ranges::min_element(
        points, {},
        [this](const Vector& point) { return distance_squared(point); });
    return *closest;
  }
  double scalar(BehaviorObservation observation) const override {
    switch (observation) {
      case BehaviorObservation::target_model:
        return enemy() ? enemy()->model_category : -1;
      case BehaviorObservation::leader_distance_squared:
        return leader() ? distance_squared(location(leader())) : -1;
      case BehaviorObservation::enemy_distance_squared:
        return distance_squared(location(enemy()));
      case BehaviorObservation::enemy_can_look:
        return enemy() && can_see(location(enemy()));
      case BehaviorObservation::actor_life:
        return actor.state().life;
      case BehaviorObservation::actor_maximum_life:
        return actor.state().maximum_life;
      case BehaviorObservation::enemy_can_shoot_ready_gun:
        return enemy() && can_shoot(location(enemy()));
      case BehaviorObservation::enemy_actor_angle:
        return angle(location(enemy()));
      case BehaviorObservation::enemy_move_angle: {
        auto movement = math::subtract(destination, position());
        movement[1] = 0;
        movement = math::normalize_or(movement, actor.forward());
        auto target = math::subtract(location(enemy()), position());
        target[1] = 0;
        return std::atan2(math::cross(movement, target)[1],
                          math::dot(movement, target)) *
               kDegrees / std::numbers::pi_v<float>;
      }
      case BehaviorObservation::last_message_id:
        return message_id;
      case BehaviorObservation::target_is_player:
        return enemy() && enemy()->perception.player;
      case BehaviorObservation::leader_angle:
        return leader() ? angle(location(leader())) : 2 * kDegrees;
      case BehaviorObservation::last_message_distance_squared:
        if (!message_position) {
          throw std::logic_error("Message position has not been produced");
        }
        return distance_squared(*message_position);
      case BehaviorObservation::last_message_actor_angle:
        if (!message_position) {
          throw std::logic_error("Message position has not been produced");
        }
        return angle(*message_position);
      case BehaviorObservation::player_angle:
        return angle(location(player()));
      case BehaviorObservation::player_distance_squared:
        return distance_squared(location(player()));
      case BehaviorObservation::player_can_look:
        return player() && can_see(location(player()));
      case BehaviorObservation::last_sound_id:
        return sound_id;
      default:
        throw std::logic_error("Brain-owned observation escaped its owner");
    }
  }
  bool callback_on(std::string_view name) const override;
  BehaviorScriptSettings script_settings() const override {
    return metadata.ai.script;
  }
  bool previous_labels(std::int32_t, std::int32_t) const override {
    throw std::logic_error("Behavior history belongs to the brain");
  }
  double path_distance_squared(std::int32_t point) const override {
    return distance_squared(near_path(point));
  }
  double path_angle(std::int32_t point) const override {
    return angle(near_path(point));
  }
  NpcActionPose pose() const override {
    return {position(),
            actor.forward(),
            math::translation(weapon_attachment(
                actor,
                resources.weapon.get().effects.projectile_origin_attachment)),
            resources.weapon.get().speed,
            resources.body_radius,
            actor.action_reaction_active(),
            actor.motion_looping(),
            actor.motion_completed(),
            actor.dying(),
            static_cast<std::uint8_t>(brain.perception().knowledge),
            resources.body_height};
  }
  Vector initial_position() const override { return initial; }
  Vector reaction_facing(std::int32_t selector) const override {
    constexpr std::int32_t retained = -5, toward_message = -6,
                           away_message = -7, closest_group = -3;
    if (selector == retained) return actor.forward();
    Vector point{};
    if (selector == toward_message || selector == away_message) {
      if (!message_position) {
        throw std::logic_error(
            "Reaction direction requires a delivered message position");
      }
      point = *message_position;
    } else if (selector == closest_group) {
      const NpcEncounterActor* closest = nullptr;
      float nearest = 0;
      for (const auto& candidate : services.scene.get().actors()) {
        if (candidate.group != metadata.ai.script.group_number) continue;
        const auto distance = distance_squared(candidate.perception.position);
        if (!closest || distance < nearest) {
          closest = &candidate;
          nearest = distance;
        }
      }
      if (!closest) return actor.forward();
      point = closest->perception.position;
    } else {
      const auto selected = target(selector);
      point = selected ? selected->position : Vector{};
    }
    constexpr float minimum_squared = 0x1.0c6f7cp-20F;
    const auto normalized = [this](const Vector& value) {
      const auto squared = math::dot(value, value);
      return squared >= minimum_squared
                 ? math::divide(value, std::sqrt(squared))
                 : actor.forward();
    };
    auto direction = normalized(math::subtract(point, position()));
    if (selector == away_message) direction = math::scale(direction, -1.F);
    direction[1] = 0;
    return normalized(direction);
  }
  const NpcRouteQueries& navigation_queries() const override { return *this; }
  NpcNavigationObservation navigation_state() const override {
    return navigation_observation;
  }
  std::optional<NpcActionTarget> target(std::int32_t selector) const override {
    const NpcEncounterActor* selected = nullptr;
    if (selector == -1) {
      selected = player();
    } else if (selector == -2) {
      selected = enemy();
    } else if (selector == kCurrentEnemyAlternate) {
      const auto secondary = brain.secondary_perception().target;
      selected = secondary ? find(*secondary) : nullptr;
    } else {
      throw std::logic_error(
          "NPC actor/group target selector requires source receiver");
    }
    if (!selected) return std::nullopt;
    return NpcActionTarget{selected->perception.position,
                           selected->aim_position,
                           selected->perception.identity,
                           selected->perception.player, selected->forward};
  }
  std::optional<NpcActionTarget> movement_target(
      std::int32_t selector, bool alternate) const override {
    if (selector == -3) {
      const auto* selected = leader();
      if (!selected) return std::nullopt;
      return NpcActionTarget{selected->perception.position,
                             selected->aim_position,
                             selected->perception.identity,
                             selected->perception.player, selected->forward};
    }
    auto selected = target(selector);
    if (selected && alternate &&
        (selector == -2 || selector == kCurrentEnemyAlternate)) {
      selected->position =
          (selector == kCurrentEnemyAlternate ? brain.secondary_perception()
                                              : brain.perception())
              .last_seen_position;
    }
    return selected;
  }
  Vector ground_destination(const Vector& point, float up,
                            float down) const override {
    return npc_ground_destination(services.collision.get(), point, up, down);
  }
  std::span<const Vector> patrol_points(std::int32_t selector) const override {
    return services.scene.get().patrol_points(
        resources.path, resources.secondary_path, selector);
  }
  float movement_speed(float authored) const override {
    return metadata.movement_rate_scale == 0
               ? authored
               : authored * metadata.movement_rate_scale;
  }
  float movement_range(std::uint32_t category) const override;
  bool can_shoot(const Vector& point) const override {
    return clear(
        math::translation(weapon_attachment(
            actor,
            resources.weapon.get().effects.projectile_origin_attachment)),
        point, kReadyGunFlags);
  }
  bool can_see(const Vector& point) const override {
    return clear(eye_position(), point, kSightFlags);
  }
  NpcAimSettings aim_settings(std::int32_t mode,
                              bool player_target) const override {
    return services.settings.get().aim(mode, player_target);
  }
  bool begin_reaction(const NpcReactionCommand& command) override;
  bool dodge_running() const override {
    return contact_action == ContactAction::dodge;
  }
  bool melee_idle() const override {
    return contact_action != ContactAction::grapple;
  }
  bool voice_finished() const override { return reaction_voice_finished(); }
  bool reaction_voice_finished() const override {
    return !reaction_voice || !audio.voice_active(*reaction_voice);
  }
  std::optional<Vector> last_message_position() const override {
    return message_position;
  }
  bool callback_on(std::int32_t index) const {
    if (index < 0 || static_cast<std::size_t>(index) >=
                         callbacks.size() * kCallbackWordBits) {
      return false;
    }
    const auto value = static_cast<std::uint32_t>(index);
    return (callbacks.at(value / kCallbackWordBits) &
            (1U << (value % kCallbackWordBits))) != 0;
  }
  std::string_view return_behavior() const override {
    return brain.return_behavior();
  }
  bool clear(const Vector& from, const Vector& to,
             std::uint32_t flags) const override {
    if (from == to) return true;
    physics::Query query{};
    query.start = from;
    query.end = to;
    query.layers = services.scenery_layers;
    query.exclude_user_data = actor.identity().value;
    query.exclude_flags = flags;
    return !services.queries.ray(query);
  }
  bool traversable(const Vector& from, const Vector& to) const override {
    constexpr float kSensorHeight = .3F;
    return clear(from, to, resources.body_radius,
                 kSensorHeight - resources.body_radius);
  }
  bool clear(const Vector& from, const Vector& to, float radius,
             float height) const override {
    constexpr std::uint32_t kBodyQueryExclusions = 0x1000;
    std::array endpoints{from, to};
    for (auto& endpoint : endpoints) endpoint[1] += radius + height;
    return !services.collision.get().overlaps_capsule(
        endpoints[0], endpoints[1], radius, kBodyQueryExclusions);
  }
  std::optional<Vector> route_face(const Vector& from,
                                   const Vector& to) const override {
    constexpr float kHeight = .3F;
    constexpr std::uint32_t kExcluded = 0x1000;
    std::array endpoints{from, to};
    for (auto& endpoint : endpoints) endpoint[1] += kHeight;
    return services.collision.get().capsule_face(
        endpoints[0], endpoints[1], resources.body_radius, kExcluded);
  }
  CollisionRouteGround ground(
      const CollisionRouteGroundRequest& request) const override {
    auto elevated = request.point;
    elevated[1] += request.ground_up;
    constexpr float kDown = 20;
    auto projected =
        npc_ground_destination(services.collision, elevated, 0, kDown);
    const auto height = projected[1];
    projected[1] = std::clamp(height, request.point[1] - request.maximum_step,
                              request.point[1] + request.maximum_step);
    return {projected, height == projected[1]};
  }
  bool support(const Vector& center, const Vector& direction, float range,
               float maximum_step, float radius,
               float lateral_radius) const override {
    return npc_ground_support(
               services.collision,
               {center, direction, range, maximum_step, radius, lateral_radius})
               .status == NpcGroundSupportStatus::clear;
  }
  Vector initial_waypoint(const Vector& from, const Vector& to,
                          float radius) const override {
    constexpr float kHeight = .3F;
    constexpr std::uint32_t kExcluded = 0x1000;
    std::array endpoints{from, to};
    for (auto& endpoint : endpoints) endpoint[1] += kHeight;
    const auto hit = services.collision.get().clip_segment(
        {endpoints[0], endpoints[1], 0, kExcluded});
    if (!hit.clipped) return to;
    return math::subtract(
        hit.position,
        math::scale(math::normalize_or(math::subtract(to, from), Vector{}),
                    radius));
  }
  bool occluded(const Vector& from, const Vector& to) const override {
    return !clear(from, to, kSightFlags);
  }
  std::optional<Vector> ground(const Vector& point) const override {
    auto from = point, to = point;
    from[1] += resources.body_height;
    to[1] -= resources.body_height;
    const auto hit = services.collision.get().clip_segment({from, to, 0, 0});
    return hit.clipped ? std::optional<Vector>{hit.position} : std::nullopt;
  }
  std::span<const NpcCoverPoint> cover_points() const override {
    return services.scene.get().cover_points();
  }
  std::span<const NpcCoverObstacle> cover_obstacles() const override {
    return services.scene.get().cover_obstacles();
  }
  bool visible(const Vector& point) const override { return can_see(point); }
  Vector project(const Vector& origin, const Vector& point,
                 bool retreat) const override {
    const auto projected =
        retreat || resources.project_approach
            ? npc_approach_destination(services.collision.get(), origin, point)
            : point;
    return retreat ? npc_retreat_destination(services.collision.get(), origin,
                                             projected, resources.body_radius)
                   : projected;
  }

  Vector bone_position(std::int32_t slot) const {
    return actor.motion_attachment_position(slot);
  }
  Vector eye_position() const {
    constexpr std::int32_t eye_attachment = 0x36;
    return actor.attachment_position(eye_attachment);
  }
  void finish_contact() {
    // The incoming Blast state has already acquired the same source flags.
    // Ordinary completion releases only the departing Dodge protection.
    if (dodge_protected && !actor.melee_blast_active()) {
      constexpr std::uint32_t mask = 0xA;
      actor.set_hit_query_flags(mask, false);
    }
    contact_action = ContactAction::none;
    contact_target = 0;
    dodge_protected = false;
    actor.end_contact_action();
  }
  void dodge(const NpcDodgeCommand& command) {
    if (actor.dying() || actor.damage_reaction_active()) return;
    auto basis = actor.model_transform(false);
    basis[3] = {position()[0], position()[1], position()[2], 1};
    const auto inverse = math::inverse_affine(basis);
    if (!inverse) throw std::logic_error("NPC dodge basis is singular");
    auto offset = math::subtract(command.target, position());
    offset[1] = 0;
    const auto relative = math::transform_direction<float>(offset, *inverse);
    const auto angle = math::dot(relative, relative) < .000001F
                           ? 0.F
                           : std::atan2(relative[0], relative[2]);
    const auto order = npc_dodge_order(angle);
    constexpr float distance = 4.6F;
    constexpr std::array<Vector, 4> directions{
        Vector{0, 0, distance}, Vector{0, 0, -distance},
        Vector{-distance, 0, 0}, Vector{distance, 0, 0}};
    std::array<bool, 4> clear{};
    for (const auto index : order) {
      clear.at(index) = npc_dodge_clearance(
          services.collision, {position(), math::transform_point<float>(
                                               directions.at(index), basis)});
      if (clear.at(index)) break;
    }
    finish_contact();
    const auto slot = npc_dodge_slot(order, clear);
    (void)actor.begin_contact_action(
        {.slot = slot, .blend_seconds = 0, .fresh = true});
    contact_action = ContactAction::dodge;
    dodge_protected = services.random.get().unit() <= resources.dodge_threshold;
    constexpr std::uint32_t mask = 0xA;
    actor.set_hit_query_flags(mask, dodge_protected);
  }
  void grapple(const NpcMeleeCommand& command) {
    if (actor.dying() || actor.damage_reaction_active()) return;
    finish_contact();
    const auto motion = npc_grapple_motion(command.attack);
    if (!motion) return;
    (void)actor.begin_contact_action(*motion);
    contact_action = ContactAction::grapple;
    contact_target = command.target;
    contact_attack = command.attack;
  }
  void advance_contact(double seconds) {
    if (contact_action == ContactAction::none) return;
    const auto* motion = actor.body().current_motion();
    if (!actor.contact_action_active() || actor.dying() ||
        actor.damage_reaction_active() || (motion && motion->completed)) {
      finish_contact();
      return;
    }
    if (contact_action != ContactAction::grapple || !motion) return;
    const auto* target_actor = find(contact_target);
    if (!target_actor) return;
    constexpr std::uint32_t ordinary = 6, directed = 7, continuous = 11;
    constexpr double facing_frames = 10;
    const bool first_frames =
        motion->seconds * motion->clip->frames_per_second() < facing_frames;
    if ((contact_attack != ordinary && contact_attack != directed &&
         contact_attack != continuous) ||
        (contact_attack != continuous && !first_frames)) {
      return;
    }
    auto target_direction =
        math::subtract(target_actor->perception.position, position());
    target_direction[1] = 0;
    constexpr float turn_rate = 15.707963943481445F;
    const auto rate = contact_attack == continuous ? turn_rate / 2 : turn_rate;
    actor.face(npc_grapple_direction(actor.forward(), target_direction,
                                     static_cast<float>(seconds) * rate));
  }
  void request_aim(Vector direction) {
    const auto length = math::length(direction);
    if (!(length > 0) || !std::isfinite(length)) {
      throw std::invalid_argument("NPC aim command has no finite direction");
    }
    aim_direction = math::divide(direction, length);
    aim_completed = false;
  }
  void ready_weapon(float seconds) {
    if (seconds > 0) steering_heading.suppress_cohesion();
    if (!std::isfinite(seconds)) {
      throw std::invalid_argument("Invalid NPC ready duration");
    }
    const auto next = std::max(0.F, seconds);
    if ((ready_seconds > 0) != (next > 0)) {
      const auto* current = actor.body().current_motion();
      const auto slot =
          current ? static_cast<std::uint32_t>(current->binding) : 0;
      (void)actor.apply_action_plan(
          accepted_weapon_ready_motion({slot, false, next > 0}));
    }
    ready_seconds = next;
  }
  void submit(const NpcActionCommand& command) override;
  void fire(const NpcFireCommand&);
  bool navigate(const NpcNavigationRequest& request) override {
    if (!frame) {
      throw std::logic_error("NPC navigation requires admitted action frame");
    }
    const auto settings =
        services.settings.get().navigation(metadata.ai_mode, true);
    route.observe_sensor(movement_sensor.permitted());
    const auto result =
        navigation.advance(request, {position(), frame->delta_seconds(),
                                     settings.idle_threshold_adjustment,
                                     settings.retreat_arrival_squared});
    auto direction = result.direction;
    if (result.moving) {
      const auto steering_settings =
          services.settings.get().steering(metadata.ai_mode);
      const auto neighbors = services.commands.get().refresh_steering(
          {actor.identity().value, position(), steering_heading.heading(),
           steering_settings});
      direction = steering_heading.advance(
          math::subtract(result.destination, position()), neighbors,
          route.collision_route_active() ? 0
                                         : steering_settings.aggregate_weight,
          !result.retreating, frame->delta_seconds());
    }
    constexpr float kSteeringDistance = 2.009999990463257F;
    if (!result.moving) {
      navigation_observation = {direction, navigation_observation.side, false,
                                !result.blocked};
      return result.arrived;
    }
    const bool routed = request.receiver == NpcNavigationReceiver::routed;
    const auto offset = routed && route.collision_route_active()
                            ? math::subtract(result.destination, position())
                            : math::scale(direction, kSteeringDistance);
    const auto body_offset =
        routed ? Vector{} : math::scale(direction, resources.body_radius);
    constexpr float kMovementSquared = .01F, kAlignment = .9944999814033508F;
    auto horizontal = offset;
    horizontal[1] = 0;
    if (math::dot(horizontal, horizontal) <= kMovementSquared) {
      return result.arrived;
    }
    const auto alignment =
        routed ? math::dot(direction, math::normalize_or(horizontal, direction))
               : math::dot(direction, offset);
    const bool aim_only =
        alignment < kAlignment || (!routed && !movement_sensor.permitted());
    const auto from = math::add(position(), body_offset);
    const auto requested = math::add(from, offset);
    // Source aim-only updates retain the existing movement destination, and
    // refresh the sensor at the raised current point. It is not a stop command.
    if (aim_only) {
      if (!routed) {
        (void)movement_sensor.query(services.collision,
                                    services.movement_schedule,
                                    {from, from, resources.body_radius});
      }
      request_aim(direction);
      return false;
    }
    const auto probe =
        movement_sensor.query(services.collision, services.movement_schedule,
                              {from, requested, resources.body_radius});
    const bool permitted = probe.supported && probe.traversable;
    const bool force =
        routed ? route.collision_route_active() && route.pair_ready()
               : request.force_movement;
    const auto candidate = force || permitted
                               ? math::subtract(probe.destination, body_offset)
                               : position();
    constexpr float kDestinationChangeSquared = .0001F;
    if (math::squared_distance(candidate, destination) >=
        kDestinationChangeSquared) {
      destination = candidate;
      // Original MoveTo clears Runner's explicit stopped state. Destination
      // distance independently selects zero locomotion rate; it is not Stop.
      stopped = false;
      heading = math::normalize_or(
          Vector{aim_anchor[2][0], 0, aim_anchor[2][2]}, heading);
    }
    navigation_observation = {direction, navigation_observation.side, !stopped,
                              permitted};
    if (routed && !permitted && !force) {
      request_aim(direction);
    } else if (!routed && !force && !permitted) {
      navigation.reset();
      return true;
    }
    return false;
  }
  void reset_navigation() override {
    navigation.reset();
    navigation_observation.active = false;
  }
  void set_navigation_side(std::int32_t side) override {
    route.set_side(side);
    navigation_observation.side = side;
  }
  void consume_motion();
};
bool NpcCombatController::Impl::callback_on(std::string_view name) const {
  struct Callback {
    std::string_view name;
    std::int32_t bit;
  };
  constexpr std::array<Callback, 16> names{{{"all", -1},
                                            {"element_shot_fire", 25},
                                            {"element_shot_thunder", 27},
                                            {"element_shot_wind", 26},
                                            {"element_shot_light", 28},
                                            {"rose_spear", 29},
                                            {"sacrifise", 30},
                                            {"ravens_panic", 31},
                                            {"thunder", 33},
                                            {"tornado", 35},
                                            {"meteo", 37},
                                            {"ravens_panic_hit", 32},
                                            {"thunder_hit", 34},
                                            {"tornado_hit", 36},
                                            {"meteo_hit", 38},
                                            {"es0_hit", 24}}};
  const auto found = std::ranges::find(names, name, &Callback::name);
  if (found != names.end() && found->bit >= 0) return callback_on(found->bit);
  // The source unknown-name/all path scans only event callbacks, excluding
  // the five constructor-enabled controller callbacks.
  constexpr std::int32_t kCallbackCount = 45;
  for (std::int32_t index = 0; index < kCallbackCount; ++index) {
    constexpr std::int32_t kFirstSystem = 18, kLastSystem = 20;
    if (index == 0 || index == 1 ||
        (index >= kFirstSystem && index <= kLastSystem)) {
      continue;
    }
    if (callback_on(index)) return true;
  }
  return false;
}
float NpcCombatController::Impl::movement_range(std::uint32_t category) const {
  const auto& ranges = metadata.ai.script;
  switch (category) {
    case 0:
      return -1;
    case 1:
      return ranges.move_range;
    case 2:
      return ranges.attack_range_near;
    case 3:
      return ranges.attack_range_middle;
    case 4:
      return ranges.attack_range_far;
    default:
      throw std::invalid_argument(
          "Original movement range category is outside its string table");
  }
}
void NpcCombatController::Impl::fire(const NpcFireCommand& command) {
  if (!frame) {
    throw std::logic_error("NPC firing requires admitted action frame");
  }
  const auto admitted_frame = *frame;
  if (actor.dying() || actor.damage_reaction_active()) return;
  // Every source fire request refreshes readiness before the upper-owner gate.
  ready_weapon(1);
  if (actor.body().upper_active()) return;
  const auto receipt = ammunition.try_fire();
  const auto origin = weapon_attachment(
      actor, resources.weapon.get().effects.projectile_origin_attachment);
  const auto discharge = weapon_attachment(
      actor, resources.weapon.get().effects.discharge_attachment);
  auto geometry = weapon_forward_shot_geometry(origin);
  if (command.explicit_target) geometry.target = command.target;
  weapon.attempt(admitted_frame,
                 {receipt.shot_count, 0, receipt.first_shot_age_seconds,
                  receipt.interval_seconds},
                 {.shot = geometry,
                  .muzzle_world = origin,
                  .discharge_world = discharge,
                  .light_color = services.scene.get().light_color()});
  if (receipt.status == ShotStatus::accepted) actor.accepted_shot();
  if (receipt.status == ShotStatus::empty) {
    const auto* current = actor.body().current_motion();
    const auto slot =
        current ? static_cast<std::uint32_t>(current->binding) : 0;
    // Source reload sends optional model requests; missing authored slots do
    // not cancel the accepted magazine transaction.
    (void)actor.apply_action_plan(
        accepted_weapon_reload_motion({slot, 0, false}));
    ammunition.recharge();
    weapon.recharge(position());
  }
}
bool NpcCombatController::Impl::begin_reaction(
    const NpcReactionCommand& command) {
  const auto slot = content::original_motion_symbol(command.name);
  // Ordinary unknown symbols enter the no-motion phase; sequence admission
  // rejects them. These are distinct original receiver contracts.
  if (!slot) return command.admission == NpcReactionRequestKind::ordinary;
  const bool admitted =
      services.commands.get().admit_reaction(*slot, (command.flags & 1U) != 0);
  if (!admitted || !actor.has_reaction(*slot)) return false;
  return actor.react(*slot, command.blend_seconds, (command.flags & 4U) == 0);
}
void NpcCombatController::Impl::submit(const NpcActionCommand& command) {
  std::visit(
      [this](const auto& value) {
        using T = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<T, NpcPostureCommand>) {
          if (value.posture < 0) {
            throw std::invalid_argument("Invalid NPC posture");
          }
          posture = static_cast<std::uint32_t>(value.posture);
          playback_rate = value.speed_scale;
          stopped = value.stop;
        } else if constexpr (std::is_same_v<T, NpcMoveCommand>) {
          destination = math::add(position(), value.direction);
          playback_rate = value.speed_scale;
          stopped = false;
          if (value.face_direction) request_aim(value.direction);
        } else if constexpr (std::is_same_v<T, NpcFaceCommand> ||
                             std::is_same_v<T, NpcAimCommand>) {
          request_aim(value.direction);
        } else if constexpr (std::is_same_v<T, NpcReactionCommand>) {
          if (value.clear) {
            actor.clear_reaction();
          } else {
            throw std::logic_error("Named reactions require atomic admission");
          }

        } else if constexpr (std::is_same_v<T, NpcReadyWeaponCommand>) {
          ready_weapon(value.seconds);
        } else if constexpr (std::is_same_v<T, NpcDodgeCommand>) {
          dodge(value);
        } else if constexpr (std::is_same_v<T, NpcMeleeCommand>) {
          grapple(value);
        } else if constexpr (std::is_same_v<T, NpcFireCommand>) {
          fire(value);
        } else if constexpr (std::is_same_v<T, NpcBehaviorCommand>) {
          if (pending_behavior) {
            throw std::logic_error(
                "Multiple NPC behavior replacements in one dispatch");
          }
          pending_behavior = value.name;
        } else if constexpr (std::is_same_v<T, NpcTargetCommand>) {
          services.commands.get().share_target(
              {actor.identity().value, brain.perception(), position(),
               metadata.affiliation, metadata.ai.script.group_number,
               value.throw_mode, value.near_strength, value.far_strength,
               value.radius, value.duration});
        } else if constexpr (std::is_same_v<T, NpcBroadcastCommand>) {
          constexpr std::int32_t kDirectEnemy = -5;
          constexpr std::uint32_t kThreatEvent = 8, kOtherEvent = 44;
          if (value.mode != 0 && value.mode != 1) return;
          const auto direct = value.selector == kDirectEnemy
                                  ? brain.perception().target
                                  : std::nullopt;
          if (value.selector == kDirectEnemy && !direct) return;
          services.commands.get().broadcast_behavior(
              {actor.identity().value, direct, position(), metadata.affiliation,
               metadata.ai.script.group_number, value.selector, value.radius,
               value.mode == 0 ? kThreatEvent : kOtherEvent});
        } else if constexpr (std::is_same_v<T, NpcReactionVoiceCommand>) {
          const auto result = audio.voice(value.action);
          require_audio(result);
          reaction_voice = result == CharacterAudioResult::played
                               ? std::optional{audio.voice_request()}
                               : std::nullopt;
        } else if constexpr (std::is_same_v<T, NpcVoiceCommand>) {
          if (value.message < 0) {
            throw std::invalid_argument("Negative NPC voice action");
          }
          // Native voice update consumes message only. Face/mode remain
          // authored metadata; its installed state callback is a no-op.
          const auto result =
              audio.voice(static_cast<std::uint32_t>(value.message));
          require_audio(result);
          reaction_voice = result == CharacterAudioResult::played
                               ? std::optional{audio.voice_request()}
                               : std::nullopt;
        } else {
          throw std::logic_error(
              "NPC authored action receiver is not admitted");
        }
      },
      command);
}
void NpcCombatController::Impl::consume_motion() {
  for (std::size_t bank = 0; bank < kAudioBanks; ++bank) {
    for (const auto& event : actor.motion_events(bank)) {
      audio.consume(bank, event);
      if (std::holds_alternative<NpcPresentationReset>(event.command)) {
        actor.reset_facial();
        melee.reset();
        actor.reset_visibility(bank);
        continue;
      }
      const auto& command = std::get<MotionPresentationCommand>(event.command);
      std::visit(
          [&](const auto& value) {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, VisibilityMotionCommand>) {
              actor.visibility(bank, value);
            } else if constexpr (std::is_same_v<T, FacialMotionCommand>) {
              const auto result = actor.facial(value);
              if (result == FacialAdmission::invalid ||
                  result == FacialAdmission::exhausted) {
                throw std::runtime_error(
                    "NPC facial expression admission failed");
              }
            } else if constexpr (std::is_same_v<T, MeleeMotionCommand>) {
              melee.consume(value, event.phase, position(),
                            bone_position(value.joint));
            } else if constexpr (!std::is_same_v<T, SoundMotionCommand> &&
                                 !std::is_same_v<T, VoiceMotionCommand> &&
                                 !std::is_same_v<T,
                                                 CancellationMotionCommand>) {
              const auto* motion = actor.body().current_motion();
              throw std::logic_error(
                  "NPC motion receiver missing: variant=" +
                  std::to_string(command.index()) +
                  " channel=" + std::to_string(event.context.channel) +
                  " record=" + std::to_string(event.context.record) +
                  " frame=" + std::to_string(event.context.current_frame) +
                  " binding=" + std::to_string(motion ? motion->binding : 0));
            }
          },
          command);
    }
  }
  actor.consume_motion_events();
}
NpcCombatController::NpcCombatController(
    NpcActor& actor, NpcCombatMetadata metadata, NpcCombatServices services,
    NpcCombatResources resources, scene::LocalLightEmitter&& light,
    const std::shared_ptr<memory::Domain>& domain)
    : impl_(std::allocate_shared<Impl>(memory::Allocator<Impl>(domain), actor,
                                       metadata, std::move(services), resources,
                                       std::move(light), domain)) {}
NpcCombatController::~NpcCombatController() {
  try {
    stop();
  } catch (const std::exception& error) {
    std::fputs("NPC controller teardown failed: ", stderr);
    std::fputs(error.what(), stderr);
    std::fputs("\n", stderr);
  } catch (...) {
    std::fputs("NPC controller teardown failed with unknown exception\n",
               stderr);
  }
}
void NpcCombatController::receive_target(NpcTargetRequest request) {
  impl_->target_requests.submit(request);
}
void NpcCombatController::receive_sound(const ActorSoundStimulus& sound) {
  constexpr std::uint32_t kQuietSound = 20001, kLoudSound = 20002;
  constexpr std::size_t kSoundEntry = 16, kSpecialSoundEntry = 7;
  std::size_t entry = 0;
  switch (sound.message) {
    case 1:
    case 2:
    case kQuietSound:
    case kLoudSound:
      entry = kSoundEntry;
      break;
    case 3:
      entry = kSpecialSoundEntry;
      break;
    default:
      throw std::invalid_argument("Unknown actor-sound behavior message");
  }
  receive_attention(entry, sound.message, sound.position, sound.sound);
}
void NpcCombatController::receive_player_death(
    const math::Vector3<float>& position) {
  constexpr std::size_t kPlayerDeathEntry = 43;
  constexpr std::uint32_t kPlayerDeathMessage = 8;
  receive_attention(kPlayerDeathEntry, kPlayerDeathMessage, position,
                    std::nullopt);
}
void NpcCombatController::receive_attention(
    std::size_t entry, std::uint32_t message,
    const math::Vector3<float>& position, std::optional<std::uint16_t> sound) {
  auto& state = *impl_;
  if (!state.started || state.actor.dying()) return;
  // The admitted grounded soldier has no original half-death state gate.
  if (!state.brain.request_event(entry)) return;
  state.message_position = position;
  state.message_id = static_cast<std::int32_t>(message);
  if (state.brain.perception().knowledge == TargetKnowledge::unknown) {
    const auto observations = state.services.scene.get().actors();
    std::size_t count = 0;
    for (const auto& observation : observations) {
      if (observation.perception.identity != state.actor.identity().value) {
        state.candidates.at(count++) = observation.perception;
      }
    }
    state.brain.receive_target({}, TargetKnowledge::searching,
                               std::span(state.candidates).first(count));
    if (sound) state.sound_id = *sound;
  }
}
void NpcCombatController::start() {
  if (impl_->started) throw std::logic_error("NPC controller already started");
  impl_->brain.start(impl_->resources.initial_behavior);
  impl_->started = true;
}
void NpcCombatController::advance_navigation_jobs(
    const world::WorldFrame& frame) {
  auto& state = *impl_;
  if (!state.started || !frame.belongs_to(state.services.clock) ||
      frame.sequence() <= state.navigation_job_sequence) {
    throw std::invalid_argument("Invalid NPC route service frame");
  }
  state.navigation_job_sequence = frame.sequence();
  if (frame.admission().simulation) state.route.advance_jobs();
}
void NpcCombatController::advance(const world::WorldFrame& frame) {
  auto& state = *impl_;
  if (!state.started || !frame.belongs_to(state.services.clock) ||
      frame.sequence() <= state.sequence || state.frame) {
    throw std::invalid_argument("Invalid NPC controller frame or phase");
  }
  state.sequence = frame.sequence();
  if (!frame.admission().simulation) return;
  state.frame = frame;
  state.ammunition.advance(frame.delta_seconds());
  state.movement_sensor.advance();
  state.advance_contact(frame.delta_seconds());
  if (state.actor.dying()) return;
  const auto observations = state.services.scene.get().actors();
  if (observations.size() > state.candidates.size()) {
    throw std::length_error(
        "NPC observation population exceeds prepared capacity");
  }
  std::size_t count = 0;
  for (const auto& observation : observations) {
    if (observation.perception.identity != state.actor.identity().value) {
      state.candidates.at(count++) = observation.perception;
    }
  }
  if (const auto request = state.target_requests.consume()) {
    if (request->target != state.actor.identity().value) {
      state.brain.receive_target(request->target, request->knowledge,
                                 std::span(state.candidates).first(count));
    }
  }
  constexpr std::int32_t eye_attachment = 0x36;
  const auto eye = state.actor.attachment_transform(eye_attachment);
  (void)state.brain.advance(
      frame,
      {eye[3], eye[2], kOriginalTargetOffset, state.metadata.affiliation,
       std::span(state.candidates).first(count)},
      state);
  if (state.brain.perception().knowledge != TargetKnowledge::searching) {
    state.sound_id = kNoSound;
  }
  if (state.pending_behavior) {
    const auto name = *state.pending_behavior;
    state.pending_behavior.reset();
    state.brain.replace(name);
  }
  if (!state.aim_completed) {
    const auto step = advance_shooter_anchor(
        state.aim_anchor, state.aim_direction, frame.delta_seconds());
    state.aim_anchor = step.anchor;
    state.turn_rate = step.turn_rate;
    state.aim_completed = step.completed;
  }
  if (state.ready_seconds > 0 && !state.actor.body().upper_active()) {
    state.ready_weapon(state.ready_seconds - frame.delta_seconds());
  }
  if (state.actor.action_reaction_active()) {
    state.actor.reaction_facing(state.heading, state.aim_anchor,
                                state.ready_seconds > 0, frame.delta_seconds());
    state.heading = state.actor.forward();
    state.actor.aim(state.aim_anchor, state.ready_seconds > 0);
    return;
  }
  const auto selection = select_npc_locomotion(
      {state.position(),
       state.destination,
       state.heading,
       {state.aim_anchor[2][0], state.aim_anchor[2][1], state.aim_anchor[2][2]},
       state.posture,
       state.playback_rate,
       0,
       state.turn_rate,
       frame.delta_seconds(),
       state.stopped,
       state.ready_seconds > 0});
  state.heading = selection.retained_heading;
  if (selection.facing_request) state.request_aim(*selection.facing_request);
  state.actor.locomotion(selection, state.aim_anchor, state.ready_seconds > 0,
                         frame.delta_seconds());
  state.actor.aim(state.aim_anchor, state.ready_seconds > 0);
}
void NpcCombatController::after_pose(const world::WorldFrame& frame) {
  auto& state = *impl_;
  if (!frame.belongs_to(state.services.clock) ||
      frame.sequence() != state.sequence ||
      frame.sequence() <= state.presented_sequence) {
    throw std::invalid_argument("Invalid NPC completed-pose frame");
  }
  state.presented_sequence = frame.sequence();
  if (!frame.admission().simulation) return;
  if (!state.frame) {
    throw std::logic_error("NPC pose has no admitted action phase");
  }
  state.consume_motion();
  const auto* current = state.actor.body().current_motion();
  const bool widening = current && weapon_spread_widens(current->binding);
  state.weapon.advance(
      frame, widening,
      weapon_attachment(
          state.actor,
          state.resources.weapon.get().effects.projectile_origin_attachment));
  state.frame.reset();
}
void NpcCombatController::stop() {
  auto& state = *impl_;
  if (state.started) state.brain.stop();
  state.melee.reset();
  state.finish_contact();
  state.started = false;
  state.frame.reset();
  state.audio.stop();
  state.weapon.retire();
  state.navigation.reset();
}
}  // namespace bw::game
