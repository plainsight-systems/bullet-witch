#ifndef BW_CORE_DIAGNOSTICS_MATH_DIAGNOSTIC_HPP
#define BW_CORE_DIAGNOSTICS_MATH_DIAGNOSTIC_HPP

// Read guidance: F.20 makes numeric return values explicit; this exceptional
// report intentionally returns no value and never owns/mutates the caller.
// Unreal pin f45729c0b6f097038e72b52d366a62baeca3275b:
// CoreMisc.cpp:503–522 selects nonfatal LogCore/Error by default;
// Vector.h:2301–2305 supplies XYZ formatting. Full structured logging is outside
// this leaf dependency. Selected headless output adaptation is explicit in
// docs/architecture/math.md; no injected callback or fake reporter is involved.
namespace bw::diagnostics {

// Format a bounded nonfinite vector Error message and publish to real headless
// stdout. Float components widen exactly at this reporting boundary. Once-only
// ownership is in Vector3<T>::diagnostic_check, not a process-wide logger gate.
// Source keeps values and continues; this must not throw, clamp or terminate.
void log_vector_non_finite(double x, double y, double z);

// Quat.h:588–607,890–894 at the same pin: selected nonfatal headless report
// includes XYZW with nine fractional digits. Report-once remains caller-owned;
// this function preserves input and publishes to the existing real stdout sink.
void log_quaternion_non_finite(double x, double y, double z, double w);

} // namespace bw::diagnostics

#endif // BW_CORE_DIAGNOSTICS_MATH_DIAGNOSTIC_HPP
