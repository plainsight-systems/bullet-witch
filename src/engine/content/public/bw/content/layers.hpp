#ifndef BW_CONTENT_LAYERS_HPP
#define BW_CONTENT_LAYERS_HPP

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

// Responsibility: the three layers and their resolution order
// (docs/architecture/content.md, invariant 3: local overrides, then the
// published cache, then converted originals; the first layer that holds the
// id answers; invariant 6: the game requires a converted-originals cache that
// records the install it was built from, and without one fails by name).
// A layer is a directory of cooked assets at asset_relative_path(id). This
// header holds the pure parts: the order, the presence rule and the
// converted cache's record; open_layers is the one OS probe.
//
// Contract.
//   Layer enumerators are in resolution order and index every per-layer
//   array. first_present(presence) is the lowest index whose entry is true,
//   or nullopt; the loader asks each present layer's directory whether the
//   file exists, in this order, and stops at the first.
//   LayerMask: bit layer_bit(l) set for every layer that was searched; a
//   missing asset reports it (invariant 10).
//   The converted cache record is the text file kConvertedCacheRecordFile in
//   the converted layer's directory, lines "key=value" ended by '\n', keys
//   in this order and each exactly once: format_version (decimal
//   kCookedFormatVersion), install_path (the path the converter was given,
//   verbatim; may hold any byte but '\n'), graphics_containers (decimal
//   count the converter saw). parse_converted_cache_record names the first
//   failure and the 1-based line; encode writes the same form, so the two
//   round-trip. VersionMismatch is reported after the whole file parsed.
//   open_layers: the converted directory must exist, hold a record that
//   parses and carries kCookedFormatVersion; an overrides or published
//   directory, when given, must exist. A failure is returned
//   as a LayerError with the path in `detail`; a directory probe the OS
//   refuses is LayerDirectoryUnreadable with the path and the OS message,
//   never read as absence. Nothing is created.
//
// Core guidelines read: Enum.3 (the order is the enumeration), I.1 (the
// directories are explicit arguments, never a setting read here), E.27 (a
// named error per failure), SL.str.1 (the record owns
// its strings). Performance guidelines read: none applies to a three-entry
// table; recorded rather than cited.
namespace bw::content {

enum class Layer : std::uint8_t {
  LocalOverrides = 0,
  PublishedCache = 1,
  ConvertedOriginals = 2,
};
inline constexpr std::size_t kLayerCount = 3;

[[nodiscard]] constexpr std::string_view layer_name(Layer layer) noexcept {
  switch (layer) {
  case Layer::LocalOverrides:
    return "local_overrides";
  case Layer::PublishedCache:
    return "published_cache";
  case Layer::ConvertedOriginals:
    return "converted_originals";
  }
  return "unknown";
}

using LayerMask = std::uint8_t;
[[nodiscard]] constexpr LayerMask layer_bit(Layer layer) noexcept {
  return static_cast<LayerMask>(1U << static_cast<unsigned>(layer));
}

[[nodiscard]] constexpr std::optional<Layer>
first_present(const std::array<bool, kLayerCount>& presence) noexcept {
  for (std::size_t i = 0; i < kLayerCount; ++i) {
    if (presence[i]) {
      return static_cast<Layer>(i);
    }
  }
  return std::nullopt;
}

// A set of layer directories; a layer with present false is not searched.
// The converted layer is always present after open_layers succeeds.
struct LayerDirectories {
  std::array<std::filesystem::path, kLayerCount> paths{};
  std::array<bool, kLayerCount> present{};
};

inline constexpr std::string_view kConvertedCacheRecordFile = "converted_cache.txt";

struct ConvertedCacheRecord {
  std::uint32_t format_version{};
  std::string install_path{};
  std::uint32_t graphics_containers{};
  [[nodiscard]] friend bool operator==(const ConvertedCacheRecord&,
                                       const ConvertedCacheRecord&) = default;
};

enum class CacheRecordError : std::uint8_t {
  None,
  // A line without '=' or with an unknown key, or a key out of order.
  UnknownLine,
  DuplicateKey,
  MissingKey,
  // A number that does not parse as decimal u32.
  BadValue,
  VersionMismatch,
};

struct CacheRecordParse {
  ConvertedCacheRecord record{};
  CacheRecordError error{CacheRecordError::None};
  // 1-based line of the failure; 0 for MissingKey and VersionMismatch.
  std::size_t line{};
  [[nodiscard]] bool ok() const noexcept { return error == CacheRecordError::None; }
};

[[nodiscard]] CacheRecordParse parse_converted_cache_record(std::string_view text);
[[nodiscard]] std::string encode_converted_cache_record(const ConvertedCacheRecord& record);

enum class LayerError : std::uint8_t {
  None,
  OverridesNotADirectory,
  PublishedNotADirectory,
  ConvertedNotADirectory,
  // The OS refused to say whether a layer directory exists.
  LayerDirectoryUnreadable,
  ConvertedRecordMissing,
  ConvertedRecordUnreadable,
  ConvertedRecordInvalid,
  ConvertedRecordVersionMismatch,
};

// The enumerator's name as written above, for a failure line.
[[nodiscard]] std::string_view layer_error_name(LayerError error) noexcept;

// The names of the layers in `mask` in search order, separated by ", ";
// "none" for an empty mask.
[[nodiscard]] std::string layer_mask_text(LayerMask mask);

struct OpenLayersResult {
  LayerDirectories layers{};
  ConvertedCacheRecord record{};
  LayerError error{LayerError::None};
  // The path concerned and, for a record failure, its line and error name.
  std::string detail{};
  [[nodiscard]] bool ok() const noexcept { return error == LayerError::None; }
};

[[nodiscard]] OpenLayersResult open_layers(const std::optional<std::filesystem::path>& overrides,
                                           const std::optional<std::filesystem::path>& published,
                                           const std::filesystem::path& converted);

} // namespace bw::content

#endif
