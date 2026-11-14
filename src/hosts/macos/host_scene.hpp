#ifndef BW_HOSTS_MACOS_HOST_SCENE_HPP
#define BW_HOSTS_MACOS_HOST_SCENE_HPP

#include "bw/rendering/graph/builder.hpp"
#include "bw/rendering/graph/declaration.hpp"
#include "bw/rendering/rhi/device.hpp"
#include "bw/rendering/rhi/format.hpp"
#include "bw/rendering/rhi/viewport.hpp"

#include <cstdint>

// Responsibility: what game_host's shell calls on the scene it draws. The
// shell holds one scene and one frame graph (bw/rendering/graph/builder.hpp);
// each frame it imports the back buffer, lets the scene declare its passes,
// compiles the graph and records it into the frame's command list, asks the
// scene whether every pass recorded what it declared, and only then
// executes the list. The scene is destroyed after the
// command list and before the viewport. The cube scene is the host's own;
// the asset scene adapts the engine's model scene
// (bw/rendering/model_scene.hpp), which the character viewer composes too.
// Host-private: a source of the game_host target, included by no test.
namespace bw::hosts {

// What the shell gives a scene of one frame, every value one the loop
// already holds: the seconds into the run; the seconds since the last frame
// drawn (0 on the first); whether this is the first frame drawn; the count
// of frames drawn before it; and the viewport's display report. A frame
// that is not drawn changes none of them, so the frame drawn after it gets
// the values it would have had.
struct HostFrame {
  double elapsed{};
  float delta_seconds{};
  bool first{};
  std::uint32_t frame_index{};
  rhi::DisplayReport display{};
};

class HostScene {
public:
  HostScene() = default;
  HostScene(const HostScene&) = delete;
  HostScene& operator=(const HostScene&) = delete;
  HostScene(HostScene&&) = delete;
  HostScene& operator=(HostScene&&) = delete;
  virtual ~HostScene() = default;

  // The scene's passes for one frame, declared into `graph`, drawing to the
  // imported back buffer `target` of `size` for `frame`. The record
  // functions the passes name are members of the scene, so they live until
  // the graph is executed. False after a failure line on stderr, which ends
  // the run.
  [[nodiscard]] virtual bool declare(rhi::Device& device, graph::FrameGraph& graph,
                                     graph::TextureHandle target, rhi::Extent2 size,
                                     const HostFrame& frame) = 0;

  // After the frame's graph is recorded and before the device executes it:
  // false after a failure line on stderr when a pass recorded less than it
  // declared, in which case the scene has also dropped the frame
  // (frame_dropped) and the shell executes, presents and counts nothing and
  // ends the run. A scene whose passes always record what they declare
  // returns true.
  [[nodiscard]] virtual bool frame_recorded() { return true; }

  // The frame declare declared last was not executed to its end. A scene
  // that keeps nothing from one frame for the next has nothing to drop.
  virtual void frame_dropped() {}
};

} // namespace bw::hosts

#endif
