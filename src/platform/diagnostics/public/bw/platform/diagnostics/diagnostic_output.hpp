#ifndef BW_PLATFORM_DIAGNOSTIC_OUTPUT_HPP
#define BW_PLATFORM_DIAGNOSTIC_OUTPUT_HPP

#include <string_view>

// Read guidance: F.20: reporting has no hidden out parameter; view lifetime is
// borrowed only during the synchronous call. SIMD guidance does not apply to IO.
// Unreal pin f45729c0b6f097038e72b52d366a62baeca3275b:
// OutputDeviceStdOut.cpp:33–49 writes then flushes text to stdout. This explicit
// headless output choice does not claim full logging device/record machinery.
namespace bw::platform {

// Write the provided complete line and flush stdout. Retains no view/pointer.
// Caller supplies newline. Current selected host uses this concrete output;
// no mutable sink registry or test reporter injection is introduced.
void write_diagnostic_line(std::string_view line);

} // namespace bw::platform

#endif // BW_PLATFORM_DIAGNOSTIC_OUTPUT_HPP
