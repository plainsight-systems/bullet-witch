#include "core/diagnostics/math_diagnostic.hpp"

#include "platform/diagnostic_output.hpp"

#include <array>
#include <cstdio>
#include <string_view>

// Read F.20/ES.23: diagnostics borrow numeric inputs and do not change them.

// log_vector_non_finite(double x, double y, double z)
// Unreal f45729c0b6f097038e72b52d366a62baeca3275b,
// CoreMisc::InternalLogNANDiagnosticMessage :516–522;
// TVector::ToString Vector.h:2301–2305; StructuredLog::DispatchLogRecord :1420–1436.
// Source formats bounded text and publishes LogCore/Error, keeping the value.
// Selected headless adaptation: format XYZ with three fractional digits
// into bounded 4096-character storage, prefix LogCore/Error, append newline and
// call platform::write_diagnostic_line. No exception, clamp or process exit.
// Source formatting/dispatch is described here; the full structured logging,
// configurable devices, timestamps and optional ensure mode are not claimed.
// Once-only state belongs to the source diagnostic callsite, not this function.

namespace bw::diagnostics {
void log_vector_non_finite(double x, double y, double z) {
  // CoreMisc.cpp:518–521: bounded formatting precedes publication. This selected
  // ASCII text needs no wide/UTF conversion; every double fits the 4096 buffer.
  std::array<char, 4096> message{};
  std::snprintf(message.data(), message.size(),
                "LogCore: Error: Vector contains nonfinite data: X=%3.3f Y=%3.3f Z=%3.3f\n", x, y,
                z);
  // OutputDeviceStdOut.cpp:224–245: publish the completed line to concrete sink.
  platform::write_diagnostic_line(std::string_view{message.data()});
}
} // namespace bw::diagnostics
