#ifndef BW_PLATFORM_PROCESS_EXECUTABLE_DIRECTORY_HPP
#define BW_PLATFORM_PROCESS_EXECUTABLE_DIRECTORY_HPP

#include <filesystem>
#include <string>

// Responsibility: where the running executable is. A host that finds files
// beside itself (its default shader directory) asks here, so no host reads
// the OS's executable path or argv[0] itself. The implementation is the
// selected OS's, under src/platform/os/<os>/process.
//
// Contract.
//   executable_directory sets `directory` to the directory that holds the
//   running executable and returns true. The path is the one the OS loaded
//   the executable from, not argv[0], resolved through symbolic links.
//   When the OS gives no path, or the path does not resolve, it returns
//   false, leaves `directory` unchanged and sets `detail` to text for a
//   person naming which of the two failed (and, when the path did not
//   resolve, the path and the OS's message); the wording is not part of
//   the contract.
//
// Read guidance: I.1 (the OS is the only input and it is named), E.27 (the
// failure is returned and described, never a default directory).
namespace bw::platform {

[[nodiscard]] bool executable_directory(std::filesystem::path& directory, std::string& detail);

} // namespace bw::platform

#endif
