// Catalog memory migration: encoded bytes, decoded nested strings/vectors,
// traversal scratch and indexes retain the injected allocator owner. Native
// ingress moves its buffer; legacy Bytes conversion copies. Copies retain the
// source domain. Failure unwinds owned storage without allocation fallback.
// R.20 (#rr-owner) preserves ownership; SL.con.3 (#rsl-bounds) preserves
// checked traversal. MEM.6
// (guidelines/memory/MEM.6-inject-allocators-behind-one-interface.md) injects
// allocation; MEM.8 (guidelines/memory/MEM.8-pmr-allocator-aware-containers.md)
// requires explicit nested propagation and owner lifetime. Design and
// implementation queried separately. See resource-memory-migration.md.
// A1: decoded resource catalog owner (RAT). parse owns bytes and validates
// bounds, namespace membership and unique full names. Throws Error; borrowed
// metadata lasts until owner mutation/move/destruction. Full names are
// constructed only here. Algorithm: validate sections, walk namespaces, resolve
// dependency names. Cost: O(bytes + metadata), tree checks O(N log N).
// Optimization: one decode. Design: cpp-guidelines I.1 (#ri-explicit), C.122
// (#rh-separation): explicit ownership and injected file capability.
// cpp-performance COPY.2
// (guidelines/copy-move/COPY.2-mark-move-constructors-noexcept.md): container
// owners retain implicit moves. Implementation: SL.con.3 (#rsl-bounds) checks
// slices before access; COPY.4 (guidelines/copy-move/
// COPY.4-rule-of-five-rule-of-zero.md) keeps owning values rule-of-zero.

// Naming design/implementation consultation: cpp-guidelines P.3 (#rp-what)
// selects role-based names. cpp-performance COPY.4 (guidelines/copy-move/
// COPY.4-rule-of-five-rule-of-zero.md) preserves owning values and implicit
// moves; renaming adds no wrapper, allocation or payload copy.

#include "bw/formats/resource_catalog.hpp"

#include <filesystem>
#include <map>
#include <set>

#include "reader.hpp"
namespace bw::resources {
using detail::kByteOrderOffset;
using detail::kNoIndex;
using detail::kWordBytes;
using detail::malformed;
using detail::Reader;
using detail::unsupported;
namespace {
constexpr std::uint64_t kCatalogHeaderBytes = 64;
constexpr std::uint64_t kCatalogSectionsOffset = 16;
constexpr std::uint64_t kCatalogSectionBytes = 8;
constexpr std::uint64_t kCatalogResourceBytes = 20;
constexpr std::uint64_t kCatalogFileBytes = 8;
constexpr std::uint64_t kCatalogNodeHeaderBytes = 16;
constexpr std::uint32_t kNoDependencies = 0x80000000U;
memory::String relative_file(memory::String directory,
                             memory::String filename) {
  for (auto* part : {&directory, &filename}) {
    std::replace(part->begin(), part->end(), '\\', '/');
    if (part->starts_with('/') || part->find(':') != std::string::npos) {
      malformed("Absolute catalog file path");
    }
  }
  if (filename.empty()) {
    malformed("Empty catalog filename");
  }
  auto joined = directory.empty() ? filename : directory + "/" + filename;
  std::filesystem::path path(joined);
  for (const auto& part : path) {
    if (part == "..") {
      malformed("Parent traversal in catalog file path");
    }
  }
  auto result = path.lexically_normal().generic_string();
  if (result.empty() || result == "." || result.ends_with('/')) {
    malformed("Invalid catalog filename");
  }
  return memory::String(result, directory.get_allocator());
}
void component(std::string_view name) {
  if (name.empty() || name.find('.') != std::string::npos) {
    unsupported("Empty or dotted namespace/leaf component");
  }
}
}  // namespace
ResourceCatalogData ResourceCatalogData::parse(Bytes bytes) {
  return parse(CatalogBytes(bytes.begin(), bytes.end()));
}
ResourceCatalogData ResourceCatalogData::parse(CatalogBytes bytes) {
  const auto allocator = bytes.get_allocator();
  ResourceCatalogData out(allocator);
  out.bytes_ = std::move(bytes);
  Reader r{out.bytes_};
  r.range(0, kCatalogHeaderBytes);
  if (r.tag_view(0) != "RAT ") {
    malformed("Not a RAT catalog");
  }
  if (r.u32(4) != r.b.size()) {
    malformed("RAT declared size mismatch");
  }
  if (r.u32(kByteOrderOffset) || r.u32(3 * kWordBytes)) {
    unsupported("Nonzero RAT reserved header fields");
  }
  using StringSet =
      std::set<memory::String, std::less<>, memory::Allocator<memory::String>>;
  using IndexSet =
      std::set<std::uint32_t, std::less<>, memory::Allocator<std::uint32_t>>;
  StringSet names(allocator);
  memory::Vector<Range> occupied(allocator);
  for (std::size_t i = 0; i < out.sections_.size(); ++i) {
    auto section = Range{
        r.u32(kCatalogSectionsOffset + i * kCatalogSectionBytes),
        r.u32(kCatalogSectionsOffset + kWordBytes + i * kCatalogSectionBytes)};
    r.range(section.offset, section.size);
    if (section.size) {
      if (section.offset < kCatalogHeaderBytes) {
        malformed("RAT section overlaps header");
      }
      occupied.push_back(section);
    }
    out.sections_.at(i) = section;
  }
  std::sort(occupied.begin(), occupied.end(),
            [](auto a, auto b) { return a.offset < b.offset; });
  for (std::size_t i = 1; i < occupied.size(); ++i) {
    if (occupied[i].offset < occupied[i - 1].offset + occupied[i - 1].size) {
      malformed("Overlapping RAT sections");
    }
  }
  const auto [tree, reserved, resources, deps, files, strings] = out.sections_;
  if (reserved.size) {
    unsupported("Nonempty RAT reserved section");
  }
  if (resources.size % kCatalogResourceBytes ||
      files.size % kCatalogFileBytes || tree.size % 4 || deps.size % 4) {
    malformed("RAT table stride mismatch");
  }
  for (auto p = files.offset; p < files.offset + files.size;
       p += kCatalogFileBytes) {
    CatalogFile f(allocator);
    if (r.u32(p) != 0) f.directory = r.text_view(strings, r.u32(p));
    f.filename = r.text_view(strings, r.u32(p + 4));
    f.relative_path = relative_file(f.directory, f.filename);
    out.files_.push_back(std::move(f));
  }
  for (auto p = resources.offset; p < resources.offset + resources.size;
       p += kCatalogResourceBytes) {
    CatalogResource resource(allocator);
    for (std::size_t i = 0; i < resource.raw.size(); ++i) {
      resource.raw.at(i) = r.u32(p + i * 4);
    }
    resource.name = r.text_view(strings, resource.raw[0]);
    component(resource.name);
    if (resource.raw[2] != 0) {
      resource.extra.emplace(r.text_view(strings, resource.raw[2]), allocator);
    }
    if (resource.raw[3] >= out.files_.size()) {
      malformed("RAT file index out of bounds");
    }
    out.resources_.push_back(std::move(resource));
  }
  struct Pending {
    std::uint32_t offset, parent;
    memory::String prefix;
  };
  memory::Vector<Pending> pending(allocator);
  pending.push_back({0, kNoIndex, memory::String(allocator)});
  IndexSet visited(allocator);
  memory::Vector<bool> membership(out.resources_.size(), false, allocator);
  std::map<std::uint64_t, std::uint64_t, std::less<>,
           memory::Allocator<std::pair<const std::uint64_t, std::uint64_t>>>
      node_ranges(allocator);
  // Iterative traversal avoids dependence on the machine call stack for nested
  // input.
  while (!pending.empty()) {
    auto item = std::move(pending.back());
    pending.pop_back();
    if (!visited.insert(item.offset).second) {
      malformed("Repeated or cyclic RAT namespace");
    }
    if (item.offset % 4) {
      malformed("Unaligned RAT namespace");
    }
    r.within(tree, item.offset, kCatalogNodeHeaderBytes);
    auto p = tree.offset + item.offset;
    auto nc = r.u32(p + 2 * kWordBytes), nr = r.u32(p + 3 * kWordBytes);
    auto length =
        kCatalogNodeHeaderBytes + (static_cast<std::uint64_t>(nc) + nr) * 4;
    r.within(tree, item.offset, length);
    auto next = node_ranges.lower_bound(item.offset);
    if ((next != node_ranges.end() && item.offset + length > next->first) ||
        (next != node_ranges.begin() &&
         std::prev(next)->second > item.offset)) {
      malformed("Overlapping RAT namespace nodes");
    }
    node_ranges.emplace(item.offset, item.offset + length);
    NamespaceNode node(allocator);
    node.offset = item.offset;
    node.parent = r.u32(p + 4);
    node.name = r.text_view(strings, r.u32(p));
    if (node.parent != item.parent) {
      malformed("RAT namespace parent mismatch");
    }
    if (item.offset == 0) {
      if (!node.name.empty()) {
        unsupported("Named RAT root namespace");
      }
    } else {
      component(node.name);
      item.prefix += node.name + ".";
    }
    IndexSet children(allocator);
    StringSet child_names(allocator);
    for (std::uint64_t i = 0; i < nc; ++i) {
      auto child = r.u32(p + kCatalogNodeHeaderBytes + i * 4);
      r.within(tree, child, kCatalogNodeHeaderBytes);
      auto child_name = memory::String(
          r.text_view(strings, r.u32(tree.offset + child)), allocator);
      if (!children.insert(child).second ||
          !child_names.insert(child_name).second) {
        malformed("Duplicate RAT child namespace");
      }
      node.children.push_back(child);
      pending.push_back({child, item.offset, item.prefix});
    }
    for (std::uint64_t i = 0; i < nr; ++i) {
      auto index = r.u32(p + kCatalogNodeHeaderBytes +
                         (static_cast<std::uint64_t>(nc) + i) * 4);
      if (index >= membership.size() || membership[index]) {
        malformed("Invalid or duplicate RAT resource membership");
      }
      membership[index] = true;
      auto& resource = out.resources_[index];
      if (resource.raw[1] != item.offset) {
        malformed("RAT resource parent mismatch");
      }
      resource.full_name = item.prefix + resource.name;
      if (!names.insert(resource.full_name).second) {
        malformed("Duplicate RAT resource name");
      }
      node.resources.push_back(index);
    }
    out.namespaces_.push_back(std::move(node));
  }
  if (std::find(membership.begin(), membership.end(), false) !=
      membership.end()) {
    malformed("Unreachable RAT resource");
  }
  for (auto& resource : out.resources_) {
    auto offset = resource.raw[4];
    if (offset & kNoDependencies) {
      continue;
    }
    if (offset % 4) {
      malformed("Unaligned RAT dependency list");
    }
    r.within(deps, offset, 4);
    auto count = r.u32(deps.offset + offset);
    r.within(deps, offset,
             2 * kWordBytes + static_cast<std::uint64_t>(count) * 4);
    auto p = deps.offset + offset + 4;
    for (std::uint64_t i = 0; i < count; ++i) {
      auto index = r.u32(p + i * 4);
      if (index >= out.resources_.size()) {
        malformed("RAT dependency index out of bounds");
      }
      resource.dependencies.emplace_back(index, out.resources_[index].full_name,
                                         allocator);
    }
    p += static_cast<std::uint64_t>(count) * 4;
    auto named_count = r.u32(p);
    r.within(deps, p + 4 - deps.offset,
             static_cast<std::uint64_t>(named_count) * 4);
    for (std::uint64_t i = 0; i < named_count; ++i) {
      resource.dependencies.emplace_back(
          std::nullopt, r.text_view(strings, r.u32(p + 4 + i * 4)), allocator);
    }
  }
  return out;
}
}  // namespace bw::resources
