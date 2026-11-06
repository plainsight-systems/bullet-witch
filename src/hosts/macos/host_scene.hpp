#ifndef BW_HOSTS_MACOS_HOST_SCENE_HPP
#define BW_HOSTS_MACOS_HOST_SCENE_HPP

#include "bw/rendering/rhi/command_list.hpp"
#include "bw/rendering/rhi/device.hpp"
#include "bw/rendering/rhi/format.hpp"
#include "bw/rendering/rhi/resources.hpp"

// Responsibility: what game_host's shell calls on the scene it draws. The
// shell holds one scene, records it once per frame between the back
// buffer's two transitions, and destroys it after the command list and
// before the viewport. Host-private: a source of the game_host target,
// included by no test.
namespace bw::hosts {

class HostScene {
public:
  HostScene() = default;
  HostScene(const HostScene&) = delete;
  HostScene& operator=(const HostScene&) = delete;
  HostScene(HostScene&&) = delete;
  HostScene& operator=(HostScene&&) = delete;
  virtual ~HostScene() = default;

  // The scene's commands for one frame into `list`, drawing to
  // `back_buffer` of `size` at `elapsed` seconds into the run. False after
  // a failure line on stderr, which ends the run.
  [[nodiscard]] virtual bool record(rhi::Device& device, rhi::CommandList& list,
                                    rhi::Texture& back_buffer, rhi::Extent2 size,
                                    double elapsed) = 0;
};

} // namespace bw::hosts

#endif
