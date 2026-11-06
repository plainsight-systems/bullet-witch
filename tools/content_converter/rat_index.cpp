#include "bw/content_converter/rat_index.hpp"

#include "bytes.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <utility>
#include <vector>

// Responsibility: the out-of-line part of bw/content_converter/rat_index.hpp:
// the catalog parse and the entry's relative path.
//
// Contract (the header's): parse_rat names the first failure in RatError
// order; the string checks are two passes over every entry, offsets first,
// then terminators, so an index that breaks both reports StringOutOfRange
// whichever entry breaks it. The two zero words at 8 and 12 are not checked:
// the header states them as observed and names no failure for them.
namespace bw::content_converter {

namespace {

constexpr std::array<char, 4> kRatMagic{'R', 'A', 'T', ' '};
constexpr std::size_t kSectionTableAt = 0x10;
constexpr std::size_t kFileTableSection = 4;
constexpr std::size_t kStringSection = 5;

} // namespace

RatError parse_rat(std::span<const std::byte> file, RatIndex& index) {
  using E = RatError;
  if (file.size() < kRatHeaderBytes) {
    return E::TooShort;
  }
  if (bytes::tag(file, 0) != kRatMagic) {
    return E::BadMagic;
  }
  if (bytes::u32(file, 4) != file.size()) {
    return E::SizeMismatch;
  }
  std::vector<RatSection> sections(kRatSections);
  for (std::size_t s = 0; s < kRatSections; ++s) {
    sections[s].offset = bytes::u32(file, kSectionTableAt + (8U * s));
    sections[s].size = bytes::u32(file, kSectionTableAt + (8U * s) + 4U);
  }
  for (const RatSection& s : sections) {
    if (!bytes::within(s.offset, s.size, file.size())) {
      return E::SectionOutOfRange;
    }
  }
  const RatSection table = sections[kFileTableSection];
  if (table.size % kRatFileEntryBytes != 0U) {
    return E::FileTableMisaligned;
  }
  const std::span<const std::byte> strings =
      file.subspan(sections[kStringSection].offset, sections[kStringSection].size);
  const std::size_t count = table.size / kRatFileEntryBytes;
  std::vector<std::array<std::uint32_t, 2>> offsets(count);
  for (std::size_t i = 0; i < count; ++i) {
    const std::size_t at = table.offset + (i * kRatFileEntryBytes);
    offsets[i] = {bytes::u32(file, at), bytes::u32(file, at + 4)};
  }
  for (const std::array<std::uint32_t, 2>& entry : offsets) {
    if (entry[0] >= strings.size() || entry[1] >= strings.size()) {
      return E::StringOutOfRange;
    }
  }
  std::vector<RatEntry> files(count);
  for (std::size_t i = 0; i < count; ++i) {
    const bytes::CString directory = bytes::c_string(strings, offsets[i][0]);
    const bytes::CString name = bytes::c_string(strings, offsets[i][1]);
    if (!directory.found || !name.found) {
      return E::StringUnterminated;
    }
    files[i] = RatEntry{directory.text, name.text};
  }
  index.sections_ = std::move(sections);
  index.files_ = std::move(files);
  return E::None;
}

std::string relative_path(const RatEntry& entry) {
  if (entry.directory.empty()) {
    return std::string(entry.file_name);
  }
  std::string path(entry.directory);
  for (char& c : path) {
    if (c == '\\') {
      c = '/';
    }
  }
  path += '/';
  path += entry.file_name;
  return path;
}

} // namespace bw::content_converter
