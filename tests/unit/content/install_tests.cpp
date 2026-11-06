// Unit test: the pure contracts of bw/content/install.hpp; each scenario names
// the declarations it asserts. locate_install and directory_listing touch the
// file system and are asserted by tests/integration/content.
#include "bw/content/install.hpp"
#include "bw/content/vdf.hpp"
#include "bw/testing/harness.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace {

using bw::content::HostPlatform;
using bw::content::SignatureError;
using bw::content::SteamEnvironment;
using bw::content::SteamFileError;
using bw::content::VdfDocument;
using bw::testing::require;
using Path = std::filesystem::path;
using Paths = std::vector<Path>;

// (install.hpp, steam_root_candidates): each platform's list in the stated
// order from the caller's environment alone; Linux's XDG and Snap roots fall
// back to their defaults under home; Windows candidates appear only for the
// values given.
void root_candidates_per_platform() {
  SteamEnvironment environment{};
  environment.home = "/h";
  environment.xdg_data_home = Path("/xdg");
  environment.snap_user_data = Path("/snap");
  environment.crossover_bottles = {"Steam", "Games"};
  environment.program_files_x86 = Path("C:/Program Files (x86)");
  environment.registry_install_path = Path("D:/Steam");

  const Path mac_bottles = "/h/Library/Application Support/CrossOver/Bottles";
  const Paths mac{
      "/h/Library/Application Support/Steam",
      mac_bottles / "Steam/drive_c/Program Files (x86)/Steam",
      mac_bottles / "Steam/drive_c/Program Files/Steam",
      mac_bottles / "Games/drive_c/Program Files (x86)/Steam",
      mac_bottles / "Games/drive_c/Program Files/Steam",
  };
  require(bw::content::steam_root_candidates(HostPlatform::MacOS, environment) == mac,
          "macOS: native, then each bottle's two Program Files directories");

  const auto linux_list = [](const Path& xdg, const Path& snap) {
    return Paths{
        "/h/.var/app/com.valvesoftware.Steam/.local/share/Steam",
        "/h/.var/app/com.valvesoftware.Steam/.steam/steam",
        "/h/.var/app/com.valvesoftware.Steam/.steam/root",
        xdg / "Steam",
        "/h/.steam/steam",
        "/h/.steam/root",
        "/h/.steam/debian-installation",
        snap / "steam/common/.local/share/Steam",
        snap / "steam/common/.steam/steam",
        snap / "steam/common/.steam/root",
        "/h/.steam",
        "/h/.local/share/steam",
        "/h/.var/app/com.valvesoftware.Steam/data/steam",
        "/h/.var/app/com.valvesoftware.Steam/data/Steam",
        "/usr/share/steam",
        "/usr/local/share/steam",
    };
  };
  require(bw::content::steam_root_candidates(HostPlatform::Linux, environment) ==
              linux_list("/xdg", "/snap"),
          "Linux: Flatpak, standard, Snap, then the additions, with the given XDG and Snap");
  SteamEnvironment defaults = environment;
  defaults.xdg_data_home.reset();
  defaults.snap_user_data.reset();
  require(bw::content::steam_root_candidates(HostPlatform::Linux, defaults) ==
              linux_list("/h/.local/share", "/h/snap"),
          "Linux: XDG and Snap default under home");

  require(bw::content::steam_root_candidates(HostPlatform::Windows, environment) ==
              Paths{Path("D:/Steam"), Path("C:/Program Files (x86)") / "Steam"},
          "Windows: the registry path, then Program Files (x86)");
  SteamEnvironment no_registry = environment;
  no_registry.registry_install_path.reset();
  require(bw::content::steam_root_candidates(HostPlatform::Windows, no_registry) ==
              Paths{Path("C:/Program Files (x86)") / "Steam"},
          "Windows without a registry value");
  no_registry.program_files_x86.reset();
  require(bw::content::steam_root_candidates(HostPlatform::Windows, no_registry).empty(),
          "Windows with neither value has no candidate");
  SteamEnvironment no_bottles = environment;
  no_bottles.crossover_bottles.clear();
  require(bw::content::steam_root_candidates(HostPlatform::MacOS, no_bottles) ==
              Paths{"/h/Library/Application Support/Steam"},
          "macOS without bottles");
}

VdfDocument steam_file(std::string_view text) {
  const bw::content::VdfParse parse = bw::content::parse_vdf(text, {.escapes = true});
  require(parse.ok(), "fixture parses: " + std::string(text));
  return parse.document;
}

// (install.hpp, library_folders_from_vdf, app_manifest_from_vdf,
// install_path_in_library, app_manifest_file_name): integer-keyed children are
// libraries, new form by "path" and old form by value, unmounted ones skipped,
// other keys ignored; a manifest gives its install directory and flags and
// names a missing root, a missing installdir or another app.
void steam_files_parsed() {
  const auto folders = bw::content::library_folders_from_vdf(steam_file(R"(
"libraryfolders"
{
  "contentstatsid" "-123"
  "0" { "path" "C:\\Program Files (x86)\\Steam" "label" "" "apps" { "228980" "1" } }
  "1" { "path" "/mnt/games" "mounted" "1" }
  "2" { "path" "/mnt/gone" "mounted" "0" }
  "3" "/old/form"
}
)"));
  require(folders.error == SteamFileError::None, "a library file parses");
  require(folders.libraries.size() == 3U, "three libraries: the unmounted one is skipped");
  require(folders.libraries[0].path == Path("C:\\Program Files (x86)\\Steam"),
          "a Windows path with its doubled backslashes unescaped");
  require(folders.libraries[1].path == Path("/mnt/games") && folders.libraries[1].mounted,
          "a mounted library");
  require(folders.libraries[2].path == Path("/old/form"), "the old form's value is the path");
  require(bw::content::library_folders_from_vdf(steam_file("other { }")).error ==
              SteamFileError::RootMissing,
          "no libraryfolders root");
  require(bw::content::library_folders_from_vdf(
              steam_file(R"("LibraryFolders" { "0" { "label" "x" } })"))
                  .error == SteamFileError::FieldMissing,
          "a library with no path");

  const auto manifest = bw::content::app_manifest_from_vdf(
      steam_file(R"("AppState" { "appid" "696180" "installdir" "Bullet Witch" "StateFlags" "4" })"),
      bw::content::kBulletWitchAppId);
  require(manifest.error == SteamFileError::None, "a manifest parses");
  require(manifest.manifest.app_id == bw::content::kBulletWitchAppId &&
              manifest.manifest.install_dir == "Bullet Witch" &&
              manifest.manifest.state_flags == 4U && manifest.manifest.fully_installed(),
          "its fields, fully installed by bit 4");
  struct Row {
    std::string_view text;
    SteamFileError error;
    bool fully_installed;
    std::string_view note;
  };
  const std::array rows{
      Row{R"(appstate { appid 696180 installdir bw StateFlags 6 })", SteamFileError::None, true,
          "keys in any case; 6 has bit 4"},
      Row{R"(AppState { appid 696180 installdir bw StateFlags 1026 })", SteamFileError::None, false,
          "1026 lacks bit 4: reported, not required"},
      Row{R"(AppState { appid 696180 installdir bw })", SteamFileError::None, false,
          "no StateFlags"},
      Row{R"(AppState { appid 696180 installdir bw StateFlags x4 })", SteamFileError::None, false,
          "a StateFlags that is not a decimal number reads as 0"},
      Row{R"(Other { appid 696180 installdir bw })", SteamFileError::RootMissing, false,
          "no AppState root"},
      Row{R"(AppState { appid 696180 })", SteamFileError::FieldMissing, false, "no installdir"},
      Row{R"(AppState { appid 230410 installdir bw })", SteamFileError::WrongApp, false,
          "another app"},
  };
  for (std::size_t i = 0; i < rows.size(); ++i) {
    const auto parsed = bw::content::app_manifest_from_vdf(steam_file(rows[i].text),
                                                           bw::content::kBulletWitchAppId);
    require(parsed.error == rows[i].error, i, rows[i].note);
    require(parsed.error != SteamFileError::None ||
                parsed.manifest.fully_installed() == rows[i].fully_installed,
            i, std::string(rows[i].note) + ": fully installed");
  }

  require(bw::content::install_path_in_library("/lib", "Bullet Witch") ==
              Path("/lib") / "steamapps" / "common" / "Bullet Witch",
          "<library>/steamapps/common/<installdir>");
  require(bw::content::app_manifest_file_name(bw::content::kBulletWitchAppId) ==
              "appmanifest_696180.acf",
          "the manifest's file name");
}

using Entries = std::set<std::string, std::less<>>;

// A listing over a set of present entries, counting `containers` under the
// graphics directory and none under any other. A probe of an entry in
// `refused` carries an OS message; a nonempty `count_error`, which outlives
// the listing, is the count's.
bw::content::InstallListing listing(const Entries& present, std::uint32_t containers,
                                    const Entries& refused = {},
                                    std::string_view count_error = {}) {
  bw::content::InstallListing result{};
  result.exists = [present, refused](std::string_view relative) {
    if (refused.contains(relative)) {
      return bw::content::ListingProbe{false, "probe refused"};
    }
    return bw::content::ListingProbe{present.contains(relative), std::string()};
  };
  result.count_containers = [containers, count_error](std::string_view directory) {
    return bw::content::ListingCount{directory == bw::content::kGraphicsDirectory ? containers : 0U,
                                     std::string(count_error)};
  };
  return result;
}

// (install.hpp, check_install_signature, kInstallSignature,
// kMinGraphicsContainers, ListingProbe, ListingCount, ListingFailed): a tree
// with every entry and at least the minimum container count under the
// graphics directory passes and reports the count; each entry missing alone
// is named; several missing name the first in signature order; the count is
// checked only after every entry; a refused probe or count is ListingFailed
// with the entry and the message, and a refused count is never a pass.
void signature_table() {
  Entries all;
  for (const std::string_view entry : bw::content::kInstallSignature) {
    all.emplace(entry);
  }
  const auto passed =
      bw::content::check_install_signature(listing(all, bw::content::kMinGraphicsContainers));
  require(passed.ok() && passed.entry.empty() &&
              passed.graphics_containers == bw::content::kMinGraphicsContainers,
          "the whole signature at the minimum count");
  const auto counted = bw::content::check_install_signature(listing(all, 6035));
  require(counted.ok() && counted.graphics_containers == 6035U, "the count is reported");

  std::size_t row = 0;
  for (const std::string_view entry : bw::content::kInstallSignature) {
    auto without = all;
    without.erase(std::string(entry));
    const auto result =
        bw::content::check_install_signature(listing(without, bw::content::kMinGraphicsContainers));
    require(result.error == SignatureError::EntryMissing && result.entry == entry, row++,
            "missing " + std::string(entry));
  }
  const auto few =
      bw::content::check_install_signature(listing(all, bw::content::kMinGraphicsContainers - 1U));
  require(few.error == SignatureError::TooFewGraphicsContainers &&
              few.graphics_containers == bw::content::kMinGraphicsContainers - 1U,
          "one container short, with the count reported");

  const auto refused = bw::content::check_install_signature(
      listing(all, bw::content::kMinGraphicsContainers, Entries{"data/movie"}));
  require(refused.error == SignatureError::ListingFailed && refused.entry == "data/movie" &&
              refused.detail == "probe refused",
          "a refused probe names its entry and the message");
  const auto cut_short = bw::content::check_install_signature(
      listing(all, bw::content::kMinGraphicsContainers, Entries{}, "count refused"));
  require(cut_short.error == SignatureError::ListingFailed &&
              cut_short.entry == bw::content::kGraphicsDirectory &&
              cut_short.detail == "count refused",
          "a refused count at the minimum is ListingFailed, never a pass");
}

using Scenario = bw::testing::Scenario<void>;
constexpr std::array kScenarios{
    Scenario{"root_candidates_per_platform", &root_candidates_per_platform},
    Scenario{"steam_files_parsed", &steam_files_parsed},
    Scenario{"signature_table", &signature_table},
};

} // namespace

int main(int argc, char** argv) { return bw::testing::run_scenario(argc, argv, kScenarios); }
