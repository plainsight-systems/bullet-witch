#ifndef BW_CONTENT_ASSET_ID_HPP
#define BW_CONTENT_ASSET_ID_HPP

#include <compare>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

// Responsibility: the logical id of an asset and the rules that derive one
// (docs/architecture/content.md, invariant 2 and "Identity": ids are the
// names the original content already carries for its records, lower-cased,
// dotted and unique across the install; assets the converter splits out of
// one record take that record's id plus a suffix; assets we author take
// `remaster.` ids). The loader, the converter and the manifest of the next
// slice all speak this type; nothing else names an asset.
//
// Contract.
//   Text: an id is 1 to kMaxAssetIdBytes bytes of segments separated by '.',
//   each segment 1 to kMaxAssetIdSegmentBytes bytes of [a-z0-9_-]. No
//   leading, trailing or doubled separator. validate_asset_id names a
//   rule the text breaks. make_asset_id changes nothing:
//   text with an upper-case letter is InvalidCharacter, because the
//   derivations below are the only place case folds.
//   Derivation from a qualified name: ASCII lower-casing of the record's
//   qualified name as the container stores it, no other change. The names
//   are already dotted and lower-case in 15,253 of 15,254 records; the
//   converter folds the CALL arguments that keep original case.
//   Derivation for a record with no qualified name: the container path
//   relative to the data root, lower-cased, with '/' or '\\' replaced by
//   '.' and a trailing ".cpr" dropped, then '.', then the record's short
//   name with its extension removed (the text after the last '.' of the
//   short name; a name with no '.' is taken whole). Example:
//   ("data/data/stable/sprite/title/title.cpr", "title_3.png") gives
//   "data.data.stable.sprite.title.title.title_3". Any byte outside the
//   segment alphabet after folding is InvalidCharacter; the converter
//   reports that record by name instead of inventing an id.
//   Split assets: split_asset_id(parent, Material, 3) is
//   "<parent>.material.3"; (parent, Skeleton) is "<parent>.skeleton";
//   (parent, Shapes, n) is "<parent>.shapes.<n>". The index is decimal with
//   no padding.
//   Uniqueness is by construction of the names and is checked by the
//   converter, which fails by name on a collision; this header does not
//   keep a registry.
//   Layer file: asset_relative_path(id) is the id's segments joined by '/'
//   with kCookedAssetExtension appended: "model/pw0/mesh.bwc". The
//   extension exists so that "model.pw0.mesh" (a file mesh.bwc) and
//   "model.pw0.mesh.material.3" (a directory mesh/) coexist in one layer
//   directory; the plan's "dots to separators" alone makes mesh both.
//   Ordering and equality compare the text bytewise.
//
// Core guidelines read: I.1 (every derivation is a pure function of its
// arguments), E.27 (a named error for every rule), SL.str.1 and SL.str.2
// (the id owns its text; callers borrow a string_view), Con.5
// (validate_asset_id is constexpr). Performance guidelines read: CACHE.2 (an
// id is one small string; a bounded length keeps a layer path within one
// path buffer); the corpus has no rule on identifiers, recorded rather than
// cited.
namespace bw::content {

inline constexpr std::size_t kMaxAssetIdBytes = 512;
inline constexpr std::size_t kMaxAssetIdSegmentBytes = 255;
inline constexpr char kAssetIdSeparator = '.';
inline constexpr std::string_view kRemasterPrefix = "remaster.";
inline constexpr std::string_view kCookedAssetExtension = ".bwc";

// Named failures.
enum class AssetIdError : std::uint8_t {
  None,
  Empty,
  TooLong,
  // A leading, trailing or doubled separator.
  EmptySegment,
  SegmentTooLong,
  // A byte outside [a-z0-9_-] and the separator.
  InvalidCharacter,
};

[[nodiscard]] constexpr bool is_asset_id_byte(char c) noexcept {
  return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-';
}

namespace detail {
// The rules over any byte sequence; `at(i)` yields byte i as a char.
template <class At>
[[nodiscard]] constexpr AssetIdError validate_asset_id_bytes(std::size_t size, At at) noexcept {
  if (size == 0U) {
    return AssetIdError::Empty;
  }
  if (size > kMaxAssetIdBytes) {
    return AssetIdError::TooLong;
  }
  std::size_t segment = 0;
  for (std::size_t i = 0; i < size; ++i) {
    const char c = at(i);
    if (c == kAssetIdSeparator) {
      if (segment == 0) {
        return AssetIdError::EmptySegment;
      }
      segment = 0;
      continue;
    }
    if (!is_asset_id_byte(c)) {
      return AssetIdError::InvalidCharacter;
    }
    ++segment;
    if (segment > kMaxAssetIdSegmentBytes) {
      return AssetIdError::SegmentTooLong;
    }
  }
  if (segment == 0) {
    return AssetIdError::EmptySegment;
  }
  return AssetIdError::None;
}
} // namespace detail

[[nodiscard]] constexpr AssetIdError validate_asset_id(std::string_view text) noexcept {
  return detail::validate_asset_id_bytes(text.size(), [&](std::size_t i) { return text[i]; });
}
// The same rules over raw bytes, for a parser that holds no char view.
[[nodiscard]] constexpr AssetIdError validate_asset_id(std::span<const std::byte> bytes) noexcept {
  return detail::validate_asset_id_bytes(
      bytes.size(), [&](std::size_t i) { return std::to_integer<char>(bytes[i]); });
}

// Text that validated. The default-constructed id is empty and valid for
// nothing; empty() tells it apart.
class AssetId {
public:
  AssetId() = default;
  [[nodiscard]] std::string_view view() const noexcept { return text_; }
  [[nodiscard]] bool empty() const noexcept { return text_.empty(); }
  [[nodiscard]] friend bool operator==(const AssetId&, const AssetId&) noexcept = default;
  [[nodiscard]] friend std::strong_ordering operator<=>(const AssetId& a,
                                                        const AssetId& b) noexcept {
    return a.text_ <=> b.text_;
  }

private:
  friend struct AssetIdAccess;
  explicit AssetId(std::string text) noexcept : text_(std::move(text)) {}
  std::string text_;
};

struct AssetIdResult {
  AssetId id{};
  AssetIdError error{AssetIdError::None};
  [[nodiscard]] bool ok() const noexcept { return error == AssetIdError::None; }
};

// The id of validated text; no folding.
[[nodiscard]] AssetIdResult make_asset_id(std::string_view text);

// ASCII lower-casing of a qualified name, then validation.
[[nodiscard]] AssetIdResult asset_id_from_qualified_name(std::string_view qualified_name);

// The id of a record with no qualified name (the rule in the contract).
[[nodiscard]] AssetIdResult asset_id_from_container_record(std::string_view container_path,
                                                           std::string_view short_name);

enum class SplitKind : std::uint8_t {
  Material,
  Skeleton,
  Shapes,
};

// "<parent>.material.<index>", "<parent>.skeleton" (index ignored) or
// "<parent>.shapes.<index>"; TooLong when the result exceeds the bound.
[[nodiscard]] AssetIdResult split_asset_id(const AssetId& parent, SplitKind kind,
                                           std::uint32_t index);

[[nodiscard]] bool is_remaster_id(const AssetId& id) noexcept;

// Segments joined by '/' plus kCookedAssetExtension; empty for the empty id.
[[nodiscard]] std::string asset_relative_path(const AssetId& id);

} // namespace bw::content

#endif
