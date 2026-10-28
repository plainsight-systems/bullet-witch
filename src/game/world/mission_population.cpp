// Family retirement preserves source live/pending ordering with bounded
// compaction. Core I.30 and MEM.9 were consulted in both phases; no copied
// callback registry or secondary lifetime owner is introduced.
// Math boundary: both-phase Core F.4 / performance MEM.9 keep generic
// fixed-value operations in bw::math; source policy remains with this owner.
// Expand source groups into retained authored placement candidates. Definitions
// and RNG are explicit borrowed capabilities; no world/renderer mutation
// occurs. Source mode0 subdivides the longest axis, shuffles cell indexes using
// 15-bit values, then applies shape rotation/translation. One-instance regions
// use their center. Ground projection is left to the explicit world consumer.
// Fixed arrays bound256 pending candidates/1024 sample cells; failures publish
// no candidates. Algorithm/lifetime are in mission_population.hpp. Core
// SL.con.3 (#rsl-bounds) bounds spans/counts; both-phase performance MEM.9
// (guidelines/memory/MEM.9-embedded-allocate-at-init.md) removes hot
// allocation.
#include "mission_population.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

#include "bw/content/formats/binary_reader.hpp"
#include "bw/math/euler.hpp"
namespace bw::game {
namespace {
using resources::detail::malformed;
using resources::detail::unsupported;
constexpr std::size_t kCellCapacity = MissionPopulationExpansion::kCapacity * 4;
using Position = math::Vector3<float>;
constexpr float kFullExtent = 2, kHalfCell = .5F;
std::size_t random_index(PlacementRandom& random, std::size_t cells) {
  constexpr std::uint16_t kMaximum = 32767;
  const auto value = random.next_random15();
  if (value > kMaximum) {
    malformed("Mission RNG must return 15-bit values");
  }
  return static_cast<std::size_t>(
      (static_cast<float>(value) * static_cast<float>(cells - 1)) /
      static_cast<float>(kMaximum));
}
// Source distribution uses separated cells, not independent uniform positions.
void distribute(const content::PlacementShape& shape,
                std::span<Position> output, PlacementRandom& random) {
  if (output.size() == 1) {
    output.front() = shape.center;
    return;
  }
  if (shape.three_dimensional ||
      shape.kind == content::PlacementShapeKind::point) {
    unsupported("Unsupported mission population shape");
  }
  auto extent = shape.half_extent;
  if (shape.kind != content::PlacementShapeKind::box) {
    extent = {shape.half_extent[0], 0, shape.half_extent[0]};
  }
  extent[1] = 0;
  auto cell_size = math::scale(extent, kFullExtent);
  std::array<std::size_t, 3> divisions{1, 1, 1};
  std::size_t cells = 1;
  while (cells < output.size() * 4) {
    std::size_t axis = 0;
    if (cell_size[0] < cell_size[2] || cell_size[0] < cell_size[1]) {
      axis = cell_size[2] < cell_size[0] || cell_size[2] < cell_size[1] ? 1 : 2;
    }
    divisions.at(axis) *= 2;
    cell_size.at(axis) *= kHalfCell;
    cells *= 2;
  }
  if (cells > kCellCapacity) {
    unsupported("Mission population scratch capacity exceeded");
  }
  std::array<std::array<std::size_t, 3>, kCellCapacity> indexes{};
  std::size_t filled = 0;
  for (std::size_t x = 0; x < divisions[0]; ++x) {
    for (std::size_t y = 0; y < divisions[1]; ++y) {
      for (std::size_t z = 0; z < divisions[2]; ++z) {
        indexes.at(filled++) = {x, y, z};
      }
    }
  }
  for (std::size_t step = 0; step < cells * 4; ++step) {
    const auto first = random_index(random, cells);
    const auto second = random_index(random, cells);
    std::swap(indexes.at(first), indexes.at(second));
  }
  const auto rotation = math::planar_rotation(shape.yaw_radians);
  std::size_t accepted = 0;
  for (std::size_t index = 0; index < cells && accepted < output.size();
       ++index) {
    Position local{};
    for (std::size_t axis = 0; axis < 3; ++axis) {
      local.at(axis) =
          (static_cast<float>(indexes.at(index).at(axis)) + kHalfCell) *
              cell_size.at(axis) -
          extent.at(axis);
    }
    if (shape.kind != content::PlacementShapeKind::box &&
        math::dot(local, local) > shape.half_extent[0] * shape.half_extent[0]) {
      continue;
    }
    if (shape.kind == content::PlacementShapeKind::box &&
        shape.yaw_radians != 0) {
      const auto rotated = math::rotate_planar(
          math::Vector<float, 2>{local[0], local[2]}, rotation);
      local[0] = rotated[0];
      local[2] = rotated[1];
    }
    output[accepted++] = math::add(shape.center, local);
  }
  if (accepted != output.size()) {
    unsupported("Mission shape yielded insufficient population samples");
  }
}
}  // namespace
std::uint16_t PlacementRandomStream::next_random15() {
  constexpr std::uint32_t kMultiplier = 214013, kIncrement = 2531011,
                          kMask = 0x7fff;
  constexpr unsigned kShift = 16;
  state_ = state_ * kMultiplier + kIncrement;
  return static_cast<std::uint16_t>((state_ >> kShift) & kMask);
}
std::span<const SpawnCandidate> MissionPopulationExpansion::prepare(
    const content::MissionPlacementDefinition& definition,
    std::int32_t group_id, PlacementRandom& random) {
  count_ = 0;
  std::array<std::size_t, kCapacity> rows{};
  std::size_t row_count = 0, population = 0;
  const auto groups = definition.groups();
  for (std::size_t row = 0; row < groups.size(); ++row) {
    if (groups[row].id != group_id) {
      continue;
    }
    if (row_count == kCapacity || groups[row].count > kCapacity - population) {
      unsupported("Mission population exceeds deferred record capacity");
    }
    rows.at(row_count++) = row;
    population += groups[row].count;
  }
  auto selected = std::span(rows).first(row_count);
  // The original groups shared regions before sampling. Preserve file order
  // among equal names; selected One Shot Kill rows have distinct region names.
  for (std::size_t i = 1; i < selected.size(); ++i) {
    auto j = i;
    while (j > 0 &&
           definition.shapes()[groups[selected[j]].shape].name <
               definition.shapes()[groups[selected[j - 1]].shape].name) {
      std::swap(selected[j], selected[j - 1]);
      --j;
    }
  }
  std::array<Position, kCapacity> positions{};
  std::size_t produced = 0;
  for (std::size_t first = 0; first < selected.size();) {
    const auto shape_index = groups[selected[first]].shape;
    std::size_t end = first, region_count = 0;
    while (end < selected.size() &&
           groups[selected[end]].shape == shape_index) {
      region_count += groups[selected[end]].count;
      ++end;
    }
    const auto& shape = definition.shapes()[shape_index];
    distribute(shape, std::span(positions).first(region_count), random);
    std::size_t position = 0;
    for (auto row = first; row < end; ++row) {
      const auto& group = groups[selected[row]];
      const auto& archetype = definition.archetypes()[group.archetype];
      for (std::uint32_t instance = 0; instance < group.count; ++instance) {
        candidates_.at(produced++) = {group.archetype,
                                      group.source_row,
                                      positions.at(position++),
                                      group.yaw_degrees,
                                      group.admission_distance,
                                      archetype.retirement_distance > 0
                                          ? archetype.retirement_distance
                                          : group.admission_distance,
                                      true,
                                      shape.three_dimensional};
      }
    }
    first = end;
  }
  count_ = produced;
  return candidates();
}

MissionPopulation::MissionPopulation(
    const content::MissionPlacementDefinition& definition,
    const std::shared_ptr<memory::Domain>& domain)
    : definition_(definition),
      fired_(definition.triggers().size(), 0,
             memory::Allocator<std::uint8_t>(domain)) {
  if (fired_.size() > PopulationCheckpoint::kTriggers) {
    throw std::length_error("Mission trigger capacity exceeded");
  }
}

PopulationCheckpoint MissionPopulation::checkpoint(
    const PopulationReceivers& receivers) const {
  PopulationCheckpoint result;
  result.trigger_count = fired_.size();
  std::ranges::copy(fired_, result.fired.begin());
  // Native serialization visits living active actors before stored records.
  for (const auto identity : std::span(active_).first(active_count_)) {
    const auto state = receivers.observe(identity);
    if (state.life > 0) {
      result.records.at(result.count++) = {identity, state, false};
    }
  }
  for (const auto& pending : pending_) {
    if (pending.occupied) {
      result.records.at(result.count++) = {pending.identity, pending.state,
                                           true};
    }
  }
  return result;
}

std::size_t MissionPopulation::pending_count() const {
  return static_cast<std::size_t>(std::ranges::count_if(
      pending_, [](const Pending& record) { return record.occupied; }));
}
// The source deferred-family classifier is distinct from authored row flags.
// Keep compatibility bits here; consumers request a typed removal family.
std::uint32_t population_family_flags(std::string_view model) {
  constexpr std::size_t kPrefixLength = 2;
  if (model.size() < kPrefixLength) return 2;
  const auto lower = [](char character) {
    return character >= 'A' && character <= 'Z'
               ? static_cast<char>(character + ('a' - 'A'))
               : character;
  };
  const auto first = lower(model[0]), second = lower(model[1]);
  if (first == 'p' && second == 'w') return 1;
  if (first == 'h') {
    if (second == 'm' || second == 'w') {
      return static_cast<std::uint32_t>(PopulationFamily::citizen);
    }
    if (second == 'r') {
      return static_cast<std::uint32_t>(PopulationFamily::resistance);
    }
  }
  return static_cast<std::uint32_t>(PopulationFamily::enemy);
}
void MissionPopulation::remove_family(PopulationFamily family,
                                      PopulationReceivers& receivers) {
  if (family != PopulationFamily::enemy &&
      family != PopulationFamily::resistance &&
      family != PopulationFamily::citizen) {
    throw std::invalid_argument("Invalid population removal family");
  }
  const auto matches = [&](const PopulationActorState& state) {
    const auto index = state.placement.archetype;
    if (index >= definition_.archetypes().size()) {
      throw std::logic_error("Population identity lost its archetype");
    }
    return (population_family_flags(definition_.archetypes()[index].model) &
            static_cast<std::uint32_t>(family)) != 0;
  };
  // Retire before compacting: an exception keeps the failing actor
  // discoverable. Earlier successful retirements remain committed and cannot be
  // replayed.
  for (std::size_t index = 0; index < active_count_;) {
    if (!matches(receivers.observe(active_.at(index)))) {
      ++index;
      continue;
    }
    receivers.retire(active_.at(index));
    const auto active = std::span(active_).first(active_count_);
    std::ranges::move(active.subspan(index + 1), active.subspan(index).begin());
    --active_count_;
  }
  for (auto& pending : pending_) {
    if (pending.occupied && matches(pending.state)) pending = {};
  }
}

std::size_t MissionPopulation::fired_count() const {
  return static_cast<std::size_t>(std::ranges::count(fired_, 1));
}
// Death permanently removes residency. Preserve identity and fired history;
// callback failure leaves the record live. Both-phase I.11/E.4 and MEM.9 keep
// this bounded owner transaction distinct from storing a distant living actor.
bool MissionPopulation::retire_dead(PopulationIdentity identity,
                                    PopulationReceivers& receivers) {
  const auto active = std::span(active_).first(active_count_);
  const auto found = std::ranges::find(active, identity);
  if (found == active.end()) return false;
  if (receivers.observe(identity).life > 0) {
    throw std::logic_error("Corpse retirement cannot remove a living actor");
  }
  receivers.retire(identity);
  std::move(found + 1, active.end(), found);
  --active_count_;
  return true;
}

void MissionPopulation::retire_distant(const PopulationObservation& view,
                                       PopulationReceivers& receivers) {
  for (std::size_t index = 0; index < active_count_; ++index) {
    const auto state = receivers.observe(active_.at(index));
    const auto delta = math::subtract(view.camera, state.placement.position);
    const auto distance = state.placement.retirement_distance;
    if (!state.retirement_eligible || state.life <= 0 ||
        math::dot(delta, delta) < distance * distance ||
        (view.narrow_camera && receivers.visible(state.placement.position))) {
      continue;
    }
    const auto free = std::ranges::find_if(
        pending_, [](const Pending& record) { return !record.occupied; });
    if (free == pending_.end()) continue;
    // Retire first, then publish stored transform/life under the same identity.
    // A receiver failure leaves the live entry untouched and discoverable.
    receivers.retire(active_.at(index));
    *free = {active_.at(index), state, 2, true};
    const auto active = std::span(active_).first(active_count_);
    std::ranges::move(active.subspan(index + 1), active.subspan(index).begin());
    --active_count_;
    break;
  }
}

void MissionPopulation::admit_pending(
    const PopulationObservation& view, PopulationReceivers& receivers,
    std::size_t live_count_before_retirement) {
  // Source permits admission at count49, reaching50; it then stops this path.
  constexpr std::size_t kAdmissionThreshold = 49;
  if (live_count_before_retirement > kAdmissionThreshold) return;
  for (auto& pending : pending_) {
    if (!pending.occupied) continue;
    if (pending.delay != 0) {
      --pending.delay;
      continue;
    }
    const auto& placement = pending.state.placement;
    const auto delta = math::subtract(view.camera, placement.position);
    if (math::dot(delta, delta) >=
            placement.admission_distance * placement.admission_distance &&
        (!view.narrow_camera || !receivers.visible(placement.position))) {
      continue;
    }
    if (receivers.admit(pending.identity, pending.state)) {
      active_.at(active_count_++) = pending.identity;
      pending.occupied = false;
    }
    // A factory miss stops the pass as well; later pending records do not jump
    // ahead of the source-selected admission candidate.
    return;
  }
}

void MissionPopulation::scan_triggers(const PopulationObservation& view,
                                      PlacementRandom& random,
                                      PopulationReceivers& receivers) {
  const auto triggers = definition_.triggers();
  for (std::size_t row = 0; row < triggers.size(); ++row) {
    if (fired_.at(row) != 0) continue;
    const auto id = triggers[row].id;
    // Native helper tests every matching group trap. Fired state nevertheless
    // belongs to each source row; duplicate group IDs are not deduplicated.
    const auto hit = std::ranges::any_of(triggers, [&](const auto& trigger) {
      return trigger.id == id &&
             definition_.contains_2d(trigger.shape, view.player);
    });
    if (!hit) continue;
    const auto candidates = expansion_.prepare(definition_, id, random);
    if (candidates.size() > kCapacity - pending_count() ||
        candidates.size() >
            std::numeric_limits<std::uint32_t>::max() - next_identity_) {
      throw std::runtime_error("Mission deferred population capacity exceeded");
    }
    // Complete all ground queries before publishing this group. The real miss
    // policy belongs to the world query; absent collision is not a miss.
    std::array<PopulationActorState, kCapacity> staged{};
    for (std::size_t index = 0; index < candidates.size(); ++index) {
      auto placement = candidates[index];
      placement.position = receivers.project_ground(placement);
      if (!math::all_finite(placement.position)) {
        throw std::runtime_error("Mission ground query returned invalid pose");
      }
      placement.ground_projection_required = false;
      const auto life = static_cast<float>(
          definition_.archetypes()[placement.archetype].life);
      staged.at(index) = {placement, life, life, false};
    }
    std::size_t candidate = 0;
    for (auto& pending : pending_) {
      if (candidate == candidates.size()) break;
      if (!pending.occupied) {
        pending = {{next_identity_++}, staged.at(candidate++), 0, true};
      }
    }
    fired_.at(row) = 1;
  }
}

void MissionPopulation::advance(const PopulationObservation& observation,
                                PlacementRandom& random,
                                PopulationReceivers& receivers) {
  if (!math::all_finite(observation.player) ||
      !math::all_finite(observation.camera)) {
    throw std::invalid_argument("Invalid mission population observation");
  }
  const auto count_before = active_count_;
  retire_distant(observation, receivers);
  admit_pending(observation, receivers, count_before);
  scan_triggers(observation, random, receivers);
}
}  // namespace bw::game
