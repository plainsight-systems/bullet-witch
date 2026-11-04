#include "bw/rendering/rhi/viewport.hpp"

#include "bw/rendering/rhi/lifetime.hpp"
#include "bw/rendering/rhi/resources.hpp"

namespace bw::rhi {

// The description is copied and its debug name re-pointed at this object's
// own copy (viewport.hpp, Viewport::description), as Buffer and Texture do;
// the back buffer is set by the backend's constructor (set_back_buffer). The
// frame state starts at the description's size with nothing held
// (viewport.hpp, Viewport::frame_state).
Viewport::Viewport(DeferredDeleteQueue& queue, const ViewportDescription& description) noexcept
    : Resource(queue), description_(description), name_(description.debug_name),
      state_{description.pixel_size, 0} {
  description_.debug_name = name_.view();
}

} // namespace bw::rhi
