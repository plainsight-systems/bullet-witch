// Resource decoding, catalog lookup and filesystem boundary. Current combined
// A1/A2/A3/P1 mapping is described in docs/architecture/file-mapping.md
// (draft); docs/resources.md defines the implemented contract. No handler is
// executed.
//
// Contract: public ownership/lifetime rules are in bw/resources.hpp. All disk
// offsets are widened and checked by subtraction before reading/subspanning.
// Unknown tags remain visible; unsupported compression and invalid references
// fail explicitly. Filesystem reads do not modify input files. Path containment
// is checked when archive_path runs; it is not a race-free filesystem sandbox.
//
// Algorithm: (1) own bytes and validate the signature/header; (2) derive and
// bound each section; (3) decode CPR argument triples, records and selected
// payload ranges without native pointer relocation; or (4) walk RAT namespaces
// iteratively, reject overlaps/cycles/duplicate membership, build full names,
// then decode dependencies; (5) resolve names by priority and check archive
// path containment before a caller opens the selected file. Costs: B input
// bytes retained; metadata/strings/index allocated at parse time. String scans
// and copied dependency/argument metadata scale with their total decoded size,
// which can exceed B for shared references. Tree/index insertion adds
// logarithmic factors in node/resource count. Resolution visits M mounted
// catalogs with ordered-map lookup; no payload contents are copied on lookup.
// Optimization: build full-name indices once; borrow validated payload spans.
// Derived costs only; no measured speedup is asserted.
//
// Retrieved design rules: cpp-guidelines I.1 (#ri-explicit), I.30
// (#ri-encapsulate) for explicit errors and the encapsulated byte-I/O boundary;
// cpp-performance COPY.2 (guidelines/copy-move/
// COPY.2-mark-move-constructors-noexcept.md) for rule-of-zero ownership.
// Implementation rules: cpp-guidelines SL.con.3 (#rsl-bounds), checked slices
// and array access; cpp-performance CDSA.32 (guidelines/cpu-dsa/
// CDSA.32-transform-static-data-into-the-consumer-layout.md), once-built
// metadata and catalog index retained alongside original bytes for
// verification.

#include "bw/resources.hpp"

#include <algorithm>
#include <fstream>
#include <limits>
#include <set>
#include <sstream>

namespace bw::resources {
namespace {
constexpr unsigned kByteBits = 8;
constexpr std::uint64_t kWordBytes = 4;
constexpr std::uint64_t kCprHeaderBytes = 0x60;
constexpr std::uint64_t kByteOrderOffset = 8;
constexpr std::uint32_t kLittleEndianMarker = 0x11223344;
constexpr std::uint64_t kCprCompressionOffset = 0x28;
constexpr std::uint64_t kCprSecondCompressionOffset = 0x30;
constexpr std::uint64_t kCprMetadataSizeOffset = 0x2c;
constexpr std::uint64_t kCprSectionsOffset = 0x10;
constexpr std::uint64_t kCprRecordBytes = 48;
constexpr std::uint64_t kCprArgumentBytes = 12;
constexpr std::size_t kFirstOffsetField = 7;
constexpr std::size_t kFirstSizeField = 8;
constexpr std::size_t kSecondOffsetField = 9;
constexpr std::size_t kSecondSizeField = 10;
constexpr std::uint32_t kNoIndex = 0xffffffff;
constexpr std::uint64_t kCprPointerBase = 0x28;
constexpr std::uint64_t kRatHeaderBytes = 64;
constexpr std::uint64_t kRatSectionsOffset = 16;
constexpr std::uint64_t kRatSectionBytes = 8;
constexpr std::uint64_t kRatResourceBytes = 20;
constexpr std::uint64_t kRatFileBytes = 8;
constexpr std::uint64_t kRatNodeHeaderBytes = 16;
constexpr std::uint32_t kNoDependencies = 0x80000000U;
[[noreturn]] void malformed(const std::string& message) {
  throw Error(ErrorCode::malformed, message);
}
[[noreturn]] void unsupported(const std::string& message) {
  throw Error(ErrorCode::unsupported, message);
}
struct Reader {
  std::span<const std::uint8_t> b;
  void range(std::uint64_t offset, std::uint64_t size) const {
    if (offset > b.size() || size > b.size() - offset) {
      malformed("Range exceeds file at offset " + std::to_string(offset));
    }
  }
  std::uint32_t u32(std::uint64_t offset) const {
    range(offset, 4);
    auto p = b.subspan(static_cast<std::size_t>(offset), sizeof(std::uint32_t));
    return static_cast<std::uint32_t>(p[0]) |
           (static_cast<std::uint32_t>(p[1]) << kByteBits) |
           (static_cast<std::uint32_t>(p[2]) << (2 * kByteBits)) |
           (static_cast<std::uint32_t>(p[3]) << (3 * kByteBits));
  }
  std::string tag(std::uint64_t offset) const {
    range(offset, kWordBytes);
    const auto value = b.subspan(static_cast<std::size_t>(offset), kWordBytes);
    return {value.begin(), value.end()};
  }
  std::string text(Range pool, std::uint32_t offset) const {
    range(pool.offset, pool.size);
    if (offset >= pool.size) {
      malformed("String offset exceeds pool");
    }
    auto first = b.begin() + static_cast<std::ptrdiff_t>(pool.offset + offset);
    auto last =
        b.begin() + static_cast<std::ptrdiff_t>(pool.offset + pool.size);
    auto stop = std::find(first, last, 0);
    if (stop == last) {
      malformed("Unterminated pool string");
    }
    return {first, stop};
  }
  std::optional<std::string> optional_text(Range pool,
                                           std::uint32_t offset) const {
    return offset ? std::optional(text(pool, offset)) : std::nullopt;
  }
  void within(Range section, std::uint64_t offset, std::uint64_t size) const {
    if (offset > section.size || size > section.size - offset) {
      malformed("Range exceeds section at offset " +
                std::to_string(section.offset + offset));
    }
    range(section.offset + offset, size);
  }
};
std::string relative_file(std::string directory, std::string filename) {
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
  return result;
}
void component(const std::string& name) {
  if (name.empty() || name.find('.') != std::string::npos) {
    unsupported("Empty or dotted namespace/leaf component");
  }
}
}  // namespace
std::string_view error_name(ErrorCode code) {
  switch (code) {
    case ErrorCode::io:
      return "io";
    case ErrorCode::malformed:
      return "malformed";
    case ErrorCode::unsupported:
      return "unsupported";
    case ErrorCode::not_found:
      return "not_found";
    case ErrorCode::ambiguous:
      return "ambiguous";
  }
  return "unknown";
}
Bytes read_file(const std::filesystem::path& path) {
  std::ifstream input(path, std::ios::binary | std::ios::ate);
  if (!input) {
    throw Error(ErrorCode::io, "Cannot open " + path.string());
  }
  const auto end = input.tellg();
  if (end < 0 ||
      static_cast<std::uint64_t>(end) >
          std::numeric_limits<std::size_t>::max() ||
      static_cast<std::uint64_t>(end) >
          static_cast<std::uint64_t>(
              std::numeric_limits<std::streamsize>::max())) {
    throw Error(ErrorCode::io, "Invalid file size: " + path.string());
  }
  Bytes result(static_cast<std::size_t>(end));
  input.seekg(0);
  // Character access may alias any object representation; the read below is
  // bounded by the already-sized vector. No typed-object/alignment cast occurs.
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
  auto* destination = reinterpret_cast<char*>(result.data());
  if (!result.empty() &&
      !input.read(destination, static_cast<std::streamsize>(result.size()))) {
    throw Error(ErrorCode::io, "Incomplete file read: " + path.string());
  }
  return result;
}
std::string normalize_name(std::string_view name) {
  std::string result(name);
  for (char& ch : result) {
    if (ch >= 'A' && ch <= 'Z') {
      ch = static_cast<char>(ch + ('a' - 'A'));
    }
  }
  return result;
}
CprArchive CprArchive::open(const std::filesystem::path& path) {
  return parse(read_file(path));
}
CprArchive CprArchive::parse(Bytes bytes) {
  CprArchive out;
  out.bytes_ = std::move(bytes);
  Reader r{out.bytes_};
  r.range(0, kCprHeaderBytes);
  if (r.tag(0) != "CPR1") {
    malformed("Not a CPR1 archive");
  }
  for (std::size_t i = 0; i < out.header_.size(); ++i) {
    out.header_.at(i) = r.u32(4 + i * 4);
  }
  if (r.u32(4) != 1) {
    unsupported("CPR version is not 1");
  }
  if (r.u32(kByteOrderOffset) != kLittleEndianMarker) {
    unsupported("CPR byte order is not little-endian");
  }
  if (r.u32(kCprCompressionOffset) || r.u32(kCprSecondCompressionOffset)) {
    unsupported("Compressed CPR sections are not implemented");
  }
  std::uint64_t cursor = kCprHeaderBytes;
  for (std::size_t i = 0; i < out.sections_.size(); ++i) {
    out.sections_.at(i) = {cursor, r.u32(kCprSectionsOffset + i * 4)};
    r.range(cursor, out.sections_.at(i).size);
    cursor += out.sections_.at(i).size;
  }
  const auto [table, args, names, gap, first, second] = out.sections_;
  if (table.size % kCprRecordBytes) {
    malformed("CPR record table stride mismatch");
  }
  // 0x41b4a0 uses floor(argument_bytes / 12); trailing alignment bytes are
  // retained.
  if (r.u32(kCprMetadataSizeOffset) !=
      table.size + args.size + names.size + gap.size) {
    malformed("CPR metadata size mismatch");
  }
  // The native loader relocates every complete argument triple, even if no
  // record refers to it.
  for (std::uint64_t i = 0; i < args.size / kCprArgumentBytes; ++i) {
    auto q = args.offset + i * kCprArgumentBytes;
    Argument arg{{r.u32(q), r.u32(q + 4), r.u32(q + 2 * kWordBytes)}, {}, {}};
    arg.name = r.optional_text(names, arg.raw[0]);
    if (arg.name && arg.raw[1] == 2) {
      arg.string_value = r.text(names, arg.raw[2]);
    }
    out.arguments_.push_back(std::move(arg));
  }
  for (std::uint64_t p = table.offset; p < table.offset + table.size;
       p += kCprRecordBytes) {
    CprRecord record;
    record.type = r.tag(p);
    for (std::size_t i = 0; i < record.raw.size(); ++i) {
      record.raw.at(i) = r.u32(p + 4 + i * 4);
    }
    const auto& f = record.raw;
    record.name = r.text(names, f[0]);
    record.publication_name = r.optional_text(names, f[1]);
    if (f[3]) {
      r.within(args, static_cast<std::uint64_t>(f[4]) * kCprArgumentBytes,
               static_cast<std::uint64_t>(f[3]) * kCprArgumentBytes);
      for (std::uint64_t i = 0; i < f[3]; ++i) {
        record.arguments.push_back(
            out.arguments_.at(static_cast<std::size_t>(f[4] + i)));
      }
    }
    if (f[kFirstSizeField]) {
      r.within(first, f[kFirstOffsetField], f[kFirstSizeField]);
      record.first_payload =
          Range{first.offset + f[kFirstOffsetField], f[kFirstSizeField]};
      record.selected_payload = record.first_payload;
    }
    if (f[kSecondSizeField]) {
      r.within(second, f[kSecondOffsetField], f[kSecondSizeField]);
      record.second_payload =
          Range{second.offset + f[kSecondOffsetField], f[kSecondSizeField]};
      if (!record.selected_payload) {
        record.selected_payload = record.second_payload;
      }
    }
    if (f[2] != kNoIndex) {
      record.selected_payload =
          Range{kCprPointerBase + f[2],
                record.selected_payload ? record.selected_payload->size : 0};
      r.range(record.selected_payload->offset, record.selected_payload->size);
    }
    static constexpr std::array known{"USER", "DDS ", "DDSC", "DXVB",
                                      "DXIB", "DXVS", "DXPS", "CALL"};
    record.known_type =
        std::find(known.begin(), known.end(), record.type) != known.end();
    out.records_.push_back(std::move(record));
  }
  return out;
}
std::span<const std::uint8_t> CprArchive::payload(std::size_t index) const {
  const auto& range = records_.at(index).selected_payload;
  if (!range) {
    return {};
  }
  return std::span<const std::uint8_t>(bytes_).subspan(
      static_cast<std::size_t>(range->offset),
      static_cast<std::size_t>(range->size));
}
RatCatalog RatCatalog::open(const std::filesystem::path& path) {
  return parse(read_file(path));
}
RatCatalog RatCatalog::parse(Bytes bytes) {
  RatCatalog out;
  out.bytes_ = std::move(bytes);
  Reader r{out.bytes_};
  r.range(0, kRatHeaderBytes);
  if (r.tag(0) != "RAT ") {
    malformed("Not a RAT catalog");
  }
  if (r.u32(4) != r.b.size()) {
    malformed("RAT declared size mismatch");
  }
  if (r.u32(kByteOrderOffset) || r.u32(3 * kWordBytes)) {
    unsupported("Nonzero RAT reserved header fields");
  }
  std::vector<Range> occupied;
  for (std::size_t i = 0; i < out.sections_.size(); ++i) {
    auto section =
        Range{r.u32(kRatSectionsOffset + i * kRatSectionBytes),
              r.u32(kRatSectionsOffset + kWordBytes + i * kRatSectionBytes)};
    r.range(section.offset, section.size);
    if (section.size) {
      if (section.offset < kRatHeaderBytes) {
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
  if (resources.size % kRatResourceBytes || files.size % kRatFileBytes ||
      tree.size % 4 || deps.size % 4) {
    malformed("RAT table stride mismatch");
  }
  for (auto p = files.offset; p < files.offset + files.size;
       p += kRatFileBytes) {
    CatalogFile f{r.optional_text(strings, r.u32(p)).value_or(""),
                  r.text(strings, r.u32(p + 4)),
                  {}};
    f.relative_path = relative_file(f.directory, f.filename);
    out.files_.push_back(std::move(f));
  }
  for (auto p = resources.offset; p < resources.offset + resources.size;
       p += kRatResourceBytes) {
    CatalogResource resource;
    for (std::size_t i = 0; i < resource.raw.size(); ++i) {
      resource.raw.at(i) = r.u32(p + i * 4);
    }
    resource.name = r.text(strings, resource.raw[0]);
    component(resource.name);
    resource.extra = r.optional_text(strings, resource.raw[2]);
    if (resource.raw[3] >= out.files_.size()) {
      malformed("RAT file index out of bounds");
    }
    out.resources_.push_back(std::move(resource));
  }
  struct Pending {
    std::uint32_t offset, parent;
    std::string prefix;
  };
  std::vector<Pending> pending{{0, kNoIndex, ""}};
  std::set<std::uint32_t> visited;
  std::vector<bool> membership(out.resources_.size());
  std::map<std::uint64_t, std::uint64_t> node_ranges;
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
    r.within(tree, item.offset, kRatNodeHeaderBytes);
    auto p = tree.offset + item.offset;
    auto nc = r.u32(p + 2 * kWordBytes), nr = r.u32(p + 3 * kWordBytes);
    auto length =
        kRatNodeHeaderBytes + (static_cast<std::uint64_t>(nc) + nr) * 4;
    r.within(tree, item.offset, length);
    auto next = node_ranges.lower_bound(item.offset);
    if ((next != node_ranges.end() && item.offset + length > next->first) ||
        (next != node_ranges.begin() &&
         std::prev(next)->second > item.offset)) {
      malformed("Overlapping RAT namespace nodes");
    }
    node_ranges.emplace(item.offset, item.offset + length);
    NamespaceNode node{
        item.offset, r.u32(p + 4), r.text(strings, r.u32(p)), {}, {}};
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
    std::set<std::uint32_t> children;
    std::set<std::string> child_names;
    for (std::uint64_t i = 0; i < nc; ++i) {
      auto child = r.u32(p + kRatNodeHeaderBytes + i * 4);
      r.within(tree, child, kRatNodeHeaderBytes);
      auto child_name = r.text(strings, r.u32(tree.offset + child));
      if (!children.insert(child).second ||
          !child_names.insert(child_name).second) {
        malformed("Duplicate RAT child namespace");
      }
      node.children.push_back(child);
      pending.push_back({child, item.offset, item.prefix});
    }
    for (std::uint64_t i = 0; i < nr; ++i) {
      auto index = r.u32(p + kRatNodeHeaderBytes +
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
      if (!out.index_.emplace(resource.full_name, index).second) {
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
      resource.dependencies.push_back({index, out.resources_[index].full_name});
    }
    p += static_cast<std::uint64_t>(count) * 4;
    auto named_count = r.u32(p);
    r.within(deps, p + 4 - deps.offset,
             static_cast<std::uint64_t>(named_count) * 4);
    for (std::uint64_t i = 0; i < named_count; ++i) {
      resource.dependencies.push_back(
          {std::nullopt, r.text(strings, r.u32(p + 4 + i * 4))});
    }
  }
  return out;
}
std::optional<std::size_t> RatCatalog::find(std::string_view name) const {
  auto it = index_.find(normalize_name(name));
  return it == index_.end() ? std::nullopt : std::optional(it->second);
}
void CatalogSet::mount(int priority, std::filesystem::path path,
                       RatCatalog catalog) {
  catalogs_.push_back({priority, std::move(path), std::move(catalog)});
}
Resolution CatalogSet::resolve(std::string_view name) const {
  std::optional<Resolution> best;
  int priority = 0;
  bool ambiguous = false;
  for (std::size_t i = 0; i < catalogs_.size(); ++i) {
    auto resource = catalogs_[i].catalog.find(name);
    if (!resource) {
      continue;
    }
    if (!best || catalogs_[i].priority < priority) {
      best = Resolution{i, *resource};
      priority = catalogs_[i].priority;
      ambiguous = false;
    } else if (catalogs_[i].priority == priority) {
      ambiguous = true;
    }
  }
  if (!best) {
    throw Error(ErrorCode::not_found,
                "Resource not found: " + std::string(name));
  }
  if (ambiguous) {
    throw Error(ErrorCode::ambiguous,
                "Equal-priority catalog matches: " + std::string(name));
  }
  return *best;
}
std::filesystem::path archive_path(const MountedCatalog& mount,
                                   std::size_t index) {
  auto base = std::filesystem::weakly_canonical(
      std::filesystem::absolute(mount.path).parent_path());
  auto candidate = std::filesystem::weakly_canonical(
      base / mount.catalog.files().at(index).relative_path);
  auto b = base.begin(), c = candidate.begin();
  for (; b != base.end(); ++b, ++c) {
    if (c == candidate.end() || *b != *c) {
      malformed("Catalog archive escapes catalog directory");
    }
  }
  return candidate;
}
}  // namespace bw::resources
