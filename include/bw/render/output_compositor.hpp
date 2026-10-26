// Final output composition is a separate authority from world, movie and UI
// producers. The renderer consumes an immutable request, orders world output,
// movie and submitted UI, and retires native consumers before returning true.
// False means no display target was available: retain producer resources for
// retry. Errors throw. No world tick, audio service, producer mutation or
// intermediate CPU image copy belongs here. Presentation time is supplied by
// the engine; it may be absent only while there is no world output. Finite
// opacity in [0,1] applies to all three siblings. One main-thread owner; no
// overlapping producer updates or draws. Cost: O(draws + submitted UI); the
// request itself is fixed-size and owns no scene/resource storage.
// Both design and implementation retrieved Core R.1 (#rr-raii), I.11
// (#ri-raw), performance COPY.5 (guidelines/copy-move/
// COPY.5-resource-owning-types-are-move-only.md), and GPU.1
// (guidelines/gpu/GPU.1-keep-data-on-device.md): stable resource owners,
// explicit borrowed encoders, no intermediate CPU copies. Native ARC handles
// and budgeted allocations release automatically after synchronous completion.
#pragma once
#include <optional>

#include "bw/timing/frame_clock.hpp"
namespace bw::render {
struct OutputFrame {
  float opacity{1.0F};
  std::optional<timing::FrameTiming> timing;
};
class OutputCompositor {
 public:
  OutputCompositor() = default;
  virtual ~OutputCompositor() = default;
  OutputCompositor(const OutputCompositor&) = delete;
  OutputCompositor& operator=(const OutputCompositor&) = delete;
  OutputCompositor(OutputCompositor&&) = delete;
  OutputCompositor& operator=(OutputCompositor&&) = delete;
  virtual bool present(const OutputFrame& frame) = 0;
};
}  // namespace bw::render
