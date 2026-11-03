// Must not compile. (parameter_block.hpp, BW_PARAMETER_BLOCK_LAYOUT) (it goes
// after the layouts of any nested blocks) and
// (parameter_block.hpp, ParameterTraits): before its layout exists, a nested
// block is not a parameter block and matches no trait.
#include "bw/rendering/shaders/parameter_block.hpp"

struct alignas(16) ProbeInner {
  bw::shaders::Float4 tint{};
};
struct alignas(16) ProbeBlock {
  ProbeInner inner{};
};
BW_PARAMETER_BLOCK_LAYOUT(ProbeBlock, BW_PARAMETER_MEMBER(ProbeBlock, inner));
BW_PARAMETER_BLOCK_LAYOUT(ProbeInner, BW_PARAMETER_MEMBER(ProbeInner, tint));
