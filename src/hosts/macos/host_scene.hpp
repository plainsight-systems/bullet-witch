#ifndef BW_HOSTS_MACOS_HOST_SCENE_HPP
#define BW_HOSTS_MACOS_HOST_SCENE_HPP

#include "bw/rendering/graph/builder.hpp"
#include "bw/rendering/graph/declaration.hpp"
#include "bw/rendering/rhi/device.hpp"
#include "bw/rendering/rhi/format.hpp"

// Responsibility: what game_host's shell calls on the scene it draws. The
// shell holds one scene and one frame graph (bw/rendering/graph/builder.hpp);
// each frame it imports the back buffer, lets the scene declare its passes,
// then compiles and executes the graph. The scene is destroyed after the
// command list and before the viewport. The cube scene is the host's own;
// the asset scene adapts the engine's model scene
// (bw/rendering/model_scene.hpp), which the character viewer composes too.
// Host-private: a source of the game_host target, included by no test.
namespace bw::hosts {

class HostScene {
public:
  HostScene() = default;
  HostScene(const HostScene&) = delete;
  HostScene& operator=(const HostScene&) = delete;
  HostScene(HostScene&&) = delete;
  HostScene& operator=(HostScene&&) = delete;
  virtual ~HostScene() = default;

  // The scene's passes for one frame, declared into `graph`, drawing to the
  // imported back buffer `target` of `size` at `elapsed` seconds into the
  // run. The record functions the passes name are members of the scene, so
  // they live until the graph is executed. False after a failure line on
  // stderr, which ends the run.
  [[nodiscard]] virtual bool declare(rhi::Device& device, graph::FrameGraph& graph,
                                     graph::TextureHandle target, rhi::Extent2 size,
                                     double elapsed) = 0;

  // The frame declare declared last was not executed to its end. A scene
  // that keeps nothing from one frame for the next has nothing to drop.
  virtual void frame_dropped() {}
};

} // namespace bw::hosts

#endif
