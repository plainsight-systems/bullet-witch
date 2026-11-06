// Unit test: public contracts of bw/content/cooked_asset.hpp; each scenario
// names the declarations it asserts.
#include "bw/content/cooked_asset.hpp"
#include "bw/testing/harness.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace {

using bw::content::AssetKind;
using bw::content::CookedError;
using bw::content::CookedHeaderFields;
using bw::content::CookedParse;
using bw::content::Hash;
using bw::testing::require;
using E = CookedError;

std::vector<std::byte> pattern(std::size_t size, unsigned seed) {
  std::vector<std::byte> bytes(size);
  for (std::size_t i = 0; i < size; ++i) {
    bytes[i] = static_cast<std::byte>((i * 7U + seed) & 0xFFU);
  }
  return bytes;
}

Hash counting_hash(unsigned start) {
  Hash hash{};
  for (std::size_t i = 0; i < hash.size(); ++i) {
    hash[i] = static_cast<std::uint8_t>((start + i) & 0xFFU);
  }
  return hash;
}

// A whole cooked file written through encode_cooked_header, with the payload
// hash computed by hash_bytes.
std::vector<std::byte> cooked_file(std::string_view id, AssetKind kind,
                                   const std::vector<std::byte>& payload) {
  const CookedHeaderFields fields{kind, payload.size(), counting_hash(1),
                                  bw::content::hash_bytes(payload), id};
  std::vector<std::byte> file(bw::content::cooked_payload_offset(id.size()));
  const std::size_t written = bw::content::encode_cooked_header(fields, file);
  require(written == file.size(), "fixture header encodes");
  file.insert(file.end(), payload.begin(), payload.end());
  return file;
}

void put_u32(std::vector<std::byte>& file, std::size_t at, std::uint32_t value) {
  for (std::size_t i = 0; i < 4; ++i) {
    file[at + i] = static_cast<std::byte>((value >> (8U * i)) & 0xFFU);
  }
}
void put_u64(std::vector<std::byte>& file, std::size_t at, std::uint64_t value) {
  put_u32(file, at, static_cast<std::uint32_t>(value & 0xFFFFFFFFU));
  put_u32(file, at + 4, static_cast<std::uint32_t>(value >> 32U));
}

// The layout's field offsets, as cooked_asset.hpp states them.
constexpr std::size_t kVersionAt = 4;
constexpr std::size_t kKindAt = 8;
constexpr std::size_t kFlagsAt = 12;
constexpr std::size_t kOffsetAt = 16;
constexpr std::size_t kSizeAt = 24;

// The parse is a constant expression over a constexpr byte array.
constexpr std::size_t kConstantIdLength = 3;
constexpr std::size_t kConstantFileSize =
    bw::content::cooked_payload_offset(kConstantIdLength) + 4U;
constexpr auto kConstantFile = [] {
  std::array<std::byte, kConstantFileSize> file{};
  const CookedHeaderFields fields{AssetKind::Skeleton, 4, Hash{}, Hash{}, "a.b"};
  const std::size_t written = bw::content::encode_cooked_header(fields, file);
  file[written] = std::byte{1};
  return file;
}();
static_assert(bw::content::parse_cooked_header(kConstantFile).ok());
static_assert(bw::content::parse_cooked_header(kConstantFile).header.kind == AssetKind::Skeleton);
static_assert(bw::content::parse_cooked_header(kConstantFile).header.payload_size == 4U);

// (cooked_asset.hpp, encode_cooked_header, parse_cooked_header,
// cooked_payload_offset, cooked_payload, cooked_id): what is encoded parses
// back field for field; the payload starts at the 16-aligned offset after the
// id and ends the file; encode writes nothing for an invalid id or a short
// buffer.
void header_round_trip() {
  const std::string_view id = "model.pw0.mesh";
  const std::vector<std::byte> payload = pattern(37, 3);
  const Hash input = counting_hash(100);
  const Hash payload_hash = counting_hash(200);
  const CookedHeaderFields fields{AssetKind::Texture, payload.size(), input, payload_hash, id};
  const std::size_t offset = bw::content::cooked_payload_offset(id.size());
  require(offset % bw::content::kCookedPayloadAlignment == 0U &&
              offset >= bw::content::kCookedHeaderBytes + id.size() &&
              offset < bw::content::kCookedHeaderBytes + id.size() +
                           bw::content::kCookedPayloadAlignment,
          "the payload offset is the first 16-aligned byte after the id");
  std::vector<std::byte> file(offset);
  require(bw::content::encode_cooked_header(fields, file) == offset, "encode writes the header");
  file.insert(file.end(), payload.begin(), payload.end());

  const CookedParse parse = bw::content::parse_cooked_header(file);
  require(parse.ok(), "the encoded file parses");
  const bw::content::CookedHeader& h = parse.header;
  require(h.format_version == bw::content::kCookedFormatVersion, "format version");
  require(h.kind == AssetKind::Texture && h.flags == 0U, "kind and flags");
  require(h.payload_offset == offset && h.payload_size == payload.size(), "payload placement");
  require(h.input_hash == input && h.payload_hash == payload_hash, "both hashes");
  require(h.id_length == id.size() && bw::content::cooked_id(file, h) == id, "the id");
  const std::span<const std::byte> view = bw::content::cooked_payload(file, h);
  require(std::vector<std::byte>(view.begin(), view.end()) == payload, "the payload bytes");

  std::vector<std::byte> out(offset);
  CookedHeaderFields invalid = fields;
  invalid.id = "Model.pw0";
  require(bw::content::encode_cooked_header(invalid, out) == 0U, "an invalid id writes nothing");
  std::vector<std::byte> short_out(offset - 1U);
  require(bw::content::encode_cooked_header(fields, short_out) == 0U,
          "a buffer one byte short writes nothing");
}

// (cooked_asset.hpp, parse_cooked_header, CookedError): each row breaks one
// rule of a valid file and is refused by that rule's name.
void rejections_named() {
  const std::string_view id = "a.b";
  const std::vector<std::byte> valid = cooked_file(id, AssetKind::Mesh, pattern(20, 9));
  require(bw::content::parse_cooked_header(valid).ok(), "the base file parses");
  struct Row {
    std::vector<std::byte> file;
    E expected;
    std::string_view note;
  };
  const auto mutated = [&valid](auto change) {
    std::vector<std::byte> file = valid;
    change(file);
    return file;
  };
  const std::array rows{
      Row{std::vector<std::byte>(valid.begin(), valid.begin() + 103), E::TooShort,
          "fewer bytes than the fixed header"},
      Row{mutated([](auto& f) { f[0] = std::byte{'X'}; }), E::BadMagic, "another magic"},
      Row{mutated([](auto& f) { put_u32(f, kVersionAt, bw::content::kCookedFormatVersion + 1U); }),
          E::VersionMismatch, "another format version"},
      Row{mutated([](auto& f) { put_u32(f, kKindAt, 0); }), E::UnknownKind, "kind 0"},
      Row{mutated([](auto& f) { put_u32(f, kFlagsAt, 1); }), E::FlagsUnknown, "a flag bit"},
      Row{mutated([](auto& f) { f[bw::content::kCookedHeaderBytes] = std::byte{'A'}; }),
          E::IdInvalid, "an upper-case id byte"},
      Row{mutated([](auto& f) { put_u64(f, kOffsetAt, 128); }), E::PayloadMisaligned,
          "an offset other than cooked_payload_offset(id_length)"},
      Row{mutated([](auto& f) { put_u64(f, kSizeAt, 21); }), E::PayloadOutOfBounds,
          "a payload past the end"},
  };
  for (std::size_t i = 0; i < rows.size(); ++i) {
    require(bw::content::parse_cooked_header(rows[i].file).error == rows[i].expected, i,
            rows[i].note);
  }
}

using Scenario = bw::testing::Scenario<void>;
constexpr std::array kScenarios{
    Scenario{"header_round_trip", &header_round_trip},
    Scenario{"rejections_named", &rejections_named},
};

} // namespace

int main(int argc, char** argv) { return bw::testing::run_scenario(argc, argv, kScenarios); }
