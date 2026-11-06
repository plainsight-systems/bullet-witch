#ifndef BW_CONTENT_COOKED_ASSET_HPP
#define BW_CONTENT_COOKED_ASSET_HPP
#include "bw/content/asset_id.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

// Responsibility: the one frame every cooked asset shares
// (docs/architecture/content.md, invariants 1, 7 and 11 and "Asset kinds":
// every kind shares the asset header of id, kind, format version, input hash
// and payload size; the format carries a version; a mismatch fails by name).
// The loader validates a file on this header alone and hands the payload
// bytes to the consuming system's reader; the converter and the download
// client of the next slice are the only writers.
//
// Layout, little-endian, fixed:
//   0   char[4]  magic "BWCK"
//   4   u32      format_version (kCookedFormatVersion)
//   8   u32      kind (AssetKind)
//   12  u32      flags, 0 in this version; a nonzero value is FlagsUnknown
//   16  u64      payload_offset from the file start, a multiple of 16
//   24  u64      payload_size
//   32  u8[32]   input_hash: BLAKE3 of the converter's inputs for the asset
//                (the record bytes and every record it resolved), the key of
//                incremental conversion; the loader does not check it
//   64  u8[32]   payload_hash: BLAKE3 of the payload bytes, the key the
//                loader checks (the plan's loader paragraph names a payload
//                hash; its 64-byte field list omitted it, so the header is
//                104 bytes, not 64)
//   96  u32      id_length
//   100 u32      reserved, 0
//   104 char[id_length] the id, then zero bytes to payload_offset, which is
//                cooked_payload_offset(id_length) = round up of
//                104 + id_length to 16; the payload follows and the file
//                ends with it.
//
// Contract.
//   parse_cooked_header reads a whole file's bytes and names a
//   failure when a rule is broken; a header that parses has every field in
//   range, an id that validates, a payload offset equal to
//   cooked_payload_offset(id_length) and a payload that ends exactly at
//   the file's end. It does not hash. It is a constant expression over a
//   constexpr byte array; the id is read back with cooked_id, which views
//   the file bytes and is the one non-constexpr accessor.
//   verify_payload_hash computes BLAKE3 over cooked_payload and compares it
//   to payload_hash: None or PayloadHashMismatch. hash_bytes is that
//   BLAKE3, 32 bytes, so a test can state a hash by computing it the same
//   way; the function is deterministic and has no key or context.
//   check_expectations compares a parsed header and its file against what
//   a caller asked for: IdMismatch (bytewise) before KindMismatch.
//   encode_cooked_header writes the header, the id and the padding for the
//   given fields into `out`, which holds at least cooked_payload_offset
//   bytes, and returns the bytes written, or 0 when the id does not
//   validate or `out` is short. It is the inverse of parse for the header
//   part and is what every writer uses, so there is one layout.
//   AssetKind values are fixed numbers in the file; a later slice adds a
//   kind by appending an enumerator and never renumbers.
//
// Core guidelines read: SL.con.3 (every offset is checked before a read),
// C.183 (bytes are assembled, never type-punned), E.27 (one named code per
// failure), Con.5 (the parse is constexpr).
// Performance guidelines read: CACHE.2 (the header is 104 bytes and the
// payload starts 16-aligned so a consumer's records are aligned without a
// copy), CDSA.32 (the payload is in the consumer's layout; the frame adds
// no interpretation), GDSA.2 was read and found not applicable (no
// floating point here).
namespace bw::content {

inline constexpr std::array<char, 4> kCookedMagic{'B', 'W', 'C', 'K'};
inline constexpr std::uint32_t kCookedFormatVersion = 1;
inline constexpr std::size_t kCookedHeaderBytes = 104;
inline constexpr std::size_t kCookedPayloadAlignment = 16;
inline constexpr std::size_t kHashBytes = 32;
using Hash = std::array<std::uint8_t, kHashBytes>;

// Fixed file values (a u32 in the file); never renumbered.
enum class AssetKind : std::uint8_t {
  Mesh = 1,
  Texture = 2,
  Material = 3,
  Skeleton = 4,
  MorphTargets = 5,
};
[[nodiscard]] constexpr bool is_known_kind(std::uint32_t value) noexcept {
  return value >= 1U && value <= 5U;
}
[[nodiscard]] constexpr std::string_view kind_name(AssetKind kind) noexcept {
  switch (kind) {
  case AssetKind::Mesh:
    return "mesh";
  case AssetKind::Texture:
    return "texture";
  case AssetKind::Material:
    return "material";
  case AssetKind::Skeleton:
    return "skeleton";
  case AssetKind::MorphTargets:
    return "morph_targets";
  }
  return "unknown";
}

// A parsed header. The id's bytes are at kCookedHeaderBytes in the file;
// cooked_id views them.
struct CookedHeader {
  std::uint32_t format_version{};
  AssetKind kind{};
  std::uint32_t flags{};
  std::uint64_t payload_offset{};
  std::uint64_t payload_size{};
  Hash input_hash{};
  Hash payload_hash{};
  std::uint32_t id_length{};
};

// Named failures. parse_cooked_header names TooShort for the fixed header or
// the id bytes, IdInvalid for an id_length over kMaxAssetIdBytes or an id
// text that does not validate, PayloadMisaligned for the offset or the
// padding, and the others as named.
// verify_payload_hash adds PayloadHashMismatch; check_expectations adds
// IdMismatch and KindMismatch.
enum class CookedError : std::uint8_t {
  None,
  // Fewer bytes than the fixed header, or than the header plus the id.
  TooShort,
  BadMagic,
  VersionMismatch,
  UnknownKind,
  // flags or the reserved word holds a nonzero value: a bit this version
  // does not know.
  FlagsUnknown,
  // The id does not validate (asset_id.hpp).
  IdInvalid,
  // payload_offset is not cooked_payload_offset(id_length), or the padding
  // is not zero.
  PayloadMisaligned,
  // payload_offset is past the file's end, or payload_offset +
  // payload_size is not the file's size.
  PayloadOutOfBounds,
  // verify_payload_hash: the hash of the payload bytes differs.
  PayloadHashMismatch,
  // check_expectations.
  IdMismatch,
  KindMismatch,
};

struct CookedParse {
  CookedHeader header{};
  CookedError error{CookedError::None};
  [[nodiscard]] constexpr bool ok() const noexcept { return error == CookedError::None; }
};

[[nodiscard]] constexpr std::size_t cooked_payload_offset(std::size_t id_length) noexcept {
  const std::size_t end = kCookedHeaderBytes + id_length;
  return (end + kCookedPayloadAlignment - 1) / kCookedPayloadAlignment * kCookedPayloadAlignment;
}

[[nodiscard]] constexpr CookedParse parse_cooked_header(std::span<const std::byte> file) noexcept;

// Precondition: `header` parsed from `file`.
[[nodiscard]] constexpr std::span<const std::byte>
cooked_payload(std::span<const std::byte> file, const CookedHeader& header) noexcept {
  return file.subspan(static_cast<std::size_t>(header.payload_offset),
                      static_cast<std::size_t>(header.payload_size));
}

// Precondition: `header` parsed from `file`.
[[nodiscard]] constexpr bool cooked_id_equals(std::span<const std::byte> file,
                                              const CookedHeader& header,
                                              std::string_view id) noexcept {
  if (header.id_length != id.size()) {
    return false;
  }
  for (std::size_t i = 0; i < id.size(); ++i) {
    if (std::to_integer<char>(file[kCookedHeaderBytes + i]) != id[i]) {
      return false;
    }
  }
  return true;
}
// The id as text, viewing `file`; not a constant expression.
[[nodiscard]] inline std::string_view cooked_id(std::span<const std::byte> file,
                                                const CookedHeader& header) noexcept {
  return std::string_view(reinterpret_cast<const char*>(file.data()) + kCookedHeaderBytes,
                          header.id_length);
}

[[nodiscard]] constexpr CookedError check_expectations(std::span<const std::byte> file,
                                                       const CookedHeader& header,
                                                       std::string_view id,
                                                       AssetKind kind) noexcept {
  if (!cooked_id_equals(file, header, id)) {
    return CookedError::IdMismatch;
  }
  if (header.kind != kind) {
    return CookedError::KindMismatch;
  }
  return CookedError::None;
}

[[nodiscard]] Hash hash_bytes(std::span<const std::byte> bytes) noexcept;
[[nodiscard]] CookedError verify_payload_hash(std::span<const std::byte> file,
                                              const CookedHeader& header) noexcept;

// What a writer supplies; payload_offset is derived.
struct CookedHeaderFields {
  AssetKind kind{};
  std::uint64_t payload_size{};
  Hash input_hash{};
  Hash payload_hash{};
  std::string_view id{};
};
[[nodiscard]] constexpr std::size_t encode_cooked_header(const CookedHeaderFields& fields,
                                                         std::span<std::byte> out) noexcept;

// ---- constexpr definitions ------------------------------------------------

namespace detail {

[[nodiscard]] constexpr std::uint32_t read_u32(std::span<const std::byte> bytes,
                                               std::size_t at) noexcept {
  std::uint32_t value = 0;
  for (std::size_t i = 0; i < 4; ++i) {
    value |= static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(bytes[at + i])) << (8U * i);
  }
  return value;
}
[[nodiscard]] constexpr std::uint64_t read_u64(std::span<const std::byte> bytes,
                                               std::size_t at) noexcept {
  return static_cast<std::uint64_t>(read_u32(bytes, at)) |
         (static_cast<std::uint64_t>(read_u32(bytes, at + 4)) << 32U);
}
constexpr void write_u32(std::span<std::byte> bytes, std::size_t at, std::uint32_t value) noexcept {
  for (std::size_t i = 0; i < 4; ++i) {
    bytes[at + i] = static_cast<std::byte>((value >> (8U * i)) & 0xFFU);
  }
}
constexpr void write_u64(std::span<std::byte> bytes, std::size_t at, std::uint64_t value) noexcept {
  write_u32(bytes, at, static_cast<std::uint32_t>(value & 0xFFFFFFFFU));
  write_u32(bytes, at + 4, static_cast<std::uint32_t>(value >> 32U));
}

} // namespace detail

constexpr CookedParse parse_cooked_header(std::span<const std::byte> file) noexcept {
  CookedParse parse{};
  if (file.size() < kCookedHeaderBytes) {
    parse.error = CookedError::TooShort;
    return parse;
  }
  for (std::size_t i = 0; i < 4; ++i) {
    if (std::to_integer<char>(file[i]) != kCookedMagic[i]) {
      parse.error = CookedError::BadMagic;
      return parse;
    }
  }
  CookedHeader& h = parse.header;
  h.format_version = detail::read_u32(file, 4);
  if (h.format_version != kCookedFormatVersion) {
    parse.error = CookedError::VersionMismatch;
    return parse;
  }
  const std::uint32_t kind = detail::read_u32(file, 8);
  if (!is_known_kind(kind)) {
    parse.error = CookedError::UnknownKind;
    return parse;
  }
  h.kind = static_cast<AssetKind>(kind);
  h.flags = detail::read_u32(file, 12);
  if (h.flags != 0U || detail::read_u32(file, 100) != 0U) {
    parse.error = CookedError::FlagsUnknown;
    return parse;
  }
  h.payload_offset = detail::read_u64(file, 16);
  h.payload_size = detail::read_u64(file, 24);
  for (std::size_t i = 0; i < kHashBytes; ++i) {
    h.input_hash[i] = std::to_integer<std::uint8_t>(file[32 + i]);
    h.payload_hash[i] = std::to_integer<std::uint8_t>(file[64 + i]);
  }
  const std::uint32_t id_length = detail::read_u32(file, 96);
  if (id_length > kMaxAssetIdBytes) {
    parse.error = CookedError::IdInvalid;
    return parse;
  }
  if (file.size() < kCookedHeaderBytes + id_length) {
    parse.error = CookedError::TooShort;
    return parse;
  }
  h.id_length = id_length;
  if (validate_asset_id(file.subspan(kCookedHeaderBytes, id_length)) != AssetIdError::None) {
    parse.error = CookedError::IdInvalid;
    return parse;
  }
  const std::size_t expected_offset = cooked_payload_offset(id_length);
  if (h.payload_offset != expected_offset) {
    parse.error = CookedError::PayloadMisaligned;
    return parse;
  }
  if (h.payload_offset > file.size() || h.payload_size != file.size() - h.payload_offset) {
    parse.error = CookedError::PayloadOutOfBounds;
    return parse;
  }
  for (std::size_t i = kCookedHeaderBytes + id_length; i < expected_offset; ++i) {
    if (file[i] != std::byte{0}) {
      parse.error = CookedError::PayloadMisaligned;
      return parse;
    }
  }
  return parse;
}

constexpr std::size_t encode_cooked_header(const CookedHeaderFields& fields,
                                           std::span<std::byte> out) noexcept {
  if (validate_asset_id(fields.id) != AssetIdError::None) {
    return 0;
  }
  const std::size_t total = cooked_payload_offset(fields.id.size());
  if (out.size() < total) {
    return 0;
  }
  for (std::size_t i = 0; i < total; ++i) {
    out[i] = std::byte{0};
  }
  for (std::size_t i = 0; i < 4; ++i) {
    out[i] = static_cast<std::byte>(kCookedMagic[i]);
  }
  detail::write_u32(out, 4, kCookedFormatVersion);
  detail::write_u32(out, 8, static_cast<std::uint32_t>(fields.kind));
  detail::write_u32(out, 12, 0);
  detail::write_u64(out, 16, total);
  detail::write_u64(out, 24, fields.payload_size);
  for (std::size_t i = 0; i < kHashBytes; ++i) {
    out[32 + i] = static_cast<std::byte>(fields.input_hash[i]);
    out[64 + i] = static_cast<std::byte>(fields.payload_hash[i]);
  }
  detail::write_u32(out, 96, static_cast<std::uint32_t>(fields.id.size()));
  detail::write_u32(out, 100, 0);
  for (std::size_t i = 0; i < fields.id.size(); ++i) {
    out[kCookedHeaderBytes + i] = static_cast<std::byte>(fields.id[i]);
  }
  return total;
}

} // namespace bw::content

#endif
