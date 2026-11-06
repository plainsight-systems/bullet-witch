#include "bw/content/install.hpp"
#include "bw/content_converter/conversion.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <iostream>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

// Responsibility: the content_converter executable: the command line
// (conversion.hpp, parse_convert_arguments), the install search
// (install.hpp, locate_install) over the environment this process reads,
// the run and its report on standard output.
//
// Exit codes (conversion.hpp): 0 when nothing failed; 1 when any asset
// failed, after the report names every one; 2 on a usage error, an install
// that does not locate or validate, a run error (InstallNotLocated,
// OutputNotWritable, CatalogUnreadable) or a CrossOver bottle listing the
// OS refuses. A run error after the jobs ran (the cache record's write)
// prints the report first, then the error. ConvertResult carries no flag
// for "the jobs ran"; run_conversion fills the report only after them, so
// a report that is not the empty one is that mark.
//
// The environment the Steam search reads is gathered here, the one place
// the tool touches process state: HOME, XDG_DATA_HOME, SNAP_USER_DATA and
// ProgramFiles(x86), and on macOS the CrossOver bottle names listed under
// the user's CrossOver directory. The Windows registry value is not read:
// it is platform code this tool does not hold, so on Windows the search
// tries Program Files (x86) only and --install names any other install.
namespace {

namespace fs = std::filesystem;
namespace cc = bw::content_converter;
namespace content = bw::content;

std::string_view usage_error_name(cc::UsageError error) noexcept {
  static constexpr std::array<std::string_view, 8> kNames{
      "None",          "UnknownCommand", "UnknownOption", "MissingValue", "DuplicateOption",
      "OutputMissing", "BadNumber",      "BadNormalGreen"};
  const auto index = static_cast<std::size_t>(error);
  return index < kNames.size() ? kNames[index] : std::string_view("Unknown");
}

std::string_view convert_error_name(cc::ConvertError error) noexcept {
  static constexpr std::array<std::string_view, 4> kNames{"None", "InstallNotLocated",
                                                          "OutputNotWritable", "CatalogUnreadable"};
  const auto index = static_cast<std::size_t>(error);
  return index < kNames.size() ? kNames[index] : std::string_view("Unknown");
}

std::string_view install_error_name(content::InstallError error) noexcept {
  static constexpr std::array<std::string_view, 9> kNames{"None",
                                                          "ExplicitPathNotADirectory",
                                                          "ExplicitPathSignatureFailed",
                                                          "NoSteamRoot",
                                                          "LibraryFoldersUnreadable",
                                                          "ManifestNotFound",
                                                          "ManifestInvalid",
                                                          "InstallDirectoryMissing",
                                                          "SignatureFailed"};
  const auto index = static_cast<std::size_t>(error);
  return index < kNames.size() ? kNames[index] : std::string_view("Unknown");
}

std::optional<fs::path> environment_path(const char* name) {
  const char* value = std::getenv(name);
  if (value == nullptr || *value == '\0') {
    return std::nullopt;
  }
  return fs::path(value);
}

content::HostPlatform host_platform() noexcept {
#if defined(__APPLE__)
  return content::HostPlatform::MacOS;
#elif defined(_WIN32)
  return content::HostPlatform::Windows;
#else
  return content::HostPlatform::Linux;
#endif
}

struct BottleListing {
  std::vector<std::string> bottles{};
  // The path and OS message when the listing failed; empty otherwise.
  std::string error{};
};

// The bottle directory names, sorted; none when the directory is absent.
// Any other failure (the directory refused, an entry's type probe, the
// iteration) is the error, never a shorter list.
BottleListing crossover_bottles(const fs::path& home) {
  BottleListing listing;
  const fs::path root = home / "Library" / "Application Support" / "CrossOver" / "Bottles";
  std::error_code ec;
  fs::directory_iterator it(root, ec);
  if (ec == std::errc::no_such_file_or_directory) {
    return listing;
  }
  for (const fs::directory_iterator end{}; !ec && it != end; it.increment(ec)) {
    std::error_code type_ec;
    const bool directory = it->is_directory(type_ec);
    if (type_ec) {
      listing.error = it->path().generic_string() + ": " + type_ec.message();
      return listing;
    }
    if (directory) {
      listing.bottles.push_back(it->path().filename().string());
    }
  }
  if (ec) {
    listing.error = root.generic_string() + ": " + ec.message();
    return listing;
  }
  std::sort(listing.bottles.begin(), listing.bottles.end());
  return listing;
}

// The report carries something only once the jobs ran (run_conversion).
bool jobs_ran(const cc::ConversionReport& report) noexcept {
  return report.converted != 0U || report.skipped_unchanged != 0U || report.skipped_by_kind != 0U ||
         !report.failed.empty() || !report.unmapped.empty() || report.bytes_written != 0U ||
         report.seconds > 0.0;
}

struct SearchSetup {
  content::InstallSearchOptions search{};
  std::string error{};
};

SearchSetup search_options(const cc::ConvertOptions& options) {
  SearchSetup setup;
  content::InstallSearchOptions& search = setup.search;
  search.explicit_path = options.install;
  search.platform = host_platform();
  content::SteamEnvironment& environment = search.environment;
  environment.home = environment_path("HOME").value_or(fs::path());
  environment.xdg_data_home = environment_path("XDG_DATA_HOME");
  environment.snap_user_data = environment_path("SNAP_USER_DATA");
  environment.program_files_x86 = environment_path("ProgramFiles(x86)");
  if (search.platform == content::HostPlatform::MacOS && !options.install.has_value() &&
      !environment.home.empty()) {
    BottleListing listing = crossover_bottles(environment.home);
    if (!listing.error.empty()) {
      setup.error = std::move(listing.error);
      return setup;
    }
    environment.crossover_bottles = std::move(listing.bottles);
  }
  return setup;
}

int run(std::span<char* const> argv) {
  std::vector<std::string_view> arguments;
  arguments.reserve(argv.size());
  for (char* const argument : argv.subspan(1)) {
    arguments.emplace_back(argument);
  }
  const cc::ConvertParse parse = cc::parse_convert_arguments(arguments);
  if (!parse.ok()) {
    std::cerr << "content_converter: " << usage_error_name(parse.error);
    if (!parse.argument.empty()) {
      std::cerr << " '" << parse.argument << "'";
    }
    std::cerr << "\nusage: " << cc::kConvertUsage << '\n';
    return 2;
  }
  const SearchSetup setup = search_options(parse.options);
  if (!setup.error.empty()) {
    std::cerr << "content_converter: CrossOver bottles unreadable: " << setup.error << '\n';
    return 2;
  }
  const content::InstallLocateResult located = content::locate_install(setup.search);
  if (!located.ok()) {
    std::cerr << "content_converter: install " << install_error_name(located.error) << ": "
              << located.detail << '\n';
    for (const fs::path& tried : located.tried) {
      std::cerr << "  tried " << tried.generic_string() << '\n';
    }
    return 2;
  }
  std::cout << "install: " << located.location.path.generic_string() << '\n';
  const cc::ConvertResult result = cc::run_conversion(parse.options, located.location);
  if (!result.ok()) {
    if (jobs_ran(result.report)) {
      std::cout << cc::render_report(result.report) << std::flush;
    }
    std::cerr << "content_converter: " << convert_error_name(result.error) << ": " << result.detail
              << '\n';
    return 2;
  }
  std::cout << cc::render_report(result.report);
  return result.report.failed.empty() ? 0 : 1;
}

} // namespace

int main(int argc, char** argv) {
  try {
    return run(std::span<char* const>(argv, static_cast<std::size_t>(argc)));
  } catch (const std::exception& error) {
    std::cerr << "content_converter: " << error.what() << '\n';
    return 2;
  }
}
