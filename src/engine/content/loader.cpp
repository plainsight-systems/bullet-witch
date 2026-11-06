#include "bw/content/loader.hpp"

#include "file_bytes.hpp"

#include <cstddef>
#include <filesystem>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace bw::content {

Loader::Loader(LayerDirectories layers, LoaderOptions options) noexcept
    : layers_(std::move(layers)), options_(options) {}

LoadResult Loader::load(const AssetId& id, AssetKind kind) const {
  LoadResult result{};
  LoadFailure& failure = result.failure;
  failure.id = id;
  if (validate_asset_id(id.view()) != AssetIdError::None) {
    failure.error = LoadError::InvalidId;
    return result;
  }
  const std::string relative = asset_relative_path(id);
  for (std::size_t i = 0; i < kLayerCount; ++i) {
    if (!layers_.present[i]) {
      continue;
    }
    const auto layer = static_cast<Layer>(i);
    failure.searched = static_cast<LayerMask>(failure.searched | layer_bit(layer));
    const std::filesystem::path path = layers_.paths[i] / relative;
    std::error_code ec;
    const bool exists = std::filesystem::exists(path, ec);
    failure.detail = path.string();
    if (ec) {
      // The OS would not say whether this layer holds the file. Trying the
      // next layer would let it answer for a file this one may hold.
      failure.error = LoadError::ReadFailed;
      failure.detail += ": " + ec.message();
      return result;
    }
    if (!exists) {
      continue;
    }
    // The first layer holding the file answers, whatever its checks say.
    detail::FileRead<std::vector<std::byte>> file =
        detail::read_whole_file<std::vector<std::byte>>(path);
    if (!file.error.empty()) {
      failure.error = LoadError::ReadFailed;
      failure.detail += ": " + file.error;
      return result;
    }
    const CookedParse parse = parse_cooked_header(file.bytes);
    if (!parse.ok()) {
      failure.error = load_error_from(parse.error);
      return result;
    }
    const CookedError expected = check_expectations(file.bytes, parse.header, id.view(), kind);
    if (expected != CookedError::None) {
      failure.error = load_error_from(expected);
      return result;
    }
    if (options_.verify_payload_hash) {
      const CookedError hashed = verify_payload_hash(file.bytes, parse.header);
      if (hashed != CookedError::None) {
        failure.error = load_error_from(hashed);
        return result;
      }
    }
    result.handle.id_ = id;
    result.handle.bytes_ = std::move(file.bytes);
    result.handle.header_ = parse.header;
    failure.detail.clear();
    return result;
  }
  failure.error = LoadError::Missing;
  failure.detail = relative;
  return result;
}

std::string_view load_error_name(LoadError error) noexcept {
  switch (error) {
  case LoadError::None:
    return "None";
  case LoadError::InvalidId:
    return "InvalidId";
  case LoadError::Missing:
    return "Missing";
  case LoadError::ReadFailed:
    return "ReadFailed";
  case LoadError::TooShort:
    return "TooShort";
  case LoadError::BadMagic:
    return "BadMagic";
  case LoadError::VersionMismatch:
    return "VersionMismatch";
  case LoadError::UnknownKind:
    return "UnknownKind";
  case LoadError::FlagsUnknown:
    return "FlagsUnknown";
  case LoadError::IdInvalid:
    return "IdInvalid";
  case LoadError::PayloadMisaligned:
    return "PayloadMisaligned";
  case LoadError::PayloadOutOfBounds:
    return "PayloadOutOfBounds";
  case LoadError::PayloadHashMismatch:
    return "PayloadHashMismatch";
  case LoadError::IdMismatch:
    return "IdMismatch";
  case LoadError::KindMismatch:
    return "KindMismatch";
  }
  return "Unknown";
}

std::string load_failure_text(const LoadFailure& failure) {
  std::string text(load_error_name(failure.error));
  text += " loading ";
  text += failure.id.view();
  text += "; layers searched: ";
  text += layer_mask_text(failure.searched);
  if (!failure.detail.empty()) {
    text += ": ";
    text += failure.detail;
  }
  return text;
}

} // namespace bw::content
