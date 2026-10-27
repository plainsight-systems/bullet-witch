// Compose the original deferred manager with live scene/collision receivers.
// No fallback placements or synthetic readiness. I.11/E.6 and MEM.9 preserve
// setup-only prepared assets and bounded instance mutation described in header.
#include "world_population.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <stdexcept>

#include "bw/math/frustum.hpp"
#include "game/mission/loading/mission_loading.hpp"
#include "presentation/stage_presentation.hpp"
namespace bw::game {
namespace {
const content::MissionPlacementDefinition& placements(
    const MissionLoading& loading) {
  const auto* result = loading.population_definition();
  if (!result) throw std::logic_error("Mission has no registered population");
  return *result;
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
                                 PlacementRandom& random,
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
          memory::Allocator<std::shared_ptr<const PreparedNpcModel>>(domain)) {
  const auto& stage = loading.stage();
  if (!stage) throw std::logic_error("Mission population requires stage data");
  const auto customization = formats::CustomizationDefinition::parse(
      stage->customization_bytes(), domain);
  body_settings_.reserve(definition_.archetypes().size());
  bodies_.reserve(definition_.archetypes().size());
  weapons_.resize(definition_.archetypes().size());
  std::size_t archetype_index = 0;
  for (const auto& archetype : definition_.archetypes()) {
    body_settings_.push_back(
        NpcInitialBodySettings::parse(customization, archetype.model));
    const auto resident = loading.resident_character(archetype.model);
    const auto owner_name = motion_owner(archetype.model);
    const auto& owner = owner_name == "ea0"
                            ? loading.resident_character("ea0").definition.get()
                            : loading.shared_motion_model(owner_name);
    const auto slots = loading.npc_texture_slots(resident);
    bodies_.push_back(std::allocate_shared<PreparedNpcModel>(
        memory::Allocator<PreparedNpcModel>(domain), resident,
        NpcInitialMotion::prepare(resident.definition.get(), owner,
                                  loading.character_support(owner_name),
                                  owner_name, domain),
        PreparedNpcAppearance::prepare(archetype, resident,
                                       loading.npc_palettes(), slots, domain)));
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
        // Weapon factory selects defaultmotion slot0 first. Its absent slot1
        // request leaves that authored controller active.
        weapons_.at(archetype_index) = std::allocate_shared<PreparedNpcModel>(
            memory::Allocator<PreparedNpcModel>(domain), gun,
            NpcInitialMotion::prepare(
                gun.definition.get(), gun.definition.get(),
                loading.character_support(name), name, domain),
            PreparedNpcAppearance::default_weapon(gun, domain));
      }
    }
    ++archetype_index;
  }
}
WorldPopulation::~WorldPopulation() {
  // Retire scene membership while its stable actor observations still exist.
  // Native retirement is synchronous; immutable mission resources survive us.
  for (const auto& actor : actors_) {
    if (actor) presentation_.retire_npc(actor->identity());
  }
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
  const std::uint32_t flags = archetype.model.starts_with("ea") ? 2U : 8U;
  constexpr std::uint32_t kNonRetiringFlags = 0x7801U;
  state.retirement_eligible = flags != 0 && (flags & kNonRetiringFlags) == 0;
  const auto radius = body_settings_.at(state.placement.archetype).radius;
  const float effective_radius =
      radius <= 1.F && state.life <= 0 ? radius * 2 : radius;
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
      weapons_.at(state.placement.archetype).get(), effective_radius, domain_);
  presentation_.admit_npc(*actor, simulation_.presentation_camera());
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
  presentation_.retire_npc(identity);
  found->reset();
}
void WorldPopulation::advance(double seconds) {
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
  for (const auto& actor : actors_) {
    if (actor) actor->advance(seconds, presentation_.actor_publisher());
  }
}
}  // namespace bw::game
