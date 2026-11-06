#ifndef BW_CONTENT_CONVERTER_RAT_INDEX_HPP
#define BW_CONTENT_CONVERTER_RAT_INDEX_HPP

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// Responsibility: the container list of the install, read from the
// install's own catalog files rather than by walking directories, so the
// set the converter visits is the set the game names and its order is the
// catalog's. 25 `.rat` files list all 6,625 containers exactly once.
//
// Framing: "RAT " at 0; u32 total size at 4 (equals the file size); two
// zero u32; six (offset, size) u32 pairs at 0x10 for sections 0 to 5.
// Section 4 is 8-byte entries of (directory string offset, file name
// string offset) into section 5, a pool of NUL-ended strings with the
// empty string at offset 0. Directory strings use '\\' between
// components and are relative to the `.rat` file's own directory; an
// empty directory puts the file beside the `.rat`.
//
// Contract.
//   parse_rat names a failure when a rule is broken; an index that
//   parses has every section inside the file, section 4 a multiple of 8
//   and every string offset inside section 5 and NUL-terminated.
//   relative_path(entry) is the directory with '\\' replaced by '/', then
//   '/', then the file name; the file name alone when the directory is
//   empty. Joined to the `.rat` file's directory it names an existing
//   container in 6,625 of 6,625 entries.
//   Sections 0, 2 and 3 (the resource tree and resource entries) are kept
//   as bytes and not decoded: the converter derives ids from the records'
//   own qualified names, not from the catalog.
//
// Core guidelines read: SL.con.3, C.183, E.27 (as cpr_container.hpp).
namespace bw::content_converter {

inline constexpr std::size_t kRatHeaderBytes = 0x40;
inline constexpr std::size_t kRatSections = 6;
inline constexpr std::size_t kRatFileEntryBytes = 8;

struct RatSection {
  std::uint32_t offset{};
  std::uint32_t size{};
};

struct RatEntry {
  std::string_view directory{};
  std::string_view file_name{};
};

enum class RatError : std::uint8_t {
  None,
  TooShort,
  BadMagic,
  SizeMismatch,
  SectionOutOfRange,
  FileTableMisaligned,
  StringOutOfRange,
  StringUnterminated,
};

class RatIndex {
public:
  RatIndex() = default;
  [[nodiscard]] std::span<const RatSection> sections() const noexcept { return sections_; }
  [[nodiscard]] std::span<const RatEntry> files() const noexcept { return files_; }

private:
  friend RatError parse_rat(std::span<const std::byte>, RatIndex&);
  std::vector<RatSection> sections_{};
  std::vector<RatEntry> files_{};
};

[[nodiscard]] RatError parse_rat(std::span<const std::byte> file, RatIndex& index);

// "<directory with / separators>/<file name>" or "<file name>".
[[nodiscard]] std::string relative_path(const RatEntry& entry);

} // namespace bw::content_converter

#endif
