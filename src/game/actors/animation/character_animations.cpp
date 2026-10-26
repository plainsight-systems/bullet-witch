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
}  // namespace
CharacterAnimations::CharacterAnimations(
    const content::CharacterSupport& support,
    const formats::SkeletonDefinition& skeleton, std::string_view owner,
    std::size_t slot_count,
    std::span<const animation::ClipAliasRequest> aliases,
    const std::shared_ptr<memory::Domain>& domain,
    PoseSemanticsPolicy& semantics, const std::function<void()>& progress)
    : motions_(prepare_motions(support, skeleton, owner, slot_count, aliases,
                               domain, progress)),
      parameters_(memory::Allocator<Parameter>(domain)),
      shapes_(memory::Allocator<Shape>(domain)),
      clip_parameters_(memory::Allocator<std::optional<std::size_t>>(domain)),
      pose_semantics_(motions_.size(), MotionPoseSemantics::unknown,
                      memory::Allocator<MotionPoseSemantics>(domain)) {
  semantics(motions_, owner, pose_semantics_);
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
