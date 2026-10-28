// Decode original indexed navigation chunks into immutable owning values.
// Every nested range is checked relative to its parent before reading; opaque
// work buffers are ignored, never treated as serialized pointers. Setup is
// linear in records/points apart from per-group duplicate path-ID checks;
// publishes nothing on failure. Core SL.con.3
// (#rsl-bounds) governs checked spans; MEM.9 (guidelines/memory/
// MEM.9-embedded-allocate-at-init.md) keeps all allocations in admission.
#include "bw/content/formats/navigation_archive.hpp"

#include <algorithm>
#include <bit>
#include <cmath>

#include "bw/content/formats/binary_reader.hpp"

namespace bw::formats {
namespace {
using resources::detail::malformed;
using resources::detail::Reader;
using Bytes = std::span<const std::uint8_t>;
constexpr std::size_t kHeader = 8, kWord = 4, kDirectoryRecord = 16;
constexpr std::size_t kPointBytes = 12, kEdgeBytes = 16;
Bytes payload(Bytes bytes, std::string_view tag) {
  const Reader r{bytes};
  if (r.tag_view(0) != tag || r.u32(kWord) > bytes.size() - kHeader) {
    malformed("Invalid navigation chunk");
  }
  return bytes.subspan(kHeader, r.u32(kWord));
}
Bytes section(Bytes bytes, std::string_view wanted) {
  const Reader r{bytes};
  Bytes result;
  for (std::size_t offset = 0; offset < bytes.size();) {
    r.range(offset, kHeader);
    const auto size = r.u32(offset + kWord);
    r.range(offset + kHeader, size);
    if (r.tag_view(offset) == wanted) {
      if (!result.empty()) malformed("Duplicate navigation section");
      result = bytes.subspan(offset, kHeader + size);
    }
    offset += kHeader + size;
  }
  if (result.empty()) malformed("Missing navigation section");
  return payload(result, wanted);
}
Bytes indexed(Bytes data, std::uint32_t offset) {
  const Reader r{data};
  r.range(offset, kHeader);
  const auto size = r.u32(static_cast<std::size_t>(offset) + kWord);
  r.range(static_cast<std::size_t>(offset) + kHeader, size);
  return data.subspan(offset, kHeader + size);
}
NavigationPoint point(Bytes bytes, std::size_t offset) {
  const Reader r{bytes};
  NavigationPoint result{};
  for (std::size_t axis = 0; axis < result.size(); ++axis) {
    result.at(axis) = std::bit_cast<float>(r.u32(offset + axis * kWord));
    if (!std::isfinite(result.at(axis))) {
      malformed("Nonfinite navigation point");
    }
  }
  return result;
}
struct Directory {
  Bytes refs, data;
  explicit Directory(Bytes bytes)
      : refs(section(bytes, "refd")), data(section(bytes, "data")) {
    if (refs.size() % kWord != 0) malformed("Invalid navigation index size");
  }
  std::size_t size() const { return refs.size() / kWord; }
  Bytes record(std::size_t index) const {
    return indexed(data, Reader{refs}.u32(index * kWord));
  }
};
}  // namespace

NavigationArchive::NavigationArchive(
    Bytes bytes, const std::shared_ptr<memory::Domain>& domain)
    : groups_(memory::Allocator<NavigationGroup>(domain)),
      names_(memory::Allocator<char>(domain)),
      name_offsets_(memory::Allocator<std::size_t>(domain)),
      paths_(memory::Allocator<Path>(domain)),
      points_(memory::Allocator<NavigationPoint>(domain)),
      vertices_(memory::Allocator<NavigationVertex>(domain)),
      edges_(memory::Allocator<NavigationEdge>(domain)),
      adjacency_(memory::Allocator<Adjacency>(domain)),
      neighbors_(memory::Allocator<std::uint32_t>(domain)) {
  if (bytes.empty()) return;
  const auto root = payload(bytes, "arcs");
  const auto refs = section(root, "refs"), data = section(root, "data");
  const auto strings = section(root, "strs");
  names_.assign(strings.begin(), strings.end());
  if (refs.size() % kDirectoryRecord != 0) {
    malformed("Invalid navigation group directory");
  }
  const Reader r{refs};
  groups_.reserve(refs.size() / kDirectoryRecord);
  for (std::size_t row = 0; row < refs.size(); row += kDirectoryRecord) {
    const auto name = r.u32(row + 2 * kWord);
    if (name >= strings.size() ||
        std::find(strings.begin() + name, strings.end(), 0) == strings.end()) {
      malformed("Unterminated navigation group name");
    }
    name_offsets_.push_back(name);
    const auto record = indexed(data, r.u32(row));
    if (record.size() != r.u32(row + kWord)) {
      malformed("Navigation group size disagrees with directory");
    }
    NavigationGroup group;
    const Reader group_reader{record};
    if (group_reader.tag_view(0) == "ptrl") {
      group.kind = NavigationGroup::Kind::patrol;
      group.first = paths_.size();
      const Directory paths{section(payload(record, "ptrl"), "patr")};
      group.count = paths.size();
      for (std::size_t index = 0; index < paths.size(); ++index) {
        const auto raw = payload(paths.record(index), "patr");
        const Reader path_reader{raw};
        const auto id = std::bit_cast<std::int32_t>(path_reader.u32(0));
        const auto count = path_reader.u32(kWord);
        if (raw.size() != kHeader + std::uint64_t{count} * kPointBytes) {
          malformed("Invalid navigation path extent");
        }
        const auto previous =
            std::span<const Path>(paths_).subspan(group.first);
        if (std::ranges::any_of(
                previous, [id](const Path& path) { return path.id == id; })) {
          malformed("Duplicate navigation path ID");
        }
        paths_.push_back({id, points_.size(), count});
        for (std::size_t p = 0; p < count; ++p) {
          points_.push_back(point(raw, kHeader + p * kPointBytes));
        }
      }
    } else if (group_reader.tag_view(0) == "scrh") {
      group.kind = NavigationGroup::Kind::search;
      group.first = vertices_.size();
      group.first_edge = edges_.size();
      const auto search = payload(record, "scrh");
      const Directory vertices{section(search, "pint")};
      group.count = vertices.size();
      for (std::size_t index = 0; index < vertices.size(); ++index) {
        const auto vertex = payload(vertices.record(index), "vect");
        const auto info = section(vertex, "info");
        if (info.size() != kDirectoryRecord) {
          malformed("Invalid navigation vertex info");
        }
        vertices_.push_back({point(info, 0), Reader{info}.u32(kPointBytes)});
      }
      const auto edges = section(search, "pass");
      if (edges.size() % kEdgeBytes != 0) malformed("Invalid navigation edges");
      group.edge_count = edges.size() / kEdgeBytes;
      const Reader edge_reader{edges};
      for (std::size_t offset = 0; offset < edges.size();
           offset += kEdgeBytes) {
        NavigationEdge edge{edge_reader.u32(offset),
                            edge_reader.u32(offset + kWord),
                            edge_reader.u32(offset + 2 * kWord),
                            edge_reader.u32(offset + 3 * kWord)};
        if (edge.first >= group.count || edge.second >= group.count) {
          malformed("Navigation edge endpoint exceeds graph");
        }
        edges_.push_back(edge);
      }
    } else {
      malformed("Unknown navigation group kind");
    }
    groups_.push_back(group);
  }
  // Original links name both endpoint indices; publish one shared CSR graph
  // rather than scanning the edge table or copying geometry in each actor.
  adjacency_.resize(vertices_.size());
  for (const auto& group : groups_) {
    if (group.kind != NavigationGroup::Kind::search) continue;
    const auto links = std::span<const NavigationEdge>(edges_).subspan(
        group.first_edge, group.edge_count);
    for (const auto& edge : links) {
      ++adjacency_.at(group.first + edge.first).count;
      ++adjacency_.at(group.first + edge.second).count;
    }
    for (std::size_t index = 0; index < group.count; ++index) {
      auto& adjacency = adjacency_.at(group.first + index);
      adjacency.first = neighbors_.size();
      neighbors_.resize(neighbors_.size() + adjacency.count);
      adjacency.count = 0;
    }
    for (const auto& edge : links) {
      auto& first = adjacency_.at(group.first + edge.first);
      auto& second = adjacency_.at(group.first + edge.second);
      neighbors_.at(first.first + first.count++) = edge.second;
      neighbors_.at(second.first + second.count++) = edge.first;
    }
  }
}

std::optional<std::size_t> NavigationArchive::find_group(
    std::string_view name) const {
  for (std::size_t index = 0; index < name_offsets_.size(); ++index) {
    const auto* first = std::to_address(
        std::span(names_).subspan(name_offsets_[index]).begin());
    if (std::string_view(first) == name) return index;
  }
  return std::nullopt;
}

std::span<const NavigationPoint> NavigationArchive::patrol(
    std::size_t group, std::int32_t path) const {
  if (group >= groups_.size() ||
      groups_.at(group).kind != NavigationGroup::Kind::patrol) {
    return {};
  }
  const auto& value = groups_.at(group);
  for (const auto& entry :
       std::span<const Path>(paths_).subspan(value.first, value.count)) {
    if (entry.id == path) {
      return std::span<const NavigationPoint>(points_).subspan(entry.first,
                                                               entry.count);
    }
  }
  return {};
}
std::span<const NavigationVertex> NavigationArchive::vertices(
    std::size_t group) const {
  if (group >= groups_.size() ||
      groups_.at(group).kind != NavigationGroup::Kind::search) {
    return {};
  }
  const auto& value = groups_.at(group);
  return std::span<const NavigationVertex>(vertices_).subspan(value.first,
                                                              value.count);
}
std::span<const NavigationEdge> NavigationArchive::edges(
    std::size_t group) const {
  if (group >= groups_.size() ||
      groups_.at(group).kind != NavigationGroup::Kind::search) {
    return {};
  }
  const auto& value = groups_.at(group);
  return std::span<const NavigationEdge>(edges_).subspan(value.first_edge,
                                                         value.edge_count);
}
std::span<const std::uint32_t> NavigationArchive::neighbors(
    std::size_t group, std::size_t vertex) const {
  if (group >= groups_.size() ||
      groups_.at(group).kind != NavigationGroup::Kind::search ||
      vertex >= groups_.at(group).count) {
    return {};
  }
  const auto& entry = adjacency_.at(groups_.at(group).first + vertex);
  return std::span<const std::uint32_t>(neighbors_)
      .subspan(entry.first, entry.count);
}
}  // namespace bw::formats
