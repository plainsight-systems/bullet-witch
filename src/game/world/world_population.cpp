// Compose the original deferred manager with live scene/collision receivers.
// No fallback placements or synthetic readiness. I.11/E.6 and MEM.9 preserve
// setup-only prepared assets and bounded instance mutation described in header.
#include "world_population.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <stdexcept>

#include "bw/math/frustum.hpp"
#include "game/actors/animation/source_motion_bindings.hpp"
#include "game/actors/character_body_radius.hpp"
#include "game/actors/npc/npc_pose_semantics.hpp"
#include "game/mission/loading/mission_loading.hpp"
#include "presentation/stage_presentation.hpp"
#include "world_physics.hpp"

namespace bw::game {
namespace {
const content::MissionPlacementDefinition& placements(
    const MissionLoading& loading) {
  const auto* result = loading.population_definition();
  if (!result) throw std::logic_error("Mission has no registered population");
  return *result;
}
// Prepared locator programs borrow no model bytes after admission. Models with
// authored locators must supply the skeleton used to validate their indices.
std::shared_ptr<const formats::ControlPointDefinition> prepare_attachments(
    const ResidentModelView& resident,
    const std::shared_ptr<memory::Domain>& domain) {
  if (resident.control_points.empty()) return nullptr;
  const auto& skeleton = resident.definition.get().skeleton();
  if (!skeleton) throw std::runtime_error("NPC locators require a skeleton");
  return std::allocate_shared<formats::ControlPointDefinition>(
      memory::Allocator<formats::ControlPointDefinition>(domain),
      formats::ControlPointDefinition::parse(resident.control_points,
                                             skeleton->bones().size(), domain));
}
std::string_view motion_owner(std::string_view family) {
  if (family == "ea0" || family == "ea1" || family == "ea2") return "ea0";
  if (family == "hm0" || family == "hm1") return "hm0s";
  if (family == "hw0" || family == "hw1") return "hw0s";
  if (family == "hm2" || family == "hw2") return "hm2s";
  throw std::runtime_error("Untraced mission NPC initialization family");
}
}  // namespace
WorldPopulation::WorldPopulation(const MissionLoading& loading,
                                 const MissionSimulation& simulation,
                                 StagePresentation& presentation,
                                 PlacementRandom& random, WorldPhysics& physics,
                                 const std::shared_ptr<memory::Domain>& domain)
    : definition_(placements(loading)),
      simulation_(simulation),
      presentation_(presentation),
      random_(random),
      domain_(domain),
      population_(definition_, domain),
      body_settings_(memory::Allocator<NpcInitialBodySettings>(domain)),
      bodies_(
          memory::Allocator<std::shared_ptr<const PreparedNpcModel>>(domain)),
      weapons_(
          memory::Allocator<std::shared_ptr<const PreparedNpcModel>>(domain)),
      physics_resources_(physics.resources()),
      physics_queries_(physics.queries()),
      physics_mutations_(physics.mutations()),
      physics_material_(physics.character_material()),
      soldier_physics_(loading.physics_recipes().soldier),
      hit_geometry_(
          memory::Allocator<std::shared_ptr<const ActorHitGeometry>>(domain)) {
  const auto& stage = loading.stage();
  if (!stage) throw std::logic_error("Mission population requires stage data");
  const auto customization = formats::CustomizationDefinition::parse(
      stage->customization_bytes(), domain);
  body_settings_.reserve(definition_.archetypes().size());
  bodies_.reserve(definition_.archetypes().size());
  weapons_.resize(definition_.archetypes().size());
  hit_geometry_.resize(definition_.archetypes().size());
  std::size_t archetype_index = 0;
  for (const auto& archetype : definition_.archetypes()) {
    body_settings_.push_back(
        NpcInitialBodySettings::parse(customization, archetype.model));
    const auto resident = loading.resident_character(archetype.model);
    if (archetype.model == "ea0") {
      if (!resident.actor_hit) {
        throw std::runtime_error("Soldier EHIT is missing");
      }
      for (std::size_t prior = 0; prior < archetype_index; ++prior) {
        if (bodies_.at(prior)->resident.actor_hit == resident.actor_hit) {
          hit_geometry_.at(archetype_index) = hit_geometry_.at(prior);
          break;
        }
      }
      if (!hit_geometry_.at(archetype_index)) {
        hit_geometry_.at(archetype_index) =
            std::allocate_shared<ActorHitGeometry>(
                memory::Allocator<ActorHitGeometry>(domain), physics_resources_,
                *resident.actor_hit, domain);
      }
    }
    const auto owner_name = motion_owner(archetype.model);
    const auto& owner = owner_name == "ea0"
                            ? loading.resident_character("ea0").definition.get()
                            : loading.shared_motion_model(owner_name);
    std::shared_ptr<const CharacterAnimations> animations;
    if (archetype.model == "ea0") {
      const auto cached = std::ranges::find_if(bodies_, [&](const auto& body) {
        return body->resident.handle == resident.handle;
      });
      if (cached != bodies_.end()) {
        animations = (*cached)->animations;
      } else {
        if (!owner.skeleton()) {
          throw std::logic_error("NPC motion owner lacks skeleton");
        }
        animations = std::allocate_shared<CharacterAnimations>(
            memory::Allocator<CharacterAnimations>(domain),
            loading.character_support(owner_name), *owner.skeleton(),
            owner_name, kOriginalCharacterMotionSlots, soldier_motion_aliases(),
            domain, npc_pose_semantics);
      }
    }
    std::shared_ptr<const PreparedNpcShapes> shapes;
    if (archetype.model == "ea0") {
      const auto cached = std::ranges::find_if(bodies_, [&](const auto& body) {
        return body->resident.handle == resident.handle;
      });
      if (cached != bodies_.end()) {
        shapes = (*cached)->shapes;
      } else {
        // Original stage resource producer binds eight nonrepeating names;
        // absent publications keep their slot instead of shifting later IDs.
        constexpr std::array<std::string_view, 8> kSoldierExpressions{
            "@ea0_1", "@ea0_2", "@ea0_3", "@ea0_4",
            "@ea0_5", "@ea0_6", "@ea0_7", "@ea0_8"};
        shapes = std::allocate_shared<PreparedNpcShapes>(
            memory::Allocator<PreparedNpcShapes>(domain),
            resident.definition.get(), loading.character_support(owner_name),
            kSoldierExpressions, domain);
      }
    }
    const auto slots = loading.npc_texture_slots(resident);
    bodies_.push_back(std::allocate_shared<PreparedNpcModel>(
        memory::Allocator<PreparedNpcModel>(domain), resident,
        NpcInitialMotion::prepare(resident.definition.get(), owner,
                                  loading.character_support(owner_name),
                                  owner_name, domain),
        PreparedNpcAppearance::prepare(archetype, resident,
                                       loading.npc_palettes(), slots, domain),
        std::move(animations), std::move(shapes),
        prepare_attachments(resident, domain)));
    if (archetype.model.starts_with("ea")) {
      const auto name =
          archetype.weapon.empty() ? std::string_view("wg1") : archetype.weapon;
      const auto gun = loading.resident_character(name);
      const auto cached =
          std::ranges::find_if(weapons_, [&](const auto& model) {
            return model && model->resident.handle == gun.handle;
          });
      if (cached != weapons_.end()) {
        weapons_.at(archetype_index) = *cached;
      } else {
        std::shared_ptr<const CharacterAnimations> weapon_animations;
        if (name == "wg1") {
          const auto& skeleton = gun.definition.get().skeleton();
          if (!skeleton) throw std::logic_error("NPC weapon lacks skeleton");
          weapon_animations = std::allocate_shared<CharacterAnimations>(
              memory::Allocator<CharacterAnimations>(domain),
              loading.character_support(name), *skeleton, name, 0,
              std::span<const animation::ClipAliasRequest>{}, domain,
              npc_pose_semantics);
        }
        // Weapon factory selects defaultmotion slot0 first. Its absent slot1
        // request leaves that authored controller active.
        weapons_.at(archetype_index) = std::allocate_shared<PreparedNpcModel>(
            memory::Allocator<PreparedNpcModel>(domain), gun,
            NpcInitialMotion::prepare(
                gun.definition.get(), gun.definition.get(),
                loading.character_support(name), name, domain),
            PreparedNpcAppearance::default_weapon(gun, domain),
            std::move(weapon_animations), nullptr,
            prepare_attachments(gun, domain));
      }
    }
    ++archetype_index;
  }
}
WorldPopulation::~WorldPopulation() {
  // Retire scene membership while its stable actor observations still exist.
  // Native retirement is synchronous; immutable mission resources survive us.
  for (const auto& actor : actors_) {
    if (actor) {
      if (combat_lifecycle_) combat_lifecycle_->retire(actor->identity());
      presentation_.retire_npc(actor->identity());
    }
  }
}
void WorldPopulation::bind_combat_lifecycle(
    PopulationCombatLifecycle& lifecycle) {
  if (combat_lifecycle_ || population_.active_count() != 0) {
    throw std::logic_error(
        "Combat lifecycle binds before population admission");
  }
  combat_lifecycle_ = &lifecycle;
}
void WorldPopulation::unbind_combat_lifecycle(
    PopulationCombatLifecycle& lifecycle) noexcept {
  if (combat_lifecycle_ != &lifecycle) return;
  for (const auto& actor : actors_) {
    if (actor) lifecycle.retire(actor->identity());
  }
  combat_lifecycle_ = nullptr;
}
math::Vector3<float> WorldPopulation::project_ground(
    const SpawnCandidate& candidate) const {
  auto from = candidate.position, to = candidate.position;
  if (candidate.ground_3d) {
    constexpr float kProjectionDepth = 2500;
    to[1] -= kProjectionDepth;
  } else {
    constexpr float kProjectionTop = 2500, kProjectionBottom = -5000;
    from[1] = kProjectionTop;
    to[1] = kProjectionBottom;
  }
  const auto hit = simulation_.clip_scenery({from, to, .01F, 0});
  return hit.clipped ? hit.position : candidate.position;
}
bool WorldPopulation::visible(const math::Vector3<float>& position) const {
  const auto& camera = simulation_.presentation_camera();
  const auto& view = camera.perspective;
  const float height =
      std::tan(view.vertical_fov_radians * .5F) * view.near_plane;
  const float width = height * view.aspect;
  const auto& pose = camera.pose;
  const auto frustum = math::make_perspective_frustum(
      {pose.right, pose.up, pose.forward, pose.eye},
      {-width, width, -height, height, view.near_plane, view.far_plane});
  return math::intersects(frustum, position, 1.F);
}
bool WorldPopulation::admit(PopulationIdentity identity,
                            const PopulationActorState& incoming) {
  const auto free =
      std::ranges::find_if(actors_, [](const auto& actor) { return !actor; });
  if (free == actors_.end()) return false;
  auto state = incoming;
  const auto& archetype = definition_.archetypes()[state.placement.archetype];
  // Source constructor enables deferred retirement (actor1238 bit4). The
  // model-family classification at spawn+0xb4 and no death/special mode
  // complete its gate; authored row6 flags at spawn+0x10 are separate.
  const auto flags = population_family_flags(archetype.model);
  constexpr std::uint32_t kNonRetiringFlags = 0x7801U;
  state.retirement_eligible = flags != 0 && (flags & kNonRetiringFlags) == 0;
  const auto radius = body_settings_.at(state.placement.archetype).radius;
  const float effective_radius = character_body_radius(radius, state.life > 0);
  auto down = state.placement.position;
  constexpr float kInitialGroundDepth = 5;
  down[1] -= kInitialGroundDepth;
  const auto ground =
      simulation_.clip_scenery({state.placement.position, down, .01F, 0});
  if (!ground.clipped) {
    throw std::runtime_error(
        "NPC initialization requires original airborne StateStart receiver");
  }
  state.placement.position = ground.position;
  state.placement.position[1] += effective_radius;
  auto actor = std::allocate_shared<NpcActor>(
      memory::Allocator<NpcActor>(domain_), identity, state,
      *bodies_.at(state.placement.archetype),
      weapons_.at(state.placement.archetype).get(), radius, domain_);
  if (const auto& hit = hit_geometry_.at(state.placement.archetype)) {
    constexpr std::uint8_t kSoldierHitLayer = 6;
    actor->bind_combat(
        physics_resources_, physics_queries_, physics_mutations_, hit,
        soldier_physics_, physics_material_,
        NpcCombatLayers{kSoldierHitLayer, physical_character_layer}, domain_);
  }
  presentation_.admit_npc(*actor, simulation_.presentation_camera());
  try {
    if (combat_lifecycle_) combat_lifecycle_->admit(*actor, archetype);
  } catch (...) {
    if (combat_lifecycle_) combat_lifecycle_->retire(identity);
    presentation_.retire_npc(identity);
    throw;
  }
  *free = std::move(actor);
  return true;
}
PopulationActorState WorldPopulation::observe(
    PopulationIdentity identity) const {
  const auto found = std::ranges::find_if(actors_, [&](const auto& actor) {
    return actor && actor->identity() == identity;
  });
  if (found == actors_.end()) {
    throw std::logic_error("Missing live NPC identity");
  }
  return (*found)->state();
}
void WorldPopulation::retire(PopulationIdentity identity) {
  const auto found = std::ranges::find_if(actors_, [&](const auto& actor) {
    return actor && actor->identity() == identity;
  });
  if (found == actors_.end()) {
    throw std::logic_error("Missing retired NPC identity");
  }
  if (combat_lifecycle_) combat_lifecycle_->retire(identity);
  presentation_.retire_npc(identity);
  found->reset();
}
void WorldPopulation::remove_family(PopulationFamily family) {
  population_.remove_family(family, *this);
}
void WorldPopulation::advance(double seconds) {
  update_population();
  advance_actors(seconds);
}
void WorldPopulation::update_population() {
  const auto* player = simulation_.player_state();
  if (!player) {
    throw std::logic_error(
        "Mission population requires live player observation");
  }
  const auto& camera = simulation_.presentation_camera();
  constexpr float kNarrowCamera = 10.F * std::numbers::pi_v<float> / 180.F;
  population_.advance({player->pose.position, camera.pose.eye,
                       camera.perspective.vertical_fov_radians < kNarrowCamera},
                      random_, *this);
}
void WorldPopulation::advance_actors(double seconds) {
  // Actor state only requests retirement. Complete the traversal before the
  // world removes combat/audio/render/physics borrowers and the population ID.
  // I.11/E.4 and MEM.9 were reread: bounded copied identities, no raw ownership
  // transfer or additional allocation during this lifetime transaction.
  std::array<PopulationIdentity, kActiveCapacity> retired{};
  std::size_t retired_count = 0;
  const auto& camera = simulation_.presentation_camera().pose;
  for (const auto& actor : actors_) {
    if (!actor) continue;
    actor->advance(seconds, presentation_.actor_publisher());
    const auto& state = actor->state();
    const auto& archetype = definition_.archetypes()[state.placement.archetype];
    if (actor->corpse_retirement_due(
            {state.placement.position, camera.eye, camera.forward,
             population_family_flags(archetype.model), false})) {
      retired.at(retired_count++) = actor->identity();
    }
  }
  for (const auto identity : std::span(retired).first(retired_count)) {
    if (!population_.retire_dead(identity, *this)) {
      throw std::logic_error("Death retirement lost its active identity");
    }
  }
}
std::size_t WorldPopulation::observe_actors(
    std::span<PopulationActorObservation> destination) const {
  if (destination.size() < population_.active_count()) {
    throw std::length_error("Population observation capacity is too small");
  }
  std::size_t count = 0;
  // Reused storage slots do not change source admission/traversal order.
  for (const auto identity : population_.active_identities()) {
    const auto found = std::ranges::find_if(actors_, [&](const auto& actor) {
      return actor && actor->identity() == identity;
    });
    if (found == actors_.end()) {
      throw std::logic_error("Active population identity has no actor");
    }
    const auto& actor = *found;
    const auto& transform = actor->model_transform(false);
    destination[count++] = {identity,
                            actor->state(),
                            {transform[2][0], transform[2][1], transform[2][2]},
                            actor->velocity(),
                            actor->aim_position(),
                            actor->physical()};
  }
  return count;
}
HitReception WorldPopulation::receive(const CombatImpact& impact,
                                      const ActorDamageDefinition& definition,
                                      SourceRandom& random) {
  const auto found = std::ranges::find_if(actors_, [&](const auto& actor) {
    return actor && actor->identity().value == impact.target;
  });
  return found != actors_.end()
             ? (*found)->receive_ordinary_hit(impact, definition, random)
             : HitReception{};
}
bool WorldPopulation::receive_melee(const MeleeImpact& impact,
                                    const ActorDamageDefinition& definition,
                                    SourceRandom& random) {
  const auto found = std::ranges::find_if(actors_, [&](const auto& actor) {
    return actor && actor->identity().value == impact.target;
  });
  return found != actors_.end() &&
         (*found)->receive_melee(impact, definition, random);
}
void WorldPopulation::finish_physics() {
  for (const auto& actor : actors_) {
    if (actor) actor->finish_physics(presentation_.actor_publisher());
  }
}
}  // namespace bw::game
