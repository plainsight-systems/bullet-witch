// Validate and index immutable customization nodes before consumer-specific
// settings decoding. The header defines ownership/cost/failure contracts.
// Both MCP phases: R.2 (#rr-use-ptr) bounds byte/path views; MEM.6
// (guidelines/memory/MEM.6-inject-allocators-behind-one-interface.md) routes
// traversal and retained metadata through the injected domain.
#include "bw/formats/customization.hpp"

#include <algorithm>
#include <limits>

#include "reader.hpp"
namespace bw::formats {
namespace {
using resources::detail::malformed;
using resources::detail::Reader;
constexpr std::uint32_t kAbsent = std::numeric_limits<std::uint32_t>::max();
constexpr std::uint64_t kHeaderBytes = 32;
constexpr std::uint64_t kNameBaseField = 12, kNodeBaseField = 16;
constexpr std::uint64_t kNodeHeaderBytes = 16, kNameField = 8, kWordBytes = 4;
struct Pending {
  std::uint32_t offset{}, parent{};
};
}  // namespace
CustomizationDefinition CustomizationDefinition::parse(
    std::span<const std::uint8_t> bytes,
    const std::shared_ptr<memory::Domain>& domain) {
  const Reader reader{bytes};
  reader.range(0, kHeaderBytes);
  constexpr std::uint32_t kSignature = 0x4d545343;
  constexpr std::uint32_t kEndian = 0x04030201;
  if (reader.u32(0) != kSignature || reader.u32(kWordBytes) != kEndian ||
      reader.u32(kNameField) != 0) {
    resources::detail::unsupported("Unsupported bytes header");
  }
  const auto names = reader.u32(kNameBaseField);
  const auto nodes = reader.u32(kNodeBaseField);
  if (names < kHeaderBytes || names >= nodes) {
    malformed("Invalid bytes names region");
  }
  reader.range(nodes, kNodeHeaderBytes);
  CustomizationDefinition result(domain);
  auto& records = result.nodes_;
  memory::Vector<Pending> pending{memory::Allocator<Pending>(domain)};
  memory::Vector<std::uint8_t> visited(bytes.size(), 0,
                                       memory::Allocator<std::uint8_t>(domain));
  pending.push_back({0, kAbsent});
  // Visit each linked record once; repeated targets are invalid tree ownership.
  while (!pending.empty()) {
    const auto current = pending.back();
    pending.pop_back();
    const std::uint64_t offset = nodes + std::uint64_t{current.offset};
    reader.range(offset, kNodeHeaderBytes);
    if (visited.at(static_cast<std::size_t>(offset)) != 0) {
      malformed("Cyclic or shared bytes node");
    }
    visited.at(static_cast<std::size_t>(offset)) = 1;
    const auto name = reader.text_view({names, nodes - names},
                                       reader.u32(offset + kNameField));
    if (name.empty() || name.find('/') != std::string_view::npos) {
      malformed("Invalid bytes path component");
    }
    records.push_back({current.offset, current.parent, name, {}});
    const auto sibling = reader.u32(offset);
    const auto child = reader.u32(offset + kWordBytes);
    if (sibling != kAbsent) {
      pending.push_back({sibling, current.parent});
    }
    if (child != kAbsent) {
      pending.push_back({child, current.offset});
    }
  }
  // Bound each inline payload by the next actual node header.
  std::sort(records.begin(), records.end(),
            [](const Node& left, const Node& right) {
              return left.offset < right.offset;
            });
  for (std::size_t i = 0; i < records.size(); ++i) {
    auto& record = records.at(i);
    const std::uint64_t payload =
        nodes + std::uint64_t{record.offset} + kNodeHeaderBytes;
    const std::uint64_t end =
        i + 1 == records.size()
            ? bytes.size()
            : nodes + std::uint64_t{records.at(i + 1).offset};
    if (end < payload) {
      malformed("Overlapping customization node headers");
    }
    record.payload = bytes.subspan(static_cast<std::size_t>(payload),
                                   static_cast<std::size_t>(end - payload));
  }
  return result;
}
std::span<const std::uint8_t> CustomizationDefinition::lookup(
    std::span<const std::string_view> components) const {
  if (components.empty()) {
    malformed("Empty customization path");
  }
  std::uint32_t parent = kAbsent;
  const Node* selected = nullptr;
  for (const auto component : components) {
    selected = nullptr;
    for (const auto& node : nodes_) {
      if (node.parent == parent && node.name == component) {
        if (selected != nullptr) {
          malformed("Ambiguous customization path");
        }
        selected = &node;
      }
    }
    if (selected == nullptr) {
      malformed("Missing customization path");
    }
    parent = selected->offset;
  }
  if (selected == nullptr) {
    malformed("Missing customization path result");
  }
  return selected->payload;
}
}  // namespace bw::formats
