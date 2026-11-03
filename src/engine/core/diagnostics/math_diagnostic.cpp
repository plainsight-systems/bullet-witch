#include "bw/diagnostics/math_diagnostic.hpp"

#include "bw/platform/diagnostics/diagnostic_output.hpp"

#include <array>
#include <cstdio>
#include <string_view>

// Read F.20/ES.23: diagnostics borrow numeric inputs and do not change them.

// log_vector_non_finite(double x, double y, double z)
// Format XYZ with three fractional digits into bounded 4096-character storage,
// prefix LogCore/Error, append newline and call platform::write_diagnostic_line.
// The values are kept: no exception, clamp or process exit.
// Once-only state belongs to the diagnostic callsite, not this function.

namespace bw::diagnostics {
void log_vector_non_finite(double x, double y, double z) {
  // Bounded formatting precedes publication. This ASCII text needs no wide/UTF
  // conversion; every double fits the 4096 buffer.
  std::array<char, 4096> message{};
  std::snprintf(message.data(), message.size(),
                "LogCore: Error: Vector contains nonfinite data: X=%3.3f Y=%3.3f Z=%3.3f\n", x, y,
                z);
  // Publish the completed line to the concrete sink.
  platform::write_diagnostic_line(std::string_view{message.data()});
}
} // namespace bw::diagnostics
