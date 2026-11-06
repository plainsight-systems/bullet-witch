#include "bw/platform/process/executable_directory.hpp"

#include <mach-o/dyld.h>

#include <cstdint>
#include <filesystem>
#include <string>
#include <system_error>

// Responsibility: the macOS implementation of
// bw/platform/process/executable_directory.hpp.
namespace bw::platform {

// The directory of this executable, from the path the OS loaded it from,
// resolved through symbolic links; false with `detail` set when the OS
// gives no path or the path does not resolve.
bool executable_directory(std::filesystem::path& directory, std::string& detail) {
  std::uint32_t size = 1024;
  std::string buffer(size, '\0');
  if (_NSGetExecutablePath(buffer.data(), &size) != 0) {
    // The buffer was too small; `size` is now the size the path needs.
    buffer.assign(size, '\0');
    if (_NSGetExecutablePath(buffer.data(), &size) != 0) {
      detail = "the OS gave no executable path";
      return false;
    }
  }
  buffer.resize(buffer.find('\0'));
  std::error_code error;
  const std::filesystem::path real =
      std::filesystem::canonical(std::filesystem::path(buffer), error);
  if (error) {
    detail = buffer + ": " + error.message();
    return false;
  }
  directory = real.parent_path();
  return true;
}

} // namespace bw::platform
