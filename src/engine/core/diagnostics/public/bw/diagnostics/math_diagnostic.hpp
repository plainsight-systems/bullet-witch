#ifndef BW_CORE_DIAGNOSTICS_MATH_DIAGNOSTIC_HPP
#define BW_CORE_DIAGNOSTICS_MATH_DIAGNOSTIC_HPP

// Read guidance: F.20 makes numeric return values explicit; this exceptional
// report intentionally returns no value and never owns/mutates the caller.
// Reports are nonfatal LogCore/Error lines. The headless output is described in
// docs/architecture/math.md; no injected callback or fake reporter is involved.
namespace bw::diagnostics {

// Format a bounded nonfinite vector Error message and publish to real headless
// stdout. Float components widen exactly at this reporting boundary. Once-only
// ownership is in Vector3<T>::diagnostic_check, not a process-wide logger gate.
// Values are kept and execution continues; this must not throw, clamp or terminate.
void log_vector_non_finite(double x, double y, double z);

// Nonfatal headless report of XYZW with nine fractional digits. Report-once
// remains caller-owned; this function preserves input and publishes to the
// existing real stdout sink.
void log_quaternion_non_finite(double x, double y, double z, double w);

} // namespace bw::diagnostics

#endif // BW_CORE_DIAGNOSTICS_MATH_DIAGNOSTIC_HPP
