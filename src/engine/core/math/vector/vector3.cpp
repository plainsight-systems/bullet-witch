#include "bw/math/vector/vector3.hpp"

// Read ES.23: initialize constant components explicitly.

// Vector3<float>::zero and Vector3<double>::zero
// Unreal f45729c0b6f097038e72b52d366a62baeca3275b,
// Private/Math/UnrealMath.cpp:33,44 and Vector.h:1484–1489.
// Define explicit const specializations with XYZ zero construction, as source
// does; do not introduce a singleton accessor or dynamically allocated registry.
// The source's constexpr TVectorConstInit constructor at Vector.h:1478–1482 is
// not used by these definitions; do not claim it is their selected algorithm.

namespace bw::math {
template <> const Vector3<float> Vector3<float>::zero{0.0F, 0.0F, 0.0F};
template <> const Vector3<double> Vector3<double>::zero{0.0, 0.0, 0.0};
} // namespace bw::math
