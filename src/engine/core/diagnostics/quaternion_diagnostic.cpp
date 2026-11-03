#include "bw/diagnostics/math_diagnostic.hpp"
#include "bw/platform/diagnostics/diagnostic_output.hpp"

#include <array>
#include <cstdio>
#include <string_view>

// Read F.20: borrowed numeric values are observed without mutation.
namespace bw::diagnostics {
// Bounded formatting and real stdout publication of XYZW with nine fractional
// digits. Once-state belongs to the template callsite.
void log_quaternion_non_finite(double x, double y, double z, double w) {
  // Format before publication. Four doubles fit this buffer.
  std::array<char, 4096> message{};
  std::snprintf(message.data(), message.size(),
                "LogCore: Error: Quat contains NaN: X=%.9f Y=%.9f Z=%.9f W=%.9f\n", x, y, z, w);
  // Publish completed text through the selected sink.
  platform::write_diagnostic_line(std::string_view{message.data()});
}
} // namespace bw::diagnostics
