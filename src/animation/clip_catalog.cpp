// Resolve immutable motion aliases before decoding their shared resources.
// Contracts and design I.11/E.4/CDSA.32/MEM.9 are in clip_catalog.hpp.
// Implementation reconsulted Core E.4 and performance MEM.9: duplicate names
// or slots reject before publication; all temporary/retained storage uses the
// domain. Playback reads stable indices without allocation or string search.
#include "bw/animation/clip_catalog.hpp"

#include <algorithm>
#include <numeric>
#include <stdexcept>

namespace bw::animation {
PreparedClipCatalog::PreparedClipCatalog(
    std::span<const NamedClipSource> sources,
    const formats::SkeletonDefinition& skeleton, std::size_t slot_count,
    std::span<const ClipAliasRequest> aliases,
    const std::shared_ptr<memory::Domain>& domain,
    const std::function<void()>& progress)
    : names_(memory::Allocator<memory::String>(domain)),
      name_order_(memory::Allocator<std::size_t>(domain)),
      clips_(memory::Allocator<PreparedClip>(domain)),
      raw_repeat_(memory::Allocator<std::uint8_t>(domain)),
      aliases_(slot_count, PreparedClipAlias{},
               memory::Allocator<PreparedClipAlias>(domain)) {
  names_.reserve(sources.size());
  for (const auto& source : sources) {
    if (source.name.empty()) throw std::invalid_argument("Empty clip name");
    names_.emplace_back(source.name, memory::Allocator<char>(domain));
  }
  name_order_.resize(sources.size());
  std::iota(name_order_.begin(), name_order_.end(), std::size_t{0});
  std::ranges::sort(name_order_, {}, [&](std::size_t index) {
    return std::string_view(names_.at(index));
  });
  for (std::size_t index = 1; index < name_order_.size(); ++index) {
    if (names_.at(name_order_.at(index - 1)) ==
        names_.at(name_order_.at(index))) {
      throw std::invalid_argument("Duplicate clip publication name");
    }
  }
  memory::Vector<std::optional<bool>> repeat(
      sources.size(), std::nullopt,
      memory::Allocator<std::optional<bool>>(domain));
  memory::Vector<std::uint8_t> assigned(
      slot_count, 0, memory::Allocator<std::uint8_t>(domain));
  // Shared-resource policy follows registration order, not alias access order.
  for (const auto& request : aliases) {
    if (request.slot >= slot_count || assigned.at(request.slot) != 0) {
      throw std::invalid_argument("Invalid or duplicate motion alias slot");
    }
    assigned.at(request.slot) = 1;
    const auto resolved = find(request.name);
    aliases_.at(request.slot) = {resolved, request.repeating};
    if (resolved) repeat.at(*resolved) = request.repeating;
  }
  clips_.reserve(sources.size());
  raw_repeat_.reserve(sources.size());
  for (std::size_t index = 0; index < sources.size(); ++index) {
    const auto& source = sources[index].definition.get();
    constexpr std::size_t kRepeatOffset = 16, kWordBytes = 4;
    const auto repeat_word = source.header().subspan(kRepeatOffset, kWordBytes);
    raw_repeat_.push_back(std::ranges::any_of(
        repeat_word, [](std::uint8_t byte) { return byte != 0; }));
    clips_.push_back(
        PreparedClip::prepare(source, skeleton, domain, repeat.at(index)));
    if (progress) progress();
  }
}
std::optional<std::size_t> PreparedClipCatalog::find(
    std::string_view name) const {
  const auto found = std::ranges::lower_bound(
      name_order_, name, {},
      [&](std::size_t index) { return std::string_view(names_.at(index)); });
  if (found == name_order_.end() || names_.at(*found) != name) {
    return std::nullopt;
  }
  return *found;
}
}  // namespace bw::animation
