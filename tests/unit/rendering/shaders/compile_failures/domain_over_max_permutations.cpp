// Must not compile. permutation.hpp:74-76: a domain beyond kMaxPermutations
// fails to compile. 1025 values in one dimension.
#include "bw/rendering/shaders/permutation.hpp"

using ProbeDomain = bw::shaders::PermutationDomain<bw::shaders::IntDimension<"PROBE", 1025>>;
static_assert(ProbeDomain::count > 0);
