#ifndef BW_HOSTS_MACOS_HOST_FAILURE_HPP
#define BW_HOSTS_MACOS_HOST_FAILURE_HPP

#include <iostream>
#include <string_view>

// Responsibility: game_host's failure line for an error code. Host-private:
// a source of the game_host target, included by no test.
namespace bw::hosts {

// One line on stderr: what failed, the enumerator's number, the detail.
template <class Code> int fail(std::string_view what, Code code, std::string_view detail) {
  std::cerr << "game_host: " << what << " failed: error " << static_cast<unsigned>(code);
  if (!detail.empty()) {
    std::cerr << ": " << detail;
  }
  std::cerr << '\n';
  return 1;
}

} // namespace bw::hosts

#endif
