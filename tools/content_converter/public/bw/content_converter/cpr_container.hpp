#ifndef BW_CONTENT_CONVERTER_CPR_CONTAINER_HPP
#define BW_CONTENT_CONVERTER_CPR_CONTAINER_HPP

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

// Responsibility: reading the original game's container files
// (docs/architecture/content.md, invariant 1: readers of the original
// formats exist in the converter tool alone). A container is parsed from
// its bytes into records with their names, call arguments, slot bytes and
// payload spans; nothing is interpreted beyond the framing.
//
// Framing, little-endian u32 unless stated, as observed in all 6,625 files:
//   0x00 "CPR1"; 0x04 1; 0x08 0x11223344; 0x10 record table bytes
//   (48 per record); 0x14 call-argument table bytes; 0x18 names block
//   bytes; 0x1C slot block bytes; 0x20 region 0 bytes; 0x24 region 1
//   bytes; 0x2C the sum of 0x10, 0x14, 0x18 and 0x1C; 0x34 a page pool
//   size (not used to slice); the header is 0x60 bytes.
//   Record table at 0x60: per record, a 4-byte tag, name offset, qualified
//   name offset (0 none), tail block offset (0xFFFFFFFF none), argument
//   count, first argument index, slot offset, slot size, region 0 payload
//   offset and size (USER), region 1 payload offset and size (DXVB, DXIB,
//   DDS, DDSC).
//   Call-argument table at 0x60 + 0x10: 12-byte triples of slot-name
//   offset, kind (2: the value is a names offset; 0: an integer literal),
//   value; the table is padded to 16 bytes (2,609 files), so it holds
//   floor(bytes / 12) triples. USER records may carry arguments (16 do),
//   so arguments_of answers for any tag.
//   Names block at 0x60 + 0x10 + 0x14: a NUL at offset 0, then NUL-ended
//   strings. Slot block follows the names. Region 0 begins at 0x60 + 0x2C;
//   region 1 at region 0 plus 0x20.
//   A tail block (13 sprite files, 45 records): at the record's tail
//   offset, 5 u32 offsets and 5 u32 sizes, then 5 PNGs at those offsets
//   relative to the end of the two tables.
//
// Contract.
//   parse_cpr names a failure when a rule is broken; a container that
//   parses has every record's names inside the names block and
//   NUL-terminated, every payload inside its region, every argument range
//   inside the table with slot-name offsets and kind-2 values inside the
//   names block (ArgumentValueOutOfRange otherwise), every slot
//   inside the slot block, and every tail block inside the file. Payload
//   spans view the file bytes; nothing is copied.
//   Tags outside the eight known ones are RecordTag::Unknown, kept with
//   their raw bytes, never an error: the converter reports them by name.
//   find_record: first record with that short name, bytewise. Qualified
//   names are compared ignoring ASCII case, as CALL arguments keep original
//   case while stored qualified names are lower-case.
//   The slot block and the page pool word are kept as bytes and not
//   interpreted.
//
// Core guidelines read: SL.con.3 (every offset checked before a read),
// C.183 (words assembled from bytes), E.27 (named failures), R.14
// and SL.str.2 (spans and views over the caller's bytes). Performance
// guidelines read: CDSA.32 was read; the container is a source format and
// is never the consumer's layout, so it is parsed once into views and the
// converter copies out what it keeps.
namespace bw::content_converter {

inline constexpr std::size_t kCprHeaderBytes = 0x60;
inline constexpr std::size_t kCprRecordBytes = 48;
inline constexpr std::size_t kCprArgumentBytes = 12;
inline constexpr std::uint32_t kCprNoTail = 0xFFFFFFFFU;
inline constexpr std::uint32_t kCprArgumentKindName = 2;
inline constexpr std::uint32_t kCprArgumentKindInteger = 0;
inline constexpr std::size_t kCprTailImages = 5;

enum class RecordTag : std::uint8_t {
  User,
  Call,
  Dds,
  Ddsc,
  Dxvb,
  Dxib,
  Dxps,
  Dxvs,
  Unknown,
};
[[nodiscard]] constexpr RecordTag record_tag(std::array<char, 4> bytes) noexcept {
  constexpr std::array<std::array<char, 4>, 8> kTags{{{'U', 'S', 'E', 'R'},
                                                      {'C', 'A', 'L', 'L'},
                                                      {'D', 'D', 'S', ' '},
                                                      {'D', 'D', 'S', 'C'},
                                                      {'D', 'X', 'V', 'B'},
                                                      {'D', 'X', 'I', 'B'},
                                                      {'D', 'X', 'P', 'S'},
                                                      {'D', 'X', 'V', 'S'}}};
  for (std::size_t i = 0; i < kTags.size(); ++i) {
    if (kTags[i] == bytes) {
      return static_cast<RecordTag>(i);
    }
  }
  return RecordTag::Unknown;
}

struct CprHeader {
  std::uint32_t version{};
  std::uint32_t record_table_bytes{};
  std::uint32_t argument_table_bytes{};
  std::uint32_t names_bytes{};
  std::uint32_t slot_bytes{};
  std::uint32_t region0_bytes{};
  std::uint32_t region1_bytes{};
  std::uint32_t page_pool_bytes{};
  std::uint64_t region0_base{};
  std::uint64_t region1_base{};
};

struct CprRecord {
  RecordTag tag{RecordTag::Unknown};
  std::array<char, 4> tag_bytes{};
  std::string_view short_name{};
  // Empty when the record has none.
  std::string_view qualified_name{};
  std::uint32_t tail_offset{kCprNoTail};
  std::uint32_t argument_count{};
  std::uint32_t first_argument{};
  std::span<const std::byte> slot{};
  // Region 0 for User, region 1 for the buffer and image tags, empty for
  // Call, Dxps and Dxvs.
  std::span<const std::byte> payload{};
};

struct CprArgument {
  std::string_view slot_name{};
  std::uint32_t kind{};
  std::uint32_t raw_value{};
  // The names-block string for kind 2, empty otherwise.
  std::string_view value{};
};

enum class CprError : std::uint8_t {
  None,
  TooShort,
  BadMagic,
  UnsupportedVersion,
  // The four table sizes do not sum to the header's region 0 offset, the
  // record table is not a multiple of 48 bytes, or a table or region runs
  // past the file.
  LayoutInconsistent,
  NameOutOfRange,
  NameUnterminated,
  PayloadOutOfRange,
  ArgumentRangeOutOfRange,
  ArgumentValueOutOfRange,
  SlotOutOfRange,
  TailOutOfRange,
};

class CprContainer {
public:
  CprContainer() = default;
  [[nodiscard]] const CprHeader& header() const noexcept { return header_; }
  [[nodiscard]] std::span<const CprRecord> records() const noexcept { return records_; }
  [[nodiscard]] std::span<const CprArgument> arguments() const noexcept { return arguments_; }
  // The record's argument triples, in table order.
  [[nodiscard]] std::span<const CprArgument> arguments_of(const CprRecord& record) const noexcept;
  [[nodiscard]] const CprRecord* find_record(std::string_view short_name) const noexcept;
  [[nodiscard]] const CprRecord*
  find_by_qualified_name(std::string_view qualified_name) const noexcept;
  // The record's tail images; empty when it has no tail block.
  [[nodiscard]] std::array<std::span<const std::byte>, kCprTailImages>
  tail_images(const CprRecord& record) const noexcept;
  [[nodiscard]] std::span<const std::byte> file() const noexcept { return file_; }

private:
  friend CprError parse_cpr(std::span<const std::byte>, CprContainer&);
  std::span<const std::byte> file_{};
  CprHeader header_{};
  std::vector<CprRecord> records_{};
  std::vector<CprArgument> arguments_{};
};

// Fills `container` over `file`, which the caller keeps alive; the first
// failure or None.
[[nodiscard]] CprError parse_cpr(std::span<const std::byte> file, CprContainer& container);

// ASCII case-insensitive equality, the comparison qualified names use.
[[nodiscard]] bool equals_ignoring_ascii_case(std::string_view a, std::string_view b) noexcept;

} // namespace bw::content_converter

#endif
