#include "bw/content/layers.hpp"

#include "bw/content/cooked_asset.hpp"
#include "file_bytes.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

namespace bw::content {

namespace {

// The record's keys in their required order.
constexpr std::array<std::string_view, 3> kRecordKeys{"format_version", "install_path",
                                                      "graphics_containers"};
constexpr std::size_t kFormatVersionKey = 0;
constexpr std::size_t kInstallPathKey = 1;
constexpr std::size_t kGraphicsContainersKey = 2;

// Decimal digits only, at least one, within u32.
[[nodiscard]] std::optional<std::uint32_t> parse_decimal_u32(std::string_view text) noexcept {
  if (text.empty()) {
    return std::nullopt;
  }
  std::uint64_t value = 0;
  for (const char c : text) {
    if (c < '0' || c > '9') {
      return std::nullopt;
    }
    value = value * 10U + static_cast<std::uint64_t>(c - '0');
    if (value > 0xFFFFFFFFU) {
      return std::nullopt;
    }
  }
  return static_cast<std::uint32_t>(value);
}

[[nodiscard]] std::string_view cache_record_error_name(CacheRecordError error) noexcept {
  switch (error) {
  case CacheRecordError::None:
    return "None";
  case CacheRecordError::UnknownLine:
    return "UnknownLine";
  case CacheRecordError::DuplicateKey:
    return "DuplicateKey";
  case CacheRecordError::MissingKey:
    return "MissingKey";
  case CacheRecordError::BadValue:
    return "BadValue";
  case CacheRecordError::VersionMismatch:
    return "VersionMismatch";
  }
  return "Unknown";
}

} // namespace

CacheRecordParse parse_converted_cache_record(std::string_view text) {
  CacheRecordParse parse{};
  std::array<bool, kRecordKeys.size()> seen{};
  std::size_t next_key = 0;
  std::size_t line = 0;
  std::size_t at = 0;
  while (at < text.size()) {
    ++line;
    const std::size_t end = text.find('\n', at);
    if (end == std::string_view::npos) {
      // Every line ends with '\n'; trailing bytes without one are not a line
      // of the form, and accepting them would accept a truncated value.
      parse.error = CacheRecordError::UnknownLine;
      parse.line = line;
      return parse;
    }
    const std::string_view current = text.substr(at, end - at);
    at = end + 1U;
    const std::size_t equals = current.find('=');
    if (equals == std::string_view::npos) {
      parse.error = CacheRecordError::UnknownLine;
      parse.line = line;
      return parse;
    }
    const std::string_view key = current.substr(0, equals);
    const std::string_view value = current.substr(equals + 1U);
    std::size_t index = kRecordKeys.size();
    for (std::size_t k = 0; k < kRecordKeys.size(); ++k) {
      if (kRecordKeys[k] == key) {
        index = k;
      }
    }
    if (index == kRecordKeys.size()) {
      parse.error = CacheRecordError::UnknownLine;
      parse.line = line;
      return parse;
    }
    if (seen[index]) {
      parse.error = CacheRecordError::DuplicateKey;
      parse.line = line;
      return parse;
    }
    if (index != next_key) {
      parse.error = CacheRecordError::UnknownLine;
      parse.line = line;
      return parse;
    }
    seen[index] = true;
    ++next_key;
    if (index == kInstallPathKey) {
      parse.record.install_path = std::string(value);
      continue;
    }
    const std::optional<std::uint32_t> number = parse_decimal_u32(value);
    if (!number) {
      parse.error = CacheRecordError::BadValue;
      parse.line = line;
      return parse;
    }
    if (index == kFormatVersionKey) {
      parse.record.format_version = *number;
    } else if (index == kGraphicsContainersKey) {
      parse.record.graphics_containers = *number;
    }
  }
  if (next_key != kRecordKeys.size()) {
    parse.error = CacheRecordError::MissingKey;
    parse.line = 0;
    return parse;
  }
  if (parse.record.format_version != kCookedFormatVersion) {
    parse.error = CacheRecordError::VersionMismatch;
    parse.line = 0;
  }
  return parse;
}

std::string encode_converted_cache_record(const ConvertedCacheRecord& record) {
  std::string text;
  text += kRecordKeys[kFormatVersionKey];
  text += '=';
  text += std::to_string(record.format_version);
  text += '\n';
  text += kRecordKeys[kInstallPathKey];
  text += '=';
  text += record.install_path;
  text += '\n';
  text += kRecordKeys[kGraphicsContainersKey];
  text += '=';
  text += std::to_string(record.graphics_containers);
  text += '\n';
  return text;
}

OpenLayersResult open_layers(const std::optional<std::filesystem::path>& overrides,
                             const std::optional<std::filesystem::path>& published,
                             const std::filesystem::path& converted) {
  OpenLayersResult result{};
  const auto fail = [&result](LayerError error, std::string detail) {
    result.error = error;
    result.detail = std::move(detail);
    return result;
  };
  // A directory the OS answers for is either a directory or the layer's
  // NotADirectory failure; one it refuses to answer for is kept, the first
  // in layer order, and reported as LayerDirectoryUnreadable once no
  // NotADirectory failure, which comes earlier in LayerError order, applies.
  std::string refused_directory;
  const auto is_layer_directory = [&refused_directory](const std::filesystem::path& path) {
    // status() also reports a path that does not exist through the
    // error_code; that is a known answer (not a directory), not a refusal.
    std::error_code probe_ec;
    const std::filesystem::file_status status = std::filesystem::status(path, probe_ec);
    const bool directory = status.type() == std::filesystem::file_type::directory;
    if (probe_ec && !std::filesystem::status_known(status)) {
      if (refused_directory.empty()) {
        refused_directory = path.string() + ": " + probe_ec.message();
      }
      return true;
    }
    return directory;
  };
  if (overrides && !is_layer_directory(*overrides)) {
    return fail(LayerError::OverridesNotADirectory, overrides->string());
  }
  if (published && !is_layer_directory(*published)) {
    return fail(LayerError::PublishedNotADirectory, published->string());
  }
  if (!is_layer_directory(converted)) {
    return fail(LayerError::ConvertedNotADirectory, converted.string());
  }
  if (!refused_directory.empty()) {
    return fail(LayerError::LayerDirectoryUnreadable, std::move(refused_directory));
  }
  std::error_code ec;
  const std::filesystem::path record_path = converted / kConvertedCacheRecordFile;
  const bool record_exists = std::filesystem::exists(record_path, ec);
  if (ec) {
    // The OS would not say whether the record is there: that is a record
    // that cannot be read, not one that is missing.
    return fail(LayerError::ConvertedRecordUnreadable, record_path.string() + ": " + ec.message());
  }
  if (!record_exists) {
    return fail(LayerError::ConvertedRecordMissing, record_path.string());
  }
  const detail::FileRead<std::string> file = detail::read_whole_file<std::string>(record_path);
  if (!file.error.empty()) {
    return fail(LayerError::ConvertedRecordUnreadable, record_path.string() + ": " + file.error);
  }
  const CacheRecordParse parse = parse_converted_cache_record(file.bytes);
  if (!parse.ok()) {
    // The path, the record's line (0 when the failure has none) and the
    // error's name.
    return fail(parse.error == CacheRecordError::VersionMismatch
                    ? LayerError::ConvertedRecordVersionMismatch
                    : LayerError::ConvertedRecordInvalid,
                record_path.string() + ":" + std::to_string(parse.line) + ": " +
                    std::string(cache_record_error_name(parse.error)));
  }
  const auto set_layer = [&result](Layer layer, const std::filesystem::path& path) {
    const auto index = static_cast<std::size_t>(layer);
    result.layers.paths[index] = path;
    result.layers.present[index] = true;
  };
  if (overrides) {
    set_layer(Layer::LocalOverrides, *overrides);
  }
  if (published) {
    set_layer(Layer::PublishedCache, *published);
  }
  set_layer(Layer::ConvertedOriginals, converted);
  result.record = parse.record;
  return result;
}

std::string_view layer_error_name(LayerError error) noexcept {
  switch (error) {
  case LayerError::None:
    return "None";
  case LayerError::OverridesNotADirectory:
    return "OverridesNotADirectory";
  case LayerError::PublishedNotADirectory:
    return "PublishedNotADirectory";
  case LayerError::ConvertedNotADirectory:
    return "ConvertedNotADirectory";
  case LayerError::LayerDirectoryUnreadable:
    return "LayerDirectoryUnreadable";
  case LayerError::ConvertedRecordMissing:
    return "ConvertedRecordMissing";
  case LayerError::ConvertedRecordUnreadable:
    return "ConvertedRecordUnreadable";
  case LayerError::ConvertedRecordInvalid:
    return "ConvertedRecordInvalid";
  case LayerError::ConvertedRecordVersionMismatch:
    return "ConvertedRecordVersionMismatch";
  }
  return "Unknown";
}

std::string layer_mask_text(LayerMask mask) {
  std::string names;
  for (std::size_t i = 0; i < kLayerCount; ++i) {
    const auto layer = static_cast<Layer>(i);
    if ((mask & layer_bit(layer)) != 0U) {
      if (!names.empty()) {
        names += ", ";
      }
      names += layer_name(layer);
    }
  }
  return names.empty() ? std::string("none") : names;
}

} // namespace bw::content
