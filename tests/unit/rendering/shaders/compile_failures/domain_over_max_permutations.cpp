// Must not compile. (permutation.hpp, kMaxPermutations): a domain beyond kMaxPermutations
// fails to compile. 1025 values in one dimension.
#include "bw/rendering/shaders/permutation.hpp"

using ProbeDomain = bw::shaders::PermutationDomain<bw::shaders::IntDimension<"PROBE", 1025>>;
static_assert(ProbeDomain::count > 0);
