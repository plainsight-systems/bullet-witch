// Immutable customization tree used by separate stage and actor settings
// decoders. The byte owner must outlive this view and every returned payload.
// Parsing validates links/names, rejects repeated nodes and overlapping node
// headers, then orders node ranges; lookup resolves exact path components and
// rejects absent/ambiguous paths. Malformed input throws resources::Error.
// Metadata uses the injected domain; parse is O(bytes + nodes log nodes),
// lookup O(path components * nodes), with no payload copies or lookup
// allocations. Independent immutable reads need no synchronization. Both MCP
// phases: R.2
// (#rr-use-ptr) bounds borrowed sequences; MEM.6
// (guidelines/memory/MEM.6-inject-allocators-behind-one-interface.md) supplies
// metadata storage. See player-camera-initialization.md and stage environment.
#pragma once
#include "bw/formats/common.hpp"
#include "bw/memory/allocator.hpp"

namespace bw::formats {
class CustomizationDefinition {
 public:
  static CustomizationDefinition parse(
      std::span<const std::uint8_t> bytes,
      const std::shared_ptr<memory::Domain>& domain);
  std::span<const std::uint8_t> lookup(
      std::span<const std::string_view> components) const;

 private:
  struct Node {
    std::uint32_t offset{}, parent{};
    std::string_view name;
    std::span<const std::uint8_t> payload;
  };
  explicit CustomizationDefinition(
      const std::shared_ptr<memory::Domain>& domain)
      : nodes_(memory::Allocator<Node>(domain)) {}
  memory::Vector<Node> nodes_;
};
}  // namespace bw::formats
