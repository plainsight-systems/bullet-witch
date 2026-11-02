// Must not compile. parameter_block.hpp:27: a block is alignof 16 and its
// sizeof a multiple of 16. Without alignas(16) this block is 8 bytes.
#include "bw/rendering/shaders/parameter_block.hpp"

struct ProbeBlock {
  float a{};
  float b{};
};
BW_PARAMETER_BLOCK_LAYOUT(ProbeBlock, BW_PARAMETER_MEMBER(ProbeBlock, a),
                          BW_PARAMETER_MEMBER(ProbeBlock, b));
