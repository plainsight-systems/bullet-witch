#include "bw/content/asset_id.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>

namespace bw::content {

// The one place an AssetId is constructed from text; every path below
// validates the text first.
struct AssetIdAccess {
  [[nodiscard]] static AssetId make(std::string text) noexcept { return AssetId(std::move(text)); }
};

namespace {

[[nodiscard]] char fold_ascii(char c) noexcept {
  if (c >= 'A' && c <= 'Z') {
    return static_cast<char>(c - 'A' + 'a');
  }
  return c;
}

[[nodiscard]] AssetIdResult validated(std::string text) {
  const AssetIdError error = validate_asset_id(text);
  if (error != AssetIdError::None) {
    return AssetIdResult{AssetId{}, error};
  }
  return AssetIdResult{AssetIdAccess::make(std::move(text)), AssetIdError::None};
}

constexpr std::string_view kContainerExtension = ".cpr";

} // namespace

AssetIdResult make_asset_id(std::string_view text) { return validated(std::string(text)); }

AssetIdResult asset_id_from_qualified_name(std::string_view qualified_name) {
  std::string text(qualified_name);
  for (char& c : text) {
    c = fold_ascii(c);
  }
  return validated(std::move(text));
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
AssetIdResult asset_id_from_container_record(std::string_view container_path,
                                             std::string_view short_name) {
  std::string text;
  text.reserve(container_path.size() + 1U + short_name.size());
  for (const char c : container_path) {
    text.push_back(c == '/' || c == '\\' ? kAssetIdSeparator : fold_ascii(c));
  }
  if (text.ends_with(kContainerExtension)) {
    text.resize(text.size() - kContainerExtension.size());
  }
  text.push_back(kAssetIdSeparator);
  // The short name loses the text from its last '.'; a name with no '.' is
  // taken whole. It folds like the path, so the whole id is lower-case.
  const std::size_t extension = short_name.rfind('.');
  const std::string_view stem =
      extension == std::string_view::npos ? short_name : short_name.substr(0, extension);
  for (const char c : stem) {
    text.push_back(fold_ascii(c));
  }
  return validated(std::move(text));
}

AssetIdResult split_asset_id(const AssetId& parent, SplitKind kind, std::uint32_t index) {
  std::string text(parent.view());
  switch (kind) {
  case SplitKind::Material:
    text += ".material.";
    text += std::to_string(index);
    break;
  case SplitKind::Skeleton:
    text += ".skeleton";
    break;
  case SplitKind::Shapes:
    text += ".shapes.";
    text += std::to_string(index);
    break;
  }
  return validated(std::move(text));
}

bool is_remaster_id(const AssetId& id) noexcept { return id.view().starts_with(kRemasterPrefix); }

std::string asset_relative_path(const AssetId& id) {
  if (id.empty()) {
    return {};
  }
  std::string path(id.view());
  for (char& c : path) {
    if (c == kAssetIdSeparator) {
      c = '/';
    }
  }
  path += kCookedAssetExtension;
  return path;
}

} // namespace bw::content
