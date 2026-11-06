// Integration test: bw/content/install.hpp's locate_install and
// directory_listing over the player's install, given by path. The signature
// rule itself is the unit table's (unit/content/install_tests.cpp).
#include "bw/content/install.hpp"
#include "bw/testing/harness.hpp"

#include <array>
#include <filesystem>
#include <span>
#include <string_view>

namespace {

namespace fs = std::filesystem;
using bw::testing::require;

struct Arguments {
  // The install root.
  fs::path directory{};
};

// (install.hpp, locate_install, directory_listing, InstallSource): the
// player's install, given as the explicit path, is located by signature and
// reports where it came from and the container count its listing gives.
void explicit_install_located(const Arguments& arguments) {
  bw::content::InstallSearchOptions options{};
  options.explicit_path = arguments.directory;
  const bw::content::InstallLocateResult result = bw::content::locate_install(options);
  require(result.ok(), "the install is located: " + result.detail);
  require(result.location.path == arguments.directory &&
              result.location.source == bw::content::InstallSource::Explicit &&
              !result.location.fully_installed,
          "the explicit path answers as given");
  require(result.location.graphics_containers >= bw::content::kMinGraphicsContainers,
          "at least the minimum container count");
  const bw::content::InstallListing listing = bw::content::directory_listing(arguments.directory);
  const bw::content::ListingCount count = listing.count_containers(bw::content::kGraphicsDirectory);
  require(count.error.empty() && count.count == result.location.graphics_containers,
          "the count is the listing's");
  const bw::content::ListingProbe present = listing.exists("witch.exe");
  const bw::content::ListingProbe absent = listing.exists("no_such_entry");
  require(present.present && present.error.empty() && !absent.present && absent.error.empty(),
          "the listing tells a present entry from an absent one");
}

using Scenario = bw::testing::Scenario<Arguments>;
constexpr std::array kScenarios{
    Scenario{"explicit_install_located", &explicit_install_located},
};

} // namespace

int main(int argc, char** argv) {
  return bw::testing::run_scenario(argc, argv, kScenarios, "<install root>",
                                   [](std::span<char* const> rest, Arguments& arguments) {
                                     if (rest.size() != 1U || std::string_view(rest[0]).empty()) {
                                       return false;
                                     }
                                     arguments.directory = rest[0];
                                     return true;
                                   });
}
