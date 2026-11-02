// Must not compile. parameter_block.hpp:28-31: every member listed; an
// unlisted member leaves a gap and fails. `hidden` is not listed, so `after`
// does not start where the listed members end.
#include "bw/rendering/shaders/parameter_block.hpp"

struct alignas(16) ProbeBlock {
  bw::shaders::Float4 color{};
  float hidden{};
  float after{};
};
BW_PARAMETER_BLOCK_LAYOUT(ProbeBlock, BW_PARAMETER_MEMBER(ProbeBlock, color),
                          BW_PARAMETER_MEMBER(ProbeBlock, after));
