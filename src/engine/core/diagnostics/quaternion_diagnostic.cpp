#include "bw/diagnostics/math_diagnostic.hpp"
#include "bw/platform/diagnostics/diagnostic_output.hpp"

#include <array>
#include <cstdio>
#include <string_view>

// Read F.20: borrowed numeric values are observed without mutation.
namespace bw::diagnostics {
// Unreal f45729c0b6f097038e72b52d366a62baeca3275b,
// Quat.h:588–607,890–894; CoreMisc.cpp:503–522.
// Selected headless adaptation: bounded formatting and real stdout publication,
// keeping source XYZW nine-fractional-digit output. Once-state belongs to the
// template callsite; full logging/optional ensure UI is not claimed.
void log_quaternion_non_finite(double x, double y, double z, double w) {
  // CoreMisc:518–521: format before publication. Four doubles fit this buffer.
  std::array<char, 4096> message{};
  std::snprintf(message.data(), message.size(),
                "LogCore: Error: Quat contains NaN: X=%.9f Y=%.9f Z=%.9f W=%.9f\n", x, y, z, w);
  // OutputDeviceStdOut:224–245: publish completed text through the selected sink.
  platform::write_diagnostic_line(std::string_view{message.data()});
}
} // namespace bw::diagnostics
