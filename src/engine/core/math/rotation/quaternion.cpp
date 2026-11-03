#include "bw/math/rotation/quaternion.hpp"

// Per-precision const specializations use the ordinary four-component constructor,
// including its diagnostic; no singleton, heap registry or replacement constructor.
namespace bw::math {
template <> const Quaternion4<float> Quaternion4<float>::identity{0.F, 0.F, 0.F, 1.F};
template <> const Quaternion4<double> Quaternion4<double>::identity{0., 0., 0., 1.};
} // namespace bw::math
