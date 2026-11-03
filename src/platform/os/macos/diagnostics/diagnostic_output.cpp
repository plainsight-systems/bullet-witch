#include "bw/platform/diagnostics/diagnostic_output.hpp"

#include <cstdio>

// Read F.20: the borrowed line remains valid for this synchronous call only.

// write_diagnostic_line(std::string_view line)
// Write the complete supplied line to actual stdout, then fflush stdout.
// Our selected headless host supplies UTF-8 text directly; there is no
// structured record/redirector framework, sink callback or retained reference.

namespace bw::platform {
void write_diagnostic_line(std::string_view line) {
  std::fwrite(line.data(), 1, line.size(), stdout);
  std::fflush(stdout);
}
} // namespace bw::platform
