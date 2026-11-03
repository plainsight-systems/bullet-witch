// Must not compile. (parameter_block.hpp, ParameterTraits) and
// (parameter_block.hpp, Float3): alignment lives in the value types; a member of a type not in the
// value set, here a plain three-float struct with alignment 4 instead of Float3's 16, fails to
// match a trait.
#include "bw/rendering/shaders/parameter_block.hpp"

struct PlainFloat3 {
  float x{};
  float y{};
  float z{};
};
struct alignas(16) ProbeBlock {
  PlainFloat3 position{};
  float scale{};
};
BW_PARAMETER_BLOCK_LAYOUT(ProbeBlock, BW_PARAMETER_MEMBER(ProbeBlock, position),
                          BW_PARAMETER_MEMBER(ProbeBlock, scale));
