// Prepare pose representation beside catalog identity, using the original
// verified actor receiver slots and one explicit asset witness. Both-phase I.1
// and CDSA.32 keep ordinary poses out of delta consumers and avoid repeated
// source lookups. This metadata does not change source aliases or playback
// semantics. Adapt retained character publications to immutable animation
// inputs once. Header records ordering, ownership and both-phase
// I.11/E.4/CDSA.32/MEM.9. Exact suffix validation avoids accidental name
// matches. Parameter identity remains independent from motion identity,
// including parameter-only entries.
#include "character_animations.hpp"

#include <algorithm>
#include <stdexcept>

namespace bw::game {
namespace {
struct PublicationIdentity {
  std::string_view publication, owner;
};
std::string_view publication_name(PublicationIdentity identity) {
  const auto [publication, owner] = identity;
  const auto dot = publication.rfind('.');
  if (dot == std::string_view::npos) {
    throw std::invalid_argument("Character publication lacks its name");
  }
  const auto name = publication.substr(dot + 1);
  // The original weapon list publishes its neutral resource without a prefix.
  if (name == "defaultmotion") return name;
  if (!name.starts_with(owner) || name.size() <= owner.size() + 1 ||
      name.at(owner.size()) != '_') {
    throw std::invalid_argument("Character publication has another owner");
  }
  return name.substr(owner.size() + 1);
}
animation::PreparedClipCatalog prepare_motions(
    const content::CharacterSupport& support,
    const formats::SkeletonDefinition& skeleton, std::string_view owner,
    std::size_t slot_count,
    std::span<const animation::ClipAliasRequest> aliases,
    const std::shared_ptr<memory::Domain>& domain,
    const std::function<void()>& progress) {
  memory::Vector<animation::NamedClipSource> sources{
      memory::Allocator<animation::NamedClipSource>(domain)};
  sources.reserve(support.motions.size());
  for (const auto& motion : support.motions) {
    sources.push_back({publication_name({motion.publication.name, owner}),
                       motion.definition});
  }
  return {sources, skeleton, slot_count, aliases, domain, progress};
}
template <typename T>
void index_publications(memory::Vector<T>& entries) {
  std::ranges::sort(entries, {}, &T::name);
  for (std::size_t index = 1; index < entries.size(); ++index) {
    if (entries.at(index - 1).name == entries.at(index).name) {
      throw std::invalid_argument("Duplicate character publication name");
    }
  }
}
template <typename T>
const T* find_publication(const memory::Vector<T>& entries,
                          std::string_view name) {
  const auto found = std::ranges::lower_bound(entries, name, {}, &T::name);
  return found != entries.end() && found->name == name ? &*found : nullptr;
}
// These bounded receiver ranges are actor policy, not format heuristics. A
// publication-table entry alone does not prove its consumer representation.
MotionPoseSemantics alicia_slot_semantics(std::size_t slot) {
  struct Range {
    std::size_t first, last;
  };
  constexpr std::array ordinary_ranges{
      Range{0x130, 0x28f},  // Shooter switch/reload/discharge/raise/lower.
      Range{656, 704},      // Runner scripted start/flight/landing.
      Range{706, 721},      // Close attack, Dodger, fall.
      Range{730, 740},      // Weighted and crouched death.
      Range{749, 760}};     // Directional ordinary damage.
  constexpr std::array<std::size_t, 12> ordinary_slots{
      767, 770, 771, 779, 782, 783, 784, 785, 786, 787, 788, 789};
  constexpr std::array<std::size_t, 3> grounded_families{0, 16, 144};
  constexpr std::size_t kDirectionMask = 15, kFirstRaisedDirection = 8,
                        kLastRaisedDirection = 14;
  const auto direction = slot & kDirectionMask;
  const auto family = slot & ~kDirectionMask;
  const bool grounded =
      std::ranges::find(grounded_families, family) != grounded_families.end() &&
      (direction <= 1 || (direction >= kFirstRaisedDirection &&
                          direction <= kLastRaisedDirection));
  if (grounded ||
      std::ranges::any_of(ordinary_ranges,
                          [slot](const Range& range) {
                            return slot >= range.first && slot <= range.last;
                          }) ||
      std::ranges::find(ordinary_slots, slot) != ordinary_slots.end()) {
    return MotionPoseSemantics::ordinary;
  }
  constexpr std::size_t kFirstRecoilSlot = 0xd0;
  constexpr std::size_t kRecoilSlotsEnd = 0x130;
  constexpr std::size_t kAirRecoilSlot = 0x2c1;
  constexpr std::size_t kDamageDeltaSlot = 0x394;
  if ((slot >= kFirstRecoilSlot && slot < kRecoilSlotsEnd) ||
      slot == kAirRecoilSlot || slot == kDamageDeltaSlot) {
    return MotionPoseSemantics::additive;
  }
  return MotionPoseSemantics::unknown;
}
memory::Vector<MotionPoseSemantics> prepare_pose_semantics(
    const animation::PreparedClipCatalog& catalog, std::string_view owner,
    const std::shared_ptr<memory::Domain>& domain) {
  memory::Vector<MotionPoseSemantics> result(
      catalog.size(), MotionPoseSemantics::unknown,
      memory::Allocator<MotionPoseSemantics>(domain));
  constexpr std::array weapon_owners{"wa0", "wa1", "wa2", "wa3"};
  if (std::ranges::find(weapon_owners, owner) != weapon_owners.end()) {
    // The original weapon constructor registers these exact ordinary inputs;
    // unrelated resources in the same package do not inherit that contract.
    constexpr std::array<std::string_view, 7> names{
        "defaultmotion", "stand_l",   "shoot_l",    "stand_reload",
        "shoot_reload",  "shoot_end", "shoot_after"};
    for (const auto name : names) {
      if (const auto clip = catalog.find(name)) {
        result.at(*clip) = MotionPoseSemantics::ordinary;
      }
    }
    return result;
  }
  if (owner != "pw0") return result;
  for (const auto& binding : alicia_motion_aliases()) {
    const auto semantics = alicia_slot_semantics(binding.slot);
    if (semantics == MotionPoseSemantics::unknown ||
        binding.slot >= catalog.slot_count()) {
      continue;
    }
    const auto clip = catalog.alias(binding.slot).clip;
    if (!clip || catalog.name(*clip) != binding.name) continue;
    auto& destination = result.at(*clip);
    if (destination != MotionPoseSemantics::unknown &&
        destination != semantics) {
      throw std::invalid_argument("Conflicting source pose representations");
    }
    destination = semantics;
  }
  // This exact unaliased asset has independently witnessed delta data. Its
  // source action caller remains unresolved; this compatibility entry admits
  // representation, not a fabricated action or a filename suffix heuristic.
  if (const auto clip = catalog.find("shoot_stand_fire_add")) {
    result.at(*clip) = MotionPoseSemantics::additive;
  }
  return result;
}
}  // namespace
CharacterAnimations::CharacterAnimations(
    const content::CharacterSupport& support,
    const formats::SkeletonDefinition& skeleton, std::string_view owner,
    std::size_t slot_count,
    std::span<const animation::ClipAliasRequest> aliases,
    const std::shared_ptr<memory::Domain>& domain,
    const std::function<void()>& progress)
    : motions_(prepare_motions(support, skeleton, owner, slot_count, aliases,
                               domain, progress)),
      parameters_(memory::Allocator<Parameter>(domain)),
      shapes_(memory::Allocator<Shape>(domain)),
      clip_parameters_(memory::Allocator<std::optional<std::size_t>>(domain)),
      pose_semantics_(prepare_pose_semantics(motions_, owner, domain)) {
  parameters_.reserve(support.parameter_order.size());
  for (const auto index : support.parameter_order) {
    const auto& publication = support.publications.at(index);
    const auto bytes =
        support.archives.at(publication.archive).payload(publication.record);
    auto events = formats::MotionEvents::parse(bytes, domain);
    auto presentation = PreparedMotionPresentation(events, domain);
    constexpr std::size_t kTransitionChannel = 5;
    const auto transitions = events.channels().at(kTransitionChannel).records;
    const auto blend =
        transitions.empty()
            ? std::nullopt
            : std::optional(BlendFrames{transitions.front().start_frame,
                                        transitions.front().end_frame});
    // Moving metadata preserves its allocated records and the command views.
    parameters_.push_back(
        {publication_name({publication.name, owner}), bytes,
         formats::MotionCancellation::parse(bytes).first_frame,
         std::move(events), std::move(presentation), blend});
    if (progress) progress();
  }
  index_publications(parameters_);
  shapes_.reserve(support.shapes.size());
  for (const auto& shape : support.shapes) {
    shapes_.push_back(
        {publication_name({shape.publication.name, owner}), shape.definition});
  }
  index_publications(shapes_);
  clip_parameters_.reserve(motions_.size());
  for (std::size_t clip = 0; clip < motions_.size(); ++clip) {
    clip_parameters_.push_back(parameter_index(motions_.name(clip)));
  }
}
const CharacterAnimations::Parameter* CharacterAnimations::parameter(
    std::string_view name) const {
  return find_publication(parameters_, name);
}
std::optional<std::size_t> CharacterAnimations::parameter_index(
    std::string_view name) const {
  const auto* found = parameter(name);
  if (found == nullptr) return std::nullopt;
  return static_cast<std::size_t>(found - parameters_.data());
}
const CharacterAnimations::Shape* CharacterAnimations::shape(
    std::string_view name) const {
  return find_publication(shapes_, name);
}
}  // namespace bw::game
