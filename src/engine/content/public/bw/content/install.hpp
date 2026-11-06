#ifndef BW_CONTENT_INSTALL_HPP
#define BW_CONTENT_INSTALL_HPP
#include "bw/content/vdf.hpp"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// Responsibility: finding the player's original install and deciding that
// it is one (docs/architecture/content.md, invariant 5: located once by
// search, validated by structure, recorded by path, opened read-only; no
// release version is pinned). The pure parts are the candidate roots per
// platform, the two Steam files' meanings and the structural signature;
// locate_install is the one OS-bound function and composes them.
//
// Contract.
//   Search order: an explicit path, when given, is the only candidate and
//   is validated by signature; otherwise Steam discovery: every root
//   candidate whose "steamapps" is a directory, then that root's
//   steamapps/libraryfolders.vdf, each library's
//   steamapps/appmanifest_696180.acf, and the install at
//   <library>/steamapps/common/<installdir>; the first install that passes
//   the signature answers. Nothing is written anywhere.
//   Root candidates (steam_root_candidates), in order, from the caller's
//   environment and never read from the OS here:
//     macOS: <home>/Library/Application Support/Steam; then for each
//       CrossOver bottle name given, <home>/Library/Application
//       Support/CrossOver/Bottles/<bottle>/drive_c/Program Files (x86)/Steam
//       and .../drive_c/Program Files/Steam.
//     Linux: <home>/.var/app/com.valvesoftware.Steam/.local/share/Steam,
//       .../.steam/steam, .../.steam/root; <xdg_data_home or
//       <home>/.local/share>/Steam; <home>/.steam/steam, <home>/.steam/root,
//       <home>/.steam/debian-installation; <snap_user_data or
//       <home>/snap>/steam/common/.local/share/Steam, .../.steam/steam,
//       .../.steam/root.
//       then further locations: <home>/.steam, <home>/.local/share/steam,
//       <home>/.var/app/com.valvesoftware.Steam/data/steam and data/Steam,
//       /usr/share/steam, /usr/local/share/steam.
//     Windows: the registry InstallPath the caller read (HKLM
//       SOFTWARE\Wow6432Node\Valve\Steam, else SOFTWARE\Valve\Steam).
//       Then <program_files_x86>\Steam, the Steam installer's default, as
//       a fallback when the registry gives no path.
//     A CrossOver bottle's library paths are probed as written: its Windows
//     drive paths in libraryfolders.vdf are not mapped onto drive_c and
//     dosdevices, so a bottle library outside drive_c is not found.
//   Library folders: the root key "libraryfolders"; each child whose key
//   parses as an unsigned integer is a library whose "path" child is its
//   root, or, in the old form, whose value is the path (the layout older
//   clients wrote); a child with a
//   "mounted" key other than "1" is dropped at parse, so every returned
//   LibraryFolder is mounted; "contentstatsid" is ignored.
//   App manifest: the root key "AppState" with "appid", "installdir" and
//   "StateFlags"; installdir is a directory name under steamapps/common;
//   StateFlags bit 4 (value 4) is "fully installed", reported, not
//   required; a StateFlags that is absent or not a decimal number reads as
//   0. A manifest failure is named RootMissing, FieldMissing or
//   WrongApp.
//   The Steam files are parsed with escapes on: Steam writes Windows paths
//   with doubled backslashes. The "apps" block of libraryfolders.vdf is not
//   read; each library's manifest file is.
//   Signature (check_install_signature over a listing the caller supplies):
//   every entry of kInstallSignature exists, in order, and the count of
//   ".cpr" files under data/data/stable/graphics (recursive) is at least
//   kMinGraphicsContainers; the first missing entry, by name, or the count
//   is the failure. A probe or a count the OS refuses is ListingFailed
//   with the entry and the OS message; a count cut short by an error is
//   never a pass. No file is hashed and no size is compared.
//   locate_install returns the path, where it came from and the record the
//   converter writes (install_path, graphics_containers), or a failure with
//   every path probed in `tried`: for an explicit path, its failure; for
//   the Steam search over several roots and libraries, the failure of the
//   candidate that got furthest in InstallError order, the most specific
//   one, and detail names its path.
//
// Core guidelines read: I.1 (the environment is an explicit argument),
// F.4 and Con.5 where possible (the candidate list and the signature are
// pure), E.27 (named failures), R.14 (listings are
// callbacks over relative paths, no raw arrays). Performance guidelines
// read: none applies to a once-per-run search; recorded rather than cited.
namespace bw::content {

inline constexpr std::uint32_t kBulletWitchAppId = 696180;
inline constexpr std::string_view kSteamAppsDirectory = "steamapps";
inline constexpr std::string_view kLibraryFoldersFile = "libraryfolders.vdf";
inline constexpr std::string_view kCommonDirectory = "common";
inline constexpr std::uint32_t kStateFlagFullyInstalled = 4;

enum class HostPlatform : std::uint8_t {
  MacOS,
  Linux,
  Windows,
};

// What the candidate list reads; the OS layer fills it, a test fills it
// from fixtures. Empty optionals leave their candidates out.
struct SteamEnvironment {
  std::filesystem::path home{};
  std::optional<std::filesystem::path> xdg_data_home{};
  std::optional<std::filesystem::path> snap_user_data{};
  std::vector<std::string> crossover_bottles{};
  std::optional<std::filesystem::path> program_files_x86{};
  std::optional<std::filesystem::path> registry_install_path{};
};

[[nodiscard]] std::vector<std::filesystem::path>
steam_root_candidates(HostPlatform platform, const SteamEnvironment& environment);

// "appmanifest_<id>.acf".
[[nodiscard]] std::string app_manifest_file_name(std::uint32_t app_id);

struct LibraryFolder {
  std::filesystem::path path{};
  // True for every library the parse returns; unmounted ones are dropped.
  bool mounted{true};
};
enum class SteamFileError : std::uint8_t {
  None,
  // The root key is absent.
  RootMissing,
  // A library has neither a "path" child nor a string value; a manifest
  // lacks installdir.
  FieldMissing,
  // appid present and not kBulletWitchAppId, or not a number.
  WrongApp,
};
struct LibraryFoldersParse {
  std::vector<LibraryFolder> libraries{};
  SteamFileError error{SteamFileError::None};
};
[[nodiscard]] LibraryFoldersParse library_folders_from_vdf(const VdfDocument& document);

struct AppManifest {
  std::uint32_t app_id{};
  std::string install_dir{};
  std::uint64_t state_flags{};
  [[nodiscard]] bool fully_installed() const noexcept {
    return (state_flags & kStateFlagFullyInstalled) != 0U;
  }
};
struct AppManifestParse {
  AppManifest manifest{};
  SteamFileError error{SteamFileError::None};
};
[[nodiscard]] AppManifestParse app_manifest_from_vdf(const VdfDocument& document,
                                                     std::uint32_t expected_app_id);

// <library>/steamapps/common/<install_dir>.
[[nodiscard]] std::filesystem::path install_path_in_library(const std::filesystem::path& library,
                                                            std::string_view install_dir);

// The structural signature, relative to the install root, in check order.
inline constexpr std::string_view kInstallSignature[] = {
    "witch.exe",
    "data/concept",
    "data/data",
    "data/menu",
    "data/movie",
    "data/shader",
    "data/sound",
    "data/text",
    "data/data/stable/stable.rat",
    "data/shader/shaders.cpr",
};
inline constexpr std::string_view kGraphicsDirectory = "data/data/stable/graphics";
inline constexpr std::uint32_t kMinGraphicsContainers = 6000;

// A listing of one candidate root: exists(relative path) and the recursive
// count of ".cpr" files under a relative directory.
// A probe answers present or absent, or carries the OS message when the
// OS refused to answer; a count is complete or carries the OS message.
struct ListingProbe {
  bool present{};
  std::string error{};
};
struct ListingCount {
  std::uint32_t count{};
  std::string error{};
};
struct InstallListing {
  std::function<ListingProbe(std::string_view relative)> exists{};
  std::function<ListingCount(std::string_view relative_directory)> count_containers{};
};
enum class SignatureError : std::uint8_t {
  None,
  EntryMissing,
  TooFewGraphicsContainers,
  // The OS refused a probe or cut a count short; detail is its message.
  ListingFailed,
};
struct SignatureResult {
  SignatureError error{SignatureError::None};
  // The missing or refused entry, or empty.
  std::string_view entry{};
  std::string detail{};
  std::uint32_t graphics_containers{};
  [[nodiscard]] bool ok() const noexcept { return error == SignatureError::None; }
};
[[nodiscard]] SignatureResult check_install_signature(const InstallListing& listing);

enum class InstallSource : std::uint8_t {
  Explicit,
  Steam,
};
enum class InstallError : std::uint8_t {
  None,
  ExplicitPathNotADirectory,
  ExplicitPathSignatureFailed,
  // No candidate root has a steamapps directory.
  NoSteamRoot,
  // A root was found and no libraryfolders.vdf parsed under any root.
  LibraryFoldersUnreadable,
  // No library holds the app's manifest.
  ManifestNotFound,
  // A manifest exists and does not parse or names another app.
  ManifestInvalid,
  // The manifest's install directory does not exist.
  InstallDirectoryMissing,
  // The install directory exists and fails the signature.
  SignatureFailed,
};
struct InstallSearchOptions {
  std::optional<std::filesystem::path> explicit_path{};
  HostPlatform platform{HostPlatform::MacOS};
  SteamEnvironment environment{};
};
struct InstallLocation {
  std::filesystem::path path{};
  InstallSource source{InstallSource::Explicit};
  std::uint32_t graphics_containers{};
  // From the manifest when source is Steam; otherwise false.
  bool fully_installed{};
};
struct InstallLocateResult {
  InstallLocation location{};
  InstallError error{InstallError::None};
  // The failing entry or path, and the OS message when there is one.
  std::string detail{};
  std::vector<std::filesystem::path> tried{};
  [[nodiscard]] bool ok() const noexcept { return error == InstallError::None; }
};

// The OS-bound composition of the functions above over the real file
// system; the only function here that touches it.
[[nodiscard]] InstallLocateResult locate_install(const InstallSearchOptions& options);

// The listing over a real directory, the probe the integration test keeps.
[[nodiscard]] InstallListing directory_listing(const std::filesystem::path& root);

} // namespace bw::content

#endif
