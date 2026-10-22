// Resource foundation interface; current A1/A2/A3/P1 grouping, as mapped in
// docs/architecture/file-mapping.md (review draft). Implemented contract:
// docs/resources.md. This file declares owned metadata and read-only byte
// views.
//
// Contract: parse takes ownership of bytes; open reads a file into that owner.
// Validated metadata contains file-relative ranges, never native relocated
// pointers. Returned references/spans borrow from the object and must not be
// retained across its destruction, assignment or move. Copies own independent
// storage. Parse rejects malformed/unsupported data with Error; allocation and
// standard-library failures may propagate. payload(index) requires a valid
// record index (otherwise out_of_range); no payload gives an empty span.
// CatalogSet mutation requires exclusive access; const access requires stable
// owners. No runtime resource handler is executed.
//
// Algorithm: (1) acquire owned bytes; (2) validate header/section bounds;
// (3) decode metadata and index catalog names; (4) resolve across mounted
// catalogs using the lowest priority key, rejecting equal-key ambiguity;
// (5) return a checked payload view or a path confined to the catalog
// directory. Costs: O(B) owned input bytes plus decoded strings/metadata and
// catalog index; copying duplicates these. Payload slicing is O(1), with no
// payload copy. Optimization: metadata/index construction happens once per
// parse for later queries. These are derived costs, not throughput
// measurements.
//
// Retrieved design guidance: cpp-guidelines I.1 (#ri-explicit), I.30
// (#ri-encapsulate): make ownership and low-level boundaries explicit.
// cpp-performance COPY.2 (guidelines/copy-move/
// COPY.2-mark-move-constructors-noexcept.md): retain rule-of-zero container
// ownership/moves; do not claim arbitrary operations noexcept.
// Implementation consultation: cpp-guidelines SL.con.3 (#rsl-bounds) informs
// checked access; cpp-performance CDSA.32 (guidelines/cpu-dsa/
// CDSA.32-transform-static-data-into-the-consumer-layout.md) informs the
// once-built catalog index, not speculative conversion of opaque payloads.

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace bw::resources {
inline constexpr std::size_t kCprRecordWordCount = 11;
inline constexpr std::size_t kCprHeaderWordCount = 23;
inline constexpr std::size_t kSectionCount = 6;
inline constexpr std::size_t kRatResourceWordCount = 5;
using Bytes = std::vector<std::uint8_t>;
enum class ErrorCode : std::uint8_t {
  io,
  malformed,
  unsupported,
  not_found,
  ambiguous
};
class Error : public std::runtime_error {
 public:
  Error(ErrorCode code, const std::string& message)
      : std::runtime_error(message), code(code) {}
  ErrorCode code;
};
std::string_view error_name(ErrorCode code);
Bytes read_file(const std::filesystem::path& path);
std::string normalize_name(std::string_view name);

// These are file ranges, never pointers into a relocated 32-bit image.
struct Range {
  std::uint64_t offset{}, size{};
};
struct Argument {
  std::array<std::uint32_t, 3> raw{};
  std::optional<std::string> name;
  std::optional<std::string> string_value;
};
struct CprRecord {
  std::string type, name;
  std::optional<std::string> publication_name;
  std::array<std::uint32_t, kCprRecordWordCount> raw{};
  std::vector<Argument> arguments;
  std::optional<Range> first_payload, second_payload, selected_payload;
  bool known_type{};  // Identified native registry entry; no handler is
                      // executed.
};
class CprArchive {
 public:
  static CprArchive parse(Bytes bytes);
  static CprArchive open(const std::filesystem::path& path);
  const auto& header() const { return header_; }
  const auto& sections() const {
    return sections_;
  }  // table, arguments, names, gap, first, second
  const auto& records() const { return records_; }
  const auto& argument_table() const { return arguments_; }
  std::span<const std::uint8_t> bytes() const { return bytes_; }
  std::span<const std::uint8_t> payload(std::size_t record_index) const;

 private:
  Bytes bytes_;
  std::array<std::uint32_t, kCprHeaderWordCount> header_{};
  std::array<Range, kSectionCount> sections_{};
  std::vector<CprRecord> records_;
  std::vector<Argument> arguments_;
};
struct CatalogFile {
  std::string directory, filename, relative_path;
};
struct Dependency {
  std::optional<std::uint32_t> local_index;
  std::string name;  // Even local-index dependencies are requested through
                     // global name lookup.
};
struct CatalogResource {
  std::array<std::uint32_t, kRatResourceWordCount> raw{};
  std::string name, full_name;
  std::optional<std::string> extra;
  std::vector<Dependency> dependencies;
};
struct NamespaceNode {
  std::uint32_t offset{}, parent{};
  std::string name;
  std::vector<std::uint32_t> children, resources;
};
class RatCatalog {
 public:
  static RatCatalog parse(Bytes bytes);
  static RatCatalog open(const std::filesystem::path& path);
  const auto& sections() const { return sections_; }
  const auto& files() const { return files_; }
  const auto& resources() const { return resources_; }
  const auto& namespaces() const { return namespaces_; }
  std::span<const std::uint8_t> bytes() const { return bytes_; }
  std::optional<std::size_t> find(std::string_view name) const;

 private:
  Bytes bytes_;
  std::array<Range, kSectionCount> sections_{};
  std::vector<CatalogFile> files_;
  std::vector<CatalogResource> resources_;
  std::vector<NamespaceNode> namespaces_;
  std::map<std::string, std::size_t, std::less<>> index_;
};
struct MountedCatalog {
  int priority;
  std::filesystem::path path;
  RatCatalog catalog;
};
struct Resolution {
  std::size_t catalog_index, resource_index;
};
class CatalogSet {
 public:
  void mount(int priority, std::filesystem::path path, RatCatalog catalog);
  Resolution resolve(std::string_view name) const;
  const auto& catalogs() const { return catalogs_; }

 private:
  std::vector<MountedCatalog> catalogs_;
};
// Reject catalog paths escaping their own directory, including through
// symlinks.
std::filesystem::path archive_path(const MountedCatalog& catalog,
                                   std::size_t file_index);
}  // namespace bw::resources
