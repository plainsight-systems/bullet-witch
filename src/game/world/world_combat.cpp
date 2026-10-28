// Compose existing source algorithms without another clock, collision loop or
// health store. Core I.1/MEM.9 reread for implementation: explicit phase calls,
// immutable definitions and admission-only bounded allocation. Core ES.103
// (#res-overflow) rejects the signed-minimum patrol selector before negation;
// CDSA.32 shares prepared navigation and contiguous copied actor observations.
#include "world_combat.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

#include "game/actors/npc/npc_combat_controller.hpp"
#include "game/actors/npc/reaction_admission.hpp"
#include "game/combat/melee_damage.hpp"
#include "game/combat/shot_geometry.hpp"
#include "game/mission/loading/mission_loading.hpp"
#include "mission_simulation.hpp"
#include "presentation/stage_presentation.hpp"
#include "world_physics.hpp"
#include "world_population.hpp"
namespace bw::game {
namespace {
// Population identities occupy their original 32-bit domain. The player is a
// distinct stable world receiver, never a pointer or a population slot alias.
constexpr std::uint64_t kPlayerIdentity = std::uint64_t{1} << 32;
constexpr std::uint16_t kSoldierHitLayers = 1U << 6;
constexpr std::size_t kProjectileCapacity = 4096;
constexpr std::size_t kActorCapacity = 50;
constexpr std::uint8_t kPlayerHitLayer = 5;
constexpr std::int32_t kSelectedSoldier = 100;
}  // namespace
namespace {
constexpr std::array<std::string_view, 6> kSoldierBehaviors{
    "no_ea_m_000",     "sub_ea_magic_no", "sub_ea_pursue_m",
    "sub_ea_escape_m", "sub_ea_lv01_no",  "sub_ea_lv01_follow"};
bool selected_encounter(const content::PlacementArchetype& actor) {
  return actor.id == kSelectedSoldier && actor.model == "ea0" &&
         actor.weapon == "wg1" && actor.behavior == kSoldierBehaviors.front();
}
std::span<const std::uint8_t> stage_customization(
    const MissionLoading& loading) {
  if (!loading.stage()) throw std::logic_error("Combat stage is absent");
  return loading.stage()->customization_bytes();
}
const content::MissionPlacementDefinition& placement_definition(
    const MissionLoading& loading) {
  const auto* definition = loading.population_definition();
  if (!definition) {
    throw std::logic_error("Combat requires authored placements");
  }
  return *definition;
}
}  // namespace
memory::Vector<CharacterAudioProfile> combat_audio_profiles(
    const MissionLoading& loading,
    const std::shared_ptr<memory::Domain>& domain) {
  memory::Vector<CharacterAudioProfile> result{
      memory::Allocator<CharacterAudioProfile>(domain)};
  const auto& definition = placement_definition(loading);
  result.reserve(definition.archetypes().size());
  for (const auto& actor : definition.archetypes()) {
    if (actor.model != "ea0") continue;
    const auto profile =
        prepare_npc_combat_metadata(definition, actor, 0).audio;
    if (std::ranges::find(result, profile) == result.end()) {
      result.push_back(profile);
    }
  }
  return result;
}
// Mission-scoped AI services and immutable observations. Controllers retire
// before actors; actor admission prepares their retained state transactionally.
struct WorldCombat::Impl final : PopulationCombatLifecycle,
                                 NpcEncounterScene,
                                 NpcEncounterCommands,
                                 MeleeEmission {
  struct MeleeReceipt {
    std::uint64_t identity{};
    std::uint32_t serial{};
  };
  struct Prepared {
    NpcCombatMetadata metadata;
    NpcInitialBodySettings body;
    const PreparedCharacterAudio* audio{};
  };
  struct Entry {
    PopulationIdentity identity;
    NpcActor*
        actor{};  // Population retires this borrow after controller/audio.
    std::shared_ptr<NpcAudio> audio;
    std::shared_ptr<NpcCombatController> controller;
    bool started{};
  };
  Impl(const world::WorldClock& clock, const MissionLoading& loading,
       MissionSimulation& simulation, WorldPopulation& population,
       WorldPhysics& physics, StagePresentation& presentation,
       CombatWorld& projectiles, CombatEffects& effects, SourceRandom& random,
       audio::CueOutput& audio, CharacterAudioLookup character_audio,
       PlayerDamageResources player, const scene::StaticCollision& collision,
       CharacterVoicePool& voices, ActorSoundEvents& sound_events,
       const std::shared_ptr<memory::Domain>& domain)
      : clock(clock),
        loading(loading),
        simulation(simulation),
        population(population),
        physics(physics),
        presentation(presentation),
        projectiles(projectiles.emitter()),
        effects(effects.emitter()),
        random(random),
        audio(audio),
        voices(voices),
        sound_events(sound_events),
        player(player),
        collision(collision),
        domain(domain),
        definition(placement_definition(loading)),
        navigation(loading.package_input(MissionLoadKind::paths).bytes, domain),
        settings(NpcAiSettings::parse(formats::CustomizationDefinition::parse(
            stage_customization(loading), domain))),
        prepared(memory::Allocator<Prepared>(domain)),
        cover(memory::Allocator<NpcCoverPoint>(domain)),
        obstacles(memory::Allocator<NpcCoverObstacle>(domain)) {
    constexpr std::size_t kLuaHeapBytes = 1U << 20;
    std::array<const formats::BehaviorTreeDefinition*, kSoldierBehaviors.size()>
        trees{};
    for (std::size_t i = 0; i < definitions.size(); ++i) {
      const auto& tree =
          loading.combat_behaviors().definition(kSoldierBehaviors.at(i));
      definitions.at(i) = {kSoldierBehaviors.at(i), &tree, kLuaHeapBytes};
      trees.at(i) = &tree;
    }
    scripts.emplace(definitions, domain);
    actions.emplace(trees, domain);
    if (!loading.stage()) throw std::logic_error("Combat stage is absent");
    const auto customization = formats::CustomizationDefinition::parse(
        stage_customization(loading), domain);
    prepared.reserve(definition.archetypes().size());
    for (const auto& archetype : definition.archetypes()) {
      const auto metadata =
          prepare_npc_combat_metadata(definition, archetype, 0);
      prepared.push_back(
          {metadata,
           NpcInitialBodySettings::parse(customization, archetype.model),
           archetype.model == "ea0" ? &character_audio.prepared(metadata.audio)
                                    : nullptr});
    }
    cover.reserve(loading.cover_points().size());
    for (const auto& point : loading.cover_points()) {
      const auto& value = point.values;
      cover.push_back({{value[0], value[1], value[2]},
                       {value[3], value[4],
                        value[CoverPointDefinition::kCoordinateCount - 1]}});
    }
    // Background objects currently retain their admitted physical body poses.
    // Cover observes body zero's actual placement and the authored root sphere;
    // it does not substitute a mesh AABB or lighting control-point position.
    if (loading.background()) {
      const auto& background = *loading.background();
      const auto residents = loading.resident_dependencies();
      obstacles.reserve(background.placements().size());
      for (const auto& placement : background.placements()) {
        const auto name = background.names().at(placement.model);
        const auto found =
            std::ranges::find(residents, name, &ResidentModelView::name);
        if (found == residents.end() || placement.matrix_count == 0) {
          throw std::runtime_error("Cover background model is not resident");
        }
        obstacles.push_back({math::translation(background.matrices().at(
                                 placement.first_matrix)),
                             found->definition.get().bounding_radius()});
      }
    }
    // Registration is the final operation; exceptions above leave no callback.
    population.bind_combat_lifecycle(*this);
  }
  ~Impl() override { population.unbind_combat_lifecycle(*this); }
  Impl(const Impl&) = delete;
  Impl& operator=(const Impl&) = delete;
  Impl(Impl&&) = delete;
  Impl& operator=(Impl&&) = delete;
  void admit(NpcActor& actor,
             const content::PlacementArchetype& archetype) override {
    if (archetype.model != "ea0") return;
    const auto slot = std::ranges::find_if(
        entries, [](const Entry& entry) { return !entry.audio; });
    if (slot == entries.end()) {
      throw std::length_error("Combat controller capacity exceeded");
    }
    const auto index = actor.state().placement.archetype;
    const auto& source = prepared.at(index);
    actor.bind_locomotion(collision, source.body.radius,
                          source.body.standing_height);
    auto actor_audio = std::allocate_shared<NpcAudio>(
        memory::Allocator<NpcAudio>(domain), actor, *source.audio, audio,
        collision, random, source.body.radius, voices, sound_events,
        source.metadata.affiliation, domain);
    actor_audio->set_player_position(simulation.player_state()->pose.position);
    if (!selected_encounter(archetype)) {
      *slot = {actor.identity(), &actor, std::move(actor_audio), {}, false};
      return;
    }
    auto metadata = source.metadata;
    metadata.ai.script.group_number =
        definition.groups()[actor.state().placement.group_row].id;
    const auto& combat = loading.combat_definitions();
    if (!scripts || !actions) {
      throw std::logic_error("Combat AI libraries are absent");
    }
    const auto& table = definition.original_table();
    const auto character_section = table.find("chara");
    const auto group_section = table.find("group");
    if (!character_section || !group_section) {
      throw std::logic_error("Combat placement sections are absent");
    }
    constexpr std::size_t kPrimaryPathColumn = 5, kSecondaryPathColumn = 7;
    const auto primary_path =
        table
            .text(
                {*character_section, archetype.source_row, kPrimaryPathColumn})
            .value_or("NULL-PATH");
    const auto secondary_path =
        table
            .text({*group_section,
                   definition.groups()[actor.state().placement.group_row]
                       .source_row,
                   kSecondaryPathColumn})
            .value_or("NULL-PATH");
    auto controller = std::allocate_shared<NpcCombatController>(
        memory::Allocator<NpcCombatController>(domain), actor, metadata,
        NpcCombatServices{clock,
                          behaviors,
                          perceptions,
                          movement_schedule,
                          random,
                          *scripts,
                          *actions,
                          settings,
                          *this,
                          *this,
                          collision,
                          physics.queries(),
                          navigation,
                          search_group(primary_path, secondary_path),
                          audio,
                          projectiles,
                          *this,
                          effects,
                          WorldPhysics::scenery_layers(),
                          std::uint16_t{1U << kPlayerHitLayer}},
        NpcCombatResources{
            combat.soldier, combat.soldier_sound, *actor_audio,
            combat.soldier_presentation, archetype.behavior, primary_path,
            secondary_path, source.body.radius, source.body.standing_height,
            source.body.dodge_threshold, (navigation_admissions++ % 2) == 0},
        presentation.acquire_local_light(), domain);
    *slot = {actor.identity(), &actor, std::move(actor_audio),
             std::move(controller), false};
  }
  void retire(PopulationIdentity identity) noexcept override {
    steering.retire(identity.value);
    const auto slot = std::ranges::find(entries, identity, &Entry::identity);
    if (slot != entries.end()) {
      slot->controller.reset();
      slot->audio.reset();
      *slot = {};
    }
    const auto receipt = std::ranges::find(melee_receipts, identity.value,
                                           &MeleeReceipt::identity);
    if (receipt != melee_receipts.end()) *receipt = {};
  }
  NpcSteeringSample refresh_steering(
      const NpcSteeringRequest& request) override {
    return steering.refresh(request);
  }
  bool admit_reaction(std::uint32_t motion_slot, bool bypass) override {
    return reactions.admit(motion_slot, bypass);
  }
  void refresh_observations() {
    const auto count = population.observe_actors(population_snapshot);
    const auto resources = player.snapshot();
    const auto* movement = simulation.player_state();
    if (!movement) {
      throw std::logic_error("Combat player observation is unavailable");
    }
    const auto& transform = simulation.actor()->model_transform(false);
    snapshots[0] = {{kPlayerIdentity,
                     {transform[3][0], transform[3][1], transform[3][2]},
                     1,
                     resources.health,
                     true,
                     simulation.player_dead()},
                    {transform[2][0], transform[2][1], transform[2][2]},
                    movement->velocity,
                    simulation.actor()->aim_position(),
                    resources.maximum_health,
                    0,
                    npc_target_model(1),
                    false};
    for (std::size_t i = 0; i < count; ++i) {
      const auto& actor = population_snapshot.at(i);
      const auto& metadata =
          prepared.at(actor.state.placement.archetype).metadata;
      snapshots.at(i + 1) = {
          {actor.identity.value, actor.state.placement.position,
           metadata.affiliation, actor.state.life, false,
           actor.state.life <= 0},
          actor.forward,
          actor.velocity,
          actor.aim_position,
          actor.state.maximum_life,
          definition.groups()[actor.state.placement.group_row].id,
          npc_target_model(metadata.affiliation),
          metadata.leader};
    }
    snapshot_count = count + 1;
    population_snapshot_count = count;
  }
  void begin_frame(const world::WorldFrame& frame) {
    reactions.advance(frame.delta_seconds());
    behaviors.advance();
    movement_schedule.advance();
    perceptions.advance();
    steering.advance(collision);
    for (auto& entry : entries) {
      if (entry.controller && entry.started) {
        entry.controller->advance_navigation_jobs(frame);
      }
    }
    refresh_observations();
    // Drain before any actor's current-frame animation can emit another sound.
    // Original broadcast order is active-list order, at most thirty living
    // recipients; sound attention uses descriptor bit two and no LOS test.
    constexpr std::size_t kMaximumDeliveries = 30;
    for (const auto& sound : sound_stimuli.drain()) {
      if (sound.priority < 0) continue;
      std::size_t delivered = 0;
      for (std::size_t i = 0; i < population_snapshot_count; ++i) {
        const auto& receiver = population_snapshot.at(i);
        const auto& metadata =
            prepared.at(receiver.state.placement.archetype).metadata;
        const auto delta =
            math::subtract(receiver.state.placement.position, sound.position);
        if (receiver.state.life <= 0 || (metadata.affiliation & 2U) == 0 ||
            math::dot(delta, delta) >= sound.radius * sound.radius) {
          continue;
        }
        const auto entry =
            std::ranges::find_if(entries, [&](const Entry& value) {
              return value.identity == receiver.identity;
            });
        if (entry != entries.end() && entry->controller && entry->started) {
          entry->controller->receive_sound(sound);
        }
        if (++delivered == kMaximumDeliveries) break;
      }
    }
  }
  void player_death() {
    const auto* player_state = simulation.player_state();
    if (!player_state) {
      throw std::logic_error("Player death needs physical placement");
    }
    const auto position = player_state->pose.position;
    constexpr float kRadius = 8;
    constexpr std::size_t kMaximumDeliveries = 30;
    std::array<PopulationActorObservation, kActorCapacity> current{};
    const auto count = population.observe_actors(current);
    std::size_t delivered = 0;
    for (const auto& receiver : std::span(current).first(count)) {
      const auto delta =
          math::subtract(receiver.state.placement.position, position);
      if (receiver.state.life <= 0 ||
          math::dot(delta, delta) >= kRadius * kRadius) {
        continue;
      }
      const auto entry = std::ranges::find_if(entries, [&](const Entry& value) {
        return value.identity == receiver.identity;
      });
      if (entry != entries.end() && entry->controller && entry->started) {
        entry->controller->receive_player_death(position);
      }
      if (++delivered == kMaximumDeliveries) break;
    }
  }
  void advance_actors(const world::WorldFrame& frame) {
    refresh_observations();
    for (auto& entry : entries) {
      if (entry.audio) {
        entry.audio->set_player_position(snapshots[0].perception.position);
      }
      if (!entry.controller) continue;
      if (!entry.started) {
        entry.controller->start();
        entry.started = true;
      }
      entry.controller->advance(frame);
    }
  }
  void after_pose(const world::WorldFrame& frame) {
    for (auto& entry : entries) {
      if (!entry.audio) continue;
      if (entry.controller && entry.started) {
        entry.controller->after_pose(frame);
      } else {
        constexpr std::size_t kMotionBanks = 3;
        for (std::size_t bank = 0; bank < kMotionBanks; ++bank) {
          for (const auto& event : entry.actor->motion_events(bank)) {
            entry.audio->consume(bank, event);
          }
        }
        entry.actor->consume_motion_events();
      }
    }
  }
  std::span<const NpcEncounterActor> actors() const override {
    return std::span(snapshots).first(snapshot_count);
  }
  std::span<const NpcCoverPoint> cover_points() const override { return cover; }
  std::span<const NpcCoverObstacle> cover_obstacles() const override {
    return obstacles;
  }
  // Source admission keeps character and placement-group paths independent.
  // Blank means the authored default filename; absent means NULL-PATH.
  std::optional<std::size_t> path_group(std::string_view path) const {
    return navigation.find_group(path.empty() ? "notitle_0001.pat" : path);
  }
  std::size_t search_group(std::string_view primary,
                           std::string_view secondary) const {
    for (const auto path : {primary, secondary}) {
      const auto group = path_group(path);
      if (group && navigation.groups()[*group].kind ==
                       formats::NavigationGroup::Kind::search) {
        return *group;
      }
    }
    return navigation.groups().size();
  }
  std::span<const math::Vector3<float>> patrol_points(
      std::string_view primary, std::string_view secondary,
      std::int32_t selector) const override {
    if (selector == std::numeric_limits<std::int32_t>::min()) {
      throw std::invalid_argument("NPC patrol selector overflows");
    }
    const auto group = path_group(selector < 0 ? secondary : primary);
    if (!group) return {};
    return navigation.patrol(*group, selector < 0 ? -selector : selector);
  }
  // Original active-list order governs delivery. Commands copy into receiving
  // AI queues; they cannot synchronously reenter another actor's update.
  void share_target(const NpcTargetTransmission& message) override {
    constexpr std::size_t kMaximumDeliveries = 30;
    std::size_t delivered = 0;
    for (std::size_t index = 0; index < population_snapshot_count; ++index) {
      const auto& source = population_snapshot.at(index);
      const auto& receiver = snapshots.at(index + 1);
      if (receiver.perception.identity == message.sender) continue;
      const auto& archetype =
          definition.archetypes()[source.state.placement.archetype];
      const bool same_affiliation =
          ((receiver.perception.affiliation ^ message.affiliation) & 2U) == 0;
      bool selected = false;
      constexpr std::int32_t kEveryone = -100, kOtherAffiliation = -5;
      switch (message.recipients) {
        case kEveryone:
        case -4:
          selected = true;
          break;
        case -3:
          selected = receiver.group == message.group && receiver.leader;
          break;
        case -2:
          selected = receiver.group == message.group;
          break;
        case -1:
          selected = same_affiliation;
          break;
        case kOtherAffiliation:
          selected = !same_affiliation;
          break;
        default:
          selected = message.recipients == archetype.id;
          break;
      }
      if (!selected) continue;
      const auto offset =
          math::subtract(receiver.perception.position, message.origin);
      const float distance = std::sqrt(math::dot(offset, offset));
      float strength = message.near_strength;
      if (message.radius >= 0) {
        if (distance >= message.radius) continue;
        strength += (message.far_strength - message.near_strength) *
                    (distance / message.radius);
      }
      if (!(strength > 0)) continue;
      const auto entry =
          std::ranges::find(entries, source.identity, &Entry::identity);
      if (entry == entries.end() || !entry->controller) continue;
      if (delivered == kMaximumDeliveries) break;
      entry->controller->receive_target({message.target.target,
                                         message.target.knowledge, strength,
                                         message.duration});
      ++delivered;
    }
  }
  std::uint32_t begin_attack() override {
    constexpr auto kMaximumSerial =
        static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::max());
    attack_serial = attack_serial == kMaximumSerial ? 1 : attack_serial + 1;
    return attack_serial;
  }
  bool accept_melee_serial(MeleeReceipt incoming) {
    const auto identity = incoming.identity;
    const auto serial = incoming.serial;
    auto receipt =
        std::ranges::find(melee_receipts, identity, &MeleeReceipt::identity);
    if (receipt == melee_receipts.end()) {
      receipt = std::ranges::find(melee_receipts, std::uint64_t{},
                                  &MeleeReceipt::identity);
      if (receipt == melee_receipts.end()) {
        throw std::length_error("Melee receiver capacity exceeded");
      }
      *receipt = {identity, 0};
    }
    // Original actor receipt handles the global signed serial wrapping to one.
    constexpr std::uint32_t kWrapSeparation = 1000000;
    if (receipt->serial >= serial &&
        receipt->serial - serial <= kWrapSeparation) {
      return false;
    }
    receipt->serial = serial;
    return true;
  }
  void melee_scenery(const MeleeContactVolume& packet) {
    if (packet.previous == packet.center) return;
    const auto hit =
        physics.queries().ray({.start = packet.previous,
                               .end = packet.center,
                               .layers = WorldPhysics::scenery_layers()});
    if (!hit) return;
    const auto receiver = physics.receiver(hit->receiver);
    constexpr std::uint32_t kStageGeometry = 4, kPlacedGeometry = 5;
    if (!receiver || (receiver->kind != kStageGeometry &&
                      receiver->kind != kPlacedGeometry)) {
      return;
    }
    constexpr std::uint32_t kMovableScenery = 0x4000;
    if ((receiver->object_flags & kMovableScenery) != 0) {
      throw std::runtime_error(
          "Melee scenery needs its original movable-object force receiver");
    }
    const auto effect =
        loading.combat_definitions().player.effects.surface_impact.at(
            hit->surface);
    const auto reverse = math::subtract(packet.previous, packet.center);
    // Source scenery feedback always chooses WA0 normalImpact and contextual
    // sound action8, independently of the attacking actor's weapon family.
    if (effect > 0 &&
        !effects.spawn(static_cast<std::uint32_t>(effect),
                       {.transform = math::affine_part(
                            shot_forward_transform(hit->position, reverse))})) {
      throw std::length_error("Melee scenery sprite admission exhausted");
    }
    const auto cue = PreparedCharacterAudio::sound(
        8, static_cast<std::uint8_t>(hit->surface), 0);
    if (cue &&
        !audio.play(
            {.bank = cue->bank, .cue = cue->cue, .position = hit->position})) {
      throw std::runtime_error("Melee scenery sound admission failed");
    }
  }
  void emit(const MeleeContactVolume& packet) override {
    melee_scenery(packet);
    constexpr std::size_t kContacts = 32;
    constexpr std::uint32_t kSightExclusions = 0x7200, kContactSprite = 800;
    constexpr std::uint8_t kFleshSoundSurface = 0x20;
    std::array<physics::SphereOverlap, kContacts> contacts{};
    const auto queries = physics.queries();
    const auto count = queries.overlap_sphere(
        {.center = packet.center,
         .radius = packet.radius,
         .layers = kSoldierHitLayers | (1U << kPlayerHitLayer)},
        contacts);
    // Copy completed model observations, not the pre-motion AI snapshot.
    std::array<PopulationActorObservation, kActorCapacity> actors{};
    const auto actor_count = population.observe_actors(actors);
    const auto active = std::span(actors).first(actor_count);
    const auto source = std::ranges::find_if(active, [&](const auto& value) {
      return value.identity.value == packet.source;
    });
    const auto* player_actor = simulation.actor();
    if (!player_actor) throw std::logic_error("Melee world has no player");
    const auto player_position =
        math::affine_part(player_actor->model_transform(false))[3];
    const std::optional<math::Vector3<float>> source_position =
        packet.source == kPlayerIdentity ? std::optional{player_position}
        : source != active.end()
            ? std::optional{source->state.placement.position}
            : std::nullopt;
    for (const auto& contact : std::span(contacts).first(count.written)) {
      const auto target = std::ranges::find_if(active, [&](const auto& value) {
        return value.identity.value == contact.user_data;
      });
      const bool player_target = contact.user_data == kPlayerIdentity;
      if (!player_target && target == active.end()) continue;
      if (!accept_melee_serial({contact.user_data, packet.serial})) continue;
      if (contact.user_data == packet.source) continue;
      const auto target_position =
          player_target ? player_position : target->state.placement.position;
      if (source_position && *source_position != target_position &&
          queries.ray({.start = *source_position,
                       .end = target_position,
                       .layers = WorldPhysics::scenery_layers(),
                       .exclude_flags = kSightExclusions})) {
        continue;
      }
      if (source_position && packet.effect >= 0) {
        const auto sound_class =
            source == active.end()
                ? std::uint8_t{}
                : prepared.at(source->state.placement.archetype)
                      .metadata.audio.sound_class;
        const auto cue = PreparedCharacterAudio::sound(
            static_cast<std::uint32_t>(packet.effect), kFleshSoundSurface,
            sound_class);
        if (cue && !audio.play({.bank = cue->bank,
                                .cue = cue->cue,
                                .position = contact.position})) {
          throw std::runtime_error("Melee impact sound admission failed");
        }
      }
      const float health =
          player_target ? player.snapshot().health : target->state.life;
      if (health > 0) {
        auto axis = math::subtract(contact.position, packet.center);
        constexpr float kSourceMinimumLengthSquared = 1.0000001111620804e-6F;
        if (math::dot(axis, axis) < kSourceMinimumLengthSquared) {
          axis = {0, 0, 1};
        }
        if (!effects.spawn(
                kContactSprite,
                {.transform = math::affine_part(
                     shot_forward_transform(contact.position, axis))})) {
          throw std::length_error("Melee contact sprite admission exhausted");
        }
      }
      const MeleeImpact impact{
          packet.source,    contact.user_data, source_position,   packet.center,
          contact.position, packet.damage,     packet.far_damage, 0,
          packet.radius};
      if (player_target) {
        const bool was_dead = simulation.player_dead();
        (void)simulation.receive_player_melee(impact);
        if (!was_dead && simulation.player_dead()) player_death();
      } else {
        (void)population.receive_melee(
            impact, loading.combat_definitions().soldier_damage, random);
        if (health > 0) {
          const auto owner = std::ranges::find_if(entries, [&](const Entry& e) {
            return e.identity.value == impact.target && e.audio;
          });
          if (owner != entries.end()) {
            const auto updated_count = population.observe_actors(actors);
            const auto updated = std::span(actors).first(updated_count);
            const auto receiver = std::ranges::find_if(
                updated, [&](const PopulationActorObservation& value) {
                  return value.identity.value == impact.target;
                });
            if (receiver == updated.end()) {
              throw std::logic_error("Melee receiver retired during dispatch");
            }
            owner->audio->damage_voice(receiver->state.life <= 0 ? 2U : 1U);
          }
        }
      }
    }
  }
  void broadcast_behavior(const NpcBehaviorTransmission& message) override {
    constexpr std::int32_t kDirectEnemy = -5;
    if (message.recipients != kDirectEnemy) {
      throw std::runtime_error(
          "NPC behavior broadcast receiver is outside admitted closure");
    }
    if (!message.direct_target) return;
    // This message addresses AiBehavior, not the player action controller.
    // The original player has no receiving AI component for event8/44.
    if (*message.direct_target == kPlayerIdentity) return;
    const auto entry = std::ranges::find_if(entries, [&](const Entry& value) {
      return value.identity.value == *message.direct_target && value.controller;
    });
    if (entry != entries.end()) {
      throw std::runtime_error(
          "NPC event arbitration receiver is not admitted");
    }
  }
  math::Vector3<float> light_color() const override {
    return presentation.local_light_color();
  }
  const world::WorldClock& clock;
  const MissionLoading& loading;
  MissionSimulation& simulation;
  WorldPopulation& population;
  WorldPhysics& physics;
  StagePresentation& presentation;
  CombatProjectileEmitter projectiles;
  CombatEffectEmitter effects;
  SourceRandom& random;
  audio::CueOutput& audio;
  CharacterVoicePool& voices;
  ActorSoundEvents& sound_events;
  ActorSoundStimuli sound_stimuli;
  PlayerDamageResources player;
  const scene::StaticCollision& collision;
  std::shared_ptr<memory::Domain> domain;
  const content::MissionPlacementDefinition& definition;
  formats::NavigationArchive navigation;
  NpcAiSettings settings;
  NpcReactionAdmission reactions;
  NpcSteeringSensors steering;
  std::size_t navigation_admissions{};
  BehaviorSchedule behaviors;
  PerceptionSchedule perceptions;
  NpcMovementSchedule movement_schedule;
  std::array<NpcBehaviorDefinition, kSoldierBehaviors.size()> definitions{};
  std::optional<NpcBehaviorLibrary> scripts;
  std::optional<NativeActionLibrary> actions;
  memory::Vector<Prepared> prepared;
  memory::Vector<NpcCoverPoint> cover;
  memory::Vector<NpcCoverObstacle> obstacles;
  std::array<PopulationActorObservation, kActorCapacity> population_snapshot{};
  std::array<NpcEncounterActor, kActorCapacity + 1> snapshots{};
  std::size_t snapshot_count{}, population_snapshot_count{};
  std::uint32_t attack_serial{};
  std::array<MeleeReceipt, kActorCapacity + 1> melee_receipts{};
  std::array<Entry, kActorCapacity> entries{};
};
WorldCombat::WorldCombat(
    const world::WorldClock& clock, const MissionLoading& loading,
    MissionSimulation& simulation, WorldPopulation& population,
    WorldPhysics& physics, StagePresentation& presentation,
    CombatEffects& effects, SourceRandom& random, audio::CueOutput& audio,
    CharacterAudioLookup character_audio, PlayerDamageResources player_damage,
    PlayerDefeatResources player_defeat,
    const scene::StaticCollision& collision, CharacterVoicePool& voices,
    const std::shared_ptr<memory::Domain>& domain)
    : simulation_(simulation),
      population_(population),
      soldier_damage_(loading.combat_definitions().soldier_damage),
      random_(random),
      player_defeat_(player_defeat),
      projectiles_(clock, physics.queries(),
                   {kProjectileCapacity,
                    loading.combat_definitions().source_query_budget},
                   effects, audio, domain),
      player_weapon_(clock, loading.combat_definitions().player,
                     loading.combat_definitions().player_sound,
                     loading.combat_definitions().player_presentation, random,
                     projectiles_.emitter(), effects.emitter(),
                     presentation.acquire_local_light(), audio,
                     physics.queries(), kPlayerIdentity,
                     {WorldPhysics::scenery_layers(), kSoldierHitLayers},
                     presentation.local_light_color()) {
  auto geometry = std::allocate_shared<ActorHitGeometry>(
      memory::Allocator<ActorHitGeometry>(domain), physics.resources(),
      loading.player_hit_definition(), domain);
  simulation_.bind_player_combat({physics.resources(),
                                  physics.queries(),
                                  physics.mutations(),
                                  std::move(geometry),
                                  loading.physics_recipes().player,
                                  {physics.character_material(),
                                   kPlayerIdentity, physical_character_layer},
                                  kPlayerHitLayer});
  simulation_.bind_player_damage(
      player_damage, loading.combat_definitions().player_damage, random);
  impl_ = std::allocate_shared<Impl>(
      memory::Allocator<Impl>(domain), clock, loading, simulation, population,
      physics, presentation, projectiles_, effects, random, audio,
      character_audio, player_damage, collision, voices, *this, domain);
  // Publish the synchronous receiver only after all fallible preparation.
  simulation_.bind_weapon_receiver(player_weapon_);
}
void WorldCombat::retire_background() { impl_->obstacles.clear(); }
void WorldCombat::emit(ActorSoundEmission event) {
  impl_->sound_stimuli.emit(event.action, event.position, event.affiliation);
}
WorldCombat::~WorldCombat() {
  simulation_.unbind_weapon_receiver(player_weapon_);
}
void WorldCombat::begin_frame(const world::WorldFrame& frame) {
  impl_->begin_frame(frame);
}
void WorldCombat::prepare_player_presentation() {
  player_weapon_.set_environment_light(impl_->presentation.local_light_color());
}
void WorldCombat::advance_actors(const world::WorldFrame& frame) {
  // Original damage enqueues a recipient message. Deliver retained amounts on
  // the next admitted actor dispatch, before child recovery; never inside the
  // projectile callback. I.1/E.4 and MEM.9 keep this narrow and bounded.
  for (const auto amount :
       std::span(pending_defeats_).first(pending_defeat_count_)) {
    if (player_defeat_.receive(amount) != ResourceActionResult::committed) {
      throw std::logic_error(
          "Defeat notification needs an admitted resource frame");
    }
  }
  pending_defeat_count_ = 0;
  impl_->advance_actors(frame);
}
void WorldCombat::advance(const world::WorldFrame& frame) {
  impl_->after_pose(frame);
  player_weapon_.advance(frame, simulation_.weapon_pose());
  projectiles_.advance(frame, simulation_.presentation_camera().pose.eye,
                       *this);
}
void WorldCombat::receive(const CombatImpact& impact) {
  if (impact.target == kPlayerIdentity) {
    const bool was_dead = simulation_.player_dead();
    (void)simulation_.receive_player_hit(impact);
    if (!was_dead && simulation_.player_dead()) impl_->player_death();
    return;
  }
  const auto receipt = population_.receive(impact, soldier_damage_, random_);
  if (receipt.defeated && impact.source == kPlayerIdentity) {
    const auto actor =
        std::ranges::find_if(impl_->entries, [&](const auto& entry) {
          return entry.identity.value == impact.target && entry.actor;
        });
    if (actor == impl_->entries.end()) {
      throw std::logic_error("Defeated actor lost its authored placement");
    }
    const auto archetype = actor->actor->state().placement.archetype;
    const auto amount = impl_->definition.archetypes()[archetype].defeat_magic;
    pending_defeats_.at(pending_defeat_count_) = static_cast<float>(amount);
    ++pending_defeat_count_;
  }
  if (receipt.voice_action) {
    const auto owner =
        std::ranges::find_if(impl_->entries, [&](const auto& value) {
          return value.identity.value == impact.target && value.audio;
        });
    if (owner != impl_->entries.end()) {
      owner->audio->damage_voice(*receipt.voice_action);
    }
  }
}
}  // namespace bw::game
