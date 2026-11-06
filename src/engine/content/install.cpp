#include "bw/content/install.hpp"

#include "file_bytes.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace bw::content {

namespace {

namespace fs = std::filesystem;

constexpr std::string_view kContainerExtension = ".cpr";

// Decimal digits only, at least one, at most `limit`.
[[nodiscard]] std::optional<std::uint64_t> parse_decimal(std::string_view text,
                                                         std::uint64_t limit) noexcept {
  if (text.empty()) {
    return std::nullopt;
  }
  std::uint64_t value = 0;
  for (const char c : text) {
    if (c < '0' || c > '9') {
      return std::nullopt;
    }
    const auto digit = static_cast<std::uint64_t>(c - '0');
    if (value > (limit - digit) / 10U) {
      return std::nullopt;
    }
    value = value * 10U + digit;
  }
  return value;
}

// The type of `path`, with `ec` set only when the OS refused to tell.
// status() also reports a path that does not exist through `ec`; that is a
// known answer (file_type::not_found), not a refusal, so it is cleared.
[[nodiscard]] fs::file_type probe_type(const fs::path& path, std::error_code& ec) {
  const fs::file_status status = fs::status(path, ec);
  if (fs::status_known(status)) {
    ec.clear();
  }
  return status.type();
}

constexpr std::uint64_t kU32Max = 0xFFFFFFFFU;
constexpr std::uint64_t kU64Max = 0xFFFFFFFFFFFFFFFFU;

[[nodiscard]] std::string_view vdf_error_name(VdfError error) noexcept {
  switch (error) {
  case VdfError::None:
    return "None";
  case VdfError::UnexpectedEnd:
    return "UnexpectedEnd";
  case VdfError::EmptyKey:
    return "EmptyKey";
  case VdfError::UnexpectedCloseBrace:
    return "UnexpectedCloseBrace";
  case VdfError::MissingOpenBrace:
    return "MissingOpenBrace";
  case VdfError::ConditionalBeforeValue:
    return "ConditionalBeforeValue";
  case VdfError::UnterminatedString:
    return "UnterminatedString";
  case VdfError::TokenTooLong:
    return "TokenTooLong";
  case VdfError::InvalidEscape:
    return "InvalidEscape";
  case VdfError::NestingTooDeep:
    return "NestingTooDeep";
  case VdfError::DirectiveWithoutArgument:
    return "DirectiveWithoutArgument";
  }
  return "Unknown";
}

[[nodiscard]] std::string_view steam_file_error_name(SteamFileError error) noexcept {
  switch (error) {
  case SteamFileError::None:
    return "None";
  case SteamFileError::RootMissing:
    return "RootMissing";
  case SteamFileError::FieldMissing:
    return "FieldMissing";
  case SteamFileError::WrongApp:
    return "WrongApp";
  }
  return "Unknown";
}

// A Steam file read and parsed with escapes on (Steam writes Windows paths
// with doubled backslashes); `error` names the read or parse failure.
struct SteamFile {
  VdfDocument document{};
  std::string error{};
};

[[nodiscard]] SteamFile read_steam_file(const fs::path& path) {
  SteamFile result{};
  const detail::FileRead<std::string> file = detail::read_whole_file<std::string>(path);
  if (!file.error.empty()) {
    result.error = path.string() + ": " + file.error;
    return result;
  }
  VdfParse parse = parse_vdf(file.bytes, VdfOptions{.escapes = true});
  if (!parse.ok()) {
    result.error = path.string() + ":" + std::to_string(parse.line) + ": " +
                   std::string(vdf_error_name(parse.error));
    return result;
  }
  result.document = std::move(parse.document);
  return result;
}

[[nodiscard]] std::string describe_signature_failure(const fs::path& root,
                                                     const SignatureResult& signature) {
  if (signature.error == SignatureError::EntryMissing) {
    return root.string() + ": missing " + std::string(signature.entry);
  }
  if (signature.error == SignatureError::ListingFailed) {
    return root.string() + ": " + std::string(signature.entry) + ": " + signature.detail;
  }
  return root.string() + ": " + std::to_string(signature.graphics_containers) + " " +
         std::string(kContainerExtension) + " files under " + std::string(kGraphicsDirectory) +
         ", fewer than " + std::to_string(kMinGraphicsContainers);
}

// The Steam search's failures, each a stage further than the last in
// InstallError order. A search over several roots and libraries reports the
// furthest stage any candidate reached, the first such candidate in search
// order, because the earlier stages' meanings ("no root", "no library
// file parsed", "no manifest") hold only when no candidate passed them.
class SteamSearchFailure {
public:
  void record(InstallError error, std::string detail) {
    if (static_cast<unsigned>(error) > static_cast<unsigned>(error_)) {
      error_ = error;
      detail_ = std::move(detail);
    }
  }
  [[nodiscard]] InstallError error() const noexcept { return error_; }
  [[nodiscard]] const std::string& detail() const noexcept { return detail_; }

private:
  InstallError error_{InstallError::None};
  std::string detail_{};
};

} // namespace

std::vector<fs::path> steam_root_candidates(HostPlatform platform,
                                            const SteamEnvironment& environment) {
  std::vector<fs::path> candidates;
  const fs::path& home = environment.home;
  switch (platform) {
  case HostPlatform::MacOS: {
    // Listing the Bottles directory is a file system read, so the caller
    // enumerates the bottles and they are taken in the caller's order.
    candidates.push_back(home / "Library/Application Support/Steam");
    const fs::path bottles = home / "Library/Application Support/CrossOver/Bottles";
    for (const std::string& bottle : environment.crossover_bottles) {
      const fs::path drive_c = bottles / bottle / "drive_c";
      candidates.push_back(drive_c / "Program Files (x86)" / "Steam");
      candidates.push_back(drive_c / "Program Files" / "Steam");
    }
    break;
  }
  case HostPlatform::Linux: {
    // Two candidates may be links to one root. Resolving links is a file
    // system read, and a duplicate root only repeats a probe that
    // locate_install skips for libraries it has seen.
    const fs::path flatpak = home / ".var/app/com.valvesoftware.Steam";
    const fs::path xdg = environment.xdg_data_home.value_or(home / ".local/share");
    const fs::path snap = environment.snap_user_data.value_or(home / "snap");
    candidates.push_back(flatpak / ".local/share/Steam");
    candidates.push_back(flatpak / ".steam/steam");
    candidates.push_back(flatpak / ".steam/root");
    candidates.push_back(xdg / "Steam");
    candidates.push_back(home / ".steam/steam");
    candidates.push_back(home / ".steam/root");
    candidates.push_back(home / ".steam/debian-installation");
    candidates.push_back(snap / "steam/common/.local/share/Steam");
    candidates.push_back(snap / "steam/common/.steam/steam");
    candidates.push_back(snap / "steam/common/.steam/root");
    // The header's further locations.
    candidates.push_back(home / ".steam");
    candidates.push_back(home / ".local/share/steam");
    candidates.push_back(flatpak / "data/steam");
    candidates.push_back(flatpak / "data/Steam");
    candidates.emplace_back("/usr/share/steam");
    candidates.emplace_back("/usr/local/share/steam");
    break;
  }
  case HostPlatform::Windows:
    // The registry read is platform code; the caller passes the InstallPath
    // value it read.
    if (environment.registry_install_path) {
      candidates.push_back(*environment.registry_install_path);
    }
    if (environment.program_files_x86) {
      candidates.push_back(*environment.program_files_x86 / "Steam");
    }
    break;
  }
  return candidates;
}

std::string app_manifest_file_name(std::uint32_t app_id) {
  return "appmanifest_" + std::to_string(app_id) + ".acf";
}

// The "apps" block of each library is not read: it may be stale, and the
// manifest file is the truth.
LibraryFoldersParse library_folders_from_vdf(const VdfDocument& document) {
  LibraryFoldersParse parse{};
  const VdfNode* root = find_root(document, "libraryfolders");
  if (root == nullptr) {
    parse.error = SteamFileError::RootMissing;
    return parse;
  }
  for (const VdfNode& child : root->children) {
    // Only integer keys are libraries; "contentstatsid" and the like are not.
    if (!parse_decimal(child.key, kU32Max)) {
      continue;
    }
    if (!child.has_children) {
      // The old form: the value is the library's path.
      if (child.value.empty()) {
        parse.error = SteamFileError::FieldMissing;
        parse.libraries.clear();
        return parse;
      }
      parse.libraries.push_back(LibraryFolder{fs::path(child.value), true});
      continue;
    }
    // An unmounted library is skipped, whatever else it holds.
    const VdfNode* mounted = find_child(child, "mounted");
    if (mounted != nullptr && (mounted->has_children || mounted->value != "1")) {
      continue;
    }
    const VdfNode* path = find_child(child, "path");
    if (path == nullptr || path->has_children || path->value.empty()) {
      parse.error = SteamFileError::FieldMissing;
      parse.libraries.clear();
      return parse;
    }
    parse.libraries.push_back(LibraryFolder{fs::path(path->value), true});
  }
  return parse;
}

AppManifestParse app_manifest_from_vdf(const VdfDocument& document, std::uint32_t expected_app_id) {
  AppManifestParse parse{};
  const VdfNode* root = find_root(document, "AppState");
  if (root == nullptr) {
    parse.error = SteamFileError::RootMissing;
    return parse;
  }
  const VdfNode* install_dir = find_child(*root, "installdir");
  if (install_dir == nullptr || install_dir->has_children || install_dir->value.empty()) {
    parse.error = SteamFileError::FieldMissing;
    return parse;
  }
  // StateFlags is reported, not required: absent, a block or not a decimal
  // number reads as 0, so it never decides whether an install is found.
  const VdfNode* state_flags = find_child(*root, "StateFlags");
  std::uint64_t flags = 0;
  if (state_flags != nullptr && !state_flags->has_children) {
    flags = parse_decimal(state_flags->value, kU64Max).value_or(0U);
  }
  // An absent appid is tolerated (the header names only a present, wrong
  // one); the manifest then reports app id 0, "not stated".
  const VdfNode* app_id = find_child(*root, "appid");
  std::uint32_t id = 0;
  if (app_id != nullptr) {
    const std::optional<std::uint64_t> value =
        app_id->has_children ? std::nullopt : parse_decimal(app_id->value, kU32Max);
    if (!value || *value != expected_app_id) {
      parse.error = SteamFileError::WrongApp;
      return parse;
    }
    id = static_cast<std::uint32_t>(*value);
  }
  parse.manifest = AppManifest{id, install_dir->value, flags};
  return parse;
}

fs::path install_path_in_library(const fs::path& library, std::string_view install_dir) {
  return library / kSteamAppsDirectory / kCommonDirectory / fs::path(install_dir);
}

SignatureResult check_install_signature(const InstallListing& listing) {
  SignatureResult result{};
  for (const std::string_view entry : kInstallSignature) {
    ListingProbe probe = listing.exists(entry);
    if (!probe.error.empty()) {
      result.error = SignatureError::ListingFailed;
      result.entry = entry;
      result.detail = std::move(probe.error);
      return result;
    }
    if (!probe.present) {
      result.error = SignatureError::EntryMissing;
      result.entry = entry;
      return result;
    }
  }
  ListingCount count = listing.count_containers(kGraphicsDirectory);
  if (!count.error.empty()) {
    // A count cut short is never compared with the minimum: what it saw is
    // not the tree's count.
    result.error = SignatureError::ListingFailed;
    result.entry = kGraphicsDirectory;
    result.detail = std::move(count.error);
    return result;
  }
  result.graphics_containers = count.count;
  if (result.graphics_containers < kMinGraphicsContainers) {
    result.error = SignatureError::TooFewGraphicsContainers;
  }
  return result;
}

InstallListing directory_listing(const fs::path& root) {
  InstallListing listing{};
  // Shared so that copying a listing's callbacks cannot throw.
  const auto shared_root = std::make_shared<const fs::path>(root);
  // An OS refusal is carried as its message, never read as absence.
  listing.exists = [shared_root](std::string_view relative) {
    std::error_code ec;
    const bool present = fs::exists(*shared_root / fs::path(relative), ec);
    if (ec) {
      return ListingProbe{false, ec.message()};
    }
    return ListingProbe{present, std::string()};
  };
  // A directory that cannot be opened, stops listing part way, or holds an
  // entry whose type cannot be read is an error with no count: a partial
  // count is never reported as the count.
  listing.count_containers = [shared_root](std::string_view relative_directory) {
    std::uint32_t count = 0;
    std::error_code ec;
    fs::recursive_directory_iterator it(*shared_root / fs::path(relative_directory), ec);
    for (; !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
      const bool regular = it->is_regular_file(ec);
      if (ec) {
        break;
      }
      if (regular && it->path().extension() == kContainerExtension) {
        ++count;
      }
    }
    if (ec) {
      return ListingCount{0, ec.message()};
    }
    return ListingCount{count, std::string()};
  };
  return listing;
}

InstallLocateResult locate_install(const InstallSearchOptions& options) {
  InstallLocateResult result{};
  std::error_code ec;
  if (options.explicit_path) {
    const fs::path& path = *options.explicit_path;
    result.tried.push_back(path);
    if (!fs::is_directory(path, ec)) {
      result.error = InstallError::ExplicitPathNotADirectory;
      result.detail = path.string() + (ec ? ": " + ec.message() : std::string());
      return result;
    }
    const SignatureResult signature = check_install_signature(directory_listing(path));
    if (!signature.ok()) {
      result.error = InstallError::ExplicitPathSignatureFailed;
      result.detail = describe_signature_failure(path, signature);
      return result;
    }
    result.location =
        InstallLocation{path, InstallSource::Explicit, signature.graphics_containers, false};
    return result;
  }

  SteamSearchFailure failure;
  bool any_root = false;
  bool any_library_file = false;
  std::vector<fs::path> probed_libraries;
  for (const fs::path& root : steam_root_candidates(options.platform, options.environment)) {
    const fs::path steamapps = root / kSteamAppsDirectory;
    result.tried.push_back(steamapps);
    const bool steamapps_found = probe_type(steamapps, ec) == fs::file_type::directory;
    if (ec) {
      // The OS would not say whether this root has a steamapps directory: the
      // root is reported as unreadable, never passed over as absent.
      any_root = true;
      failure.record(InstallError::LibraryFoldersUnreadable,
                     steamapps.string() + ": " + ec.message());
      continue;
    }
    if (!steamapps_found) {
      continue;
    }
    any_root = true;
    const fs::path library_file = steamapps / kLibraryFoldersFile;
    result.tried.push_back(library_file);
    const SteamFile folders_file = read_steam_file(library_file);
    if (!folders_file.error.empty()) {
      failure.record(InstallError::LibraryFoldersUnreadable, folders_file.error);
      continue;
    }
    const LibraryFoldersParse folders = library_folders_from_vdf(folders_file.document);
    if (folders.error != SteamFileError::None) {
      failure.record(InstallError::LibraryFoldersUnreadable,
                     library_file.string() + ": " +
                         std::string(steam_file_error_name(folders.error)));
      continue;
    }
    any_library_file = true;
    // A Windows drive path in a CrossOver bottle's libraryfolders.vdf
    // ("C:\...") is not mapped to the bottle's drive_c or dosdevices
    // directory: a CrossOver library is probed at its path as written, and on
    // macOS the explicit path is the route that reaches such an install.
    for (const LibraryFolder& library : folders.libraries) {
      bool seen = false;
      for (const fs::path& probed : probed_libraries) {
        seen = seen || probed == library.path;
      }
      if (seen) {
        continue;
      }
      probed_libraries.push_back(library.path);
      const fs::path manifest_path =
          library.path / kSteamAppsDirectory / app_manifest_file_name(kBulletWitchAppId);
      result.tried.push_back(manifest_path);
      const bool manifest_found = probe_type(manifest_path, ec) == fs::file_type::regular;
      if (ec) {
        failure.record(InstallError::ManifestInvalid, manifest_path.string() + ": " + ec.message());
        continue;
      }
      if (!manifest_found) {
        failure.record(InstallError::ManifestNotFound, manifest_path.string());
        continue;
      }
      const SteamFile manifest_file = read_steam_file(manifest_path);
      if (!manifest_file.error.empty()) {
        failure.record(InstallError::ManifestInvalid, manifest_file.error);
        continue;
      }
      const AppManifestParse manifest =
          app_manifest_from_vdf(manifest_file.document, kBulletWitchAppId);
      if (manifest.error != SteamFileError::None) {
        failure.record(InstallError::ManifestInvalid,
                       manifest_path.string() + ": " +
                           std::string(steam_file_error_name(manifest.error)));
        continue;
      }
      const fs::path install = install_path_in_library(library.path, manifest.manifest.install_dir);
      result.tried.push_back(install);
      const bool install_found = probe_type(install, ec) == fs::file_type::directory;
      if (ec || !install_found) {
        failure.record(InstallError::InstallDirectoryMissing,
                       ec ? install.string() + ": " + ec.message() : install.string());
        continue;
      }
      const SignatureResult signature = check_install_signature(directory_listing(install));
      if (!signature.ok()) {
        failure.record(InstallError::SignatureFailed,
                       describe_signature_failure(install, signature));
        continue;
      }
      result.location =
          InstallLocation{install, InstallSource::Steam, signature.graphics_containers,
                          manifest.manifest.fully_installed()};
      return result;
    }
  }
  if (!any_root) {
    result.error = InstallError::NoSteamRoot;
    return result;
  }
  // A library file parsed and listed no library: no library holds the manifest.
  if (any_library_file) {
    failure.record(InstallError::ManifestNotFound, std::string());
  }
  result.error = failure.error();
  result.detail = failure.detail();
  return result;
}

} // namespace bw::content
