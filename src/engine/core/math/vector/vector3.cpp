#include "bw/math/vector/vector3.hpp"

// Read ES.23: initialize constant components explicitly.

// Vector3<float>::zero and Vector3<double>::zero
// Define explicit const specializations with XYZ zero construction; do not
// introduce a singleton accessor or dynamically allocated registry.

namespace bw::math {
template <> const Vector3<float> Vector3<float>::zero{0.0F, 0.0F, 0.0F};
template <> const Vector3<double> Vector3<double>::zero{0.0, 0.0, 0.0};
} // namespace bw::math
