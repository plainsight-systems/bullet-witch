// Positive control for the compile probes (tests/CMakeLists.txt): a block that
// meets every precondition of parameter_block.hpp:25-32, a nested block whose
// layout precedes its use, and a domain at kMaxPermutations
// (permutation.hpp:74-76). It must compile with the same flags the failing
// probes use, so a probe's failure is its own fault and not the setup's.
#include "bw/rendering/shaders/parameter_block.hpp"
#include "bw/rendering/shaders/permutation.hpp"

struct alignas(16) ProbeInner {
  bw::shaders::Float3 tint{};
  float weight{};
};
BW_PARAMETER_BLOCK_LAYOUT(ProbeInner, BW_PARAMETER_MEMBER(ProbeInner, tint),
                          BW_PARAMETER_MEMBER(ProbeInner, weight));

struct alignas(16) ProbeBlock {
  bw::shaders::Float4 color{};
  ProbeInner inner{};
  float scale{};
};
BW_PARAMETER_BLOCK_LAYOUT(ProbeBlock, BW_PARAMETER_MEMBER(ProbeBlock, color),
                          BW_PARAMETER_MEMBER(ProbeBlock, inner),
                          BW_PARAMETER_MEMBER(ProbeBlock, scale));

using ProbeDomain = bw::shaders::PermutationDomain<bw::shaders::IntDimension<"PROBE", 1024>>;
static_assert(ProbeDomain::count == bw::shaders::kMaxPermutations);
static_assert(bw::shaders::BlockLayoutOf<ProbeBlock>::value.size == sizeof(ProbeBlock));
