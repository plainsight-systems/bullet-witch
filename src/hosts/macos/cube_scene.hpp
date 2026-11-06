#ifndef BW_HOSTS_MACOS_CUBE_SCENE_HPP
#define BW_HOSTS_MACOS_CUBE_SCENE_HPP

#include "host_scene.hpp"

#include "bw/rendering/pulled_cube_fixture.hpp"
#include "bw/rendering/rhi/command_list.hpp"
#include "bw/rendering/rhi/device.hpp"
#include "bw/rendering/rhi/format.hpp"
#include "bw/rendering/rhi/resources.hpp"
#include "bw/rendering/shaders/artifact_file.hpp"

#include <filesystem>

// Responsibility: game_host's draw without --asset: the fixture cube
// (bw/rendering/pulled_cube_fixture.hpp) in a pass begun by
// bw/rendering/target_pass.hpp. The matrices are made by
// bw/math/matrix/matrix4.hpp and bw/rendering/scene/view.hpp; the scene
// states the cube's camera, composes the cube's turn with its view, and
// writes no matrix element, resource description or pass description.
// Host-private: a source of the game_host target, included by no test.
namespace bw::hosts {

// Everything the cube draw holds: the two artifacts and the cube fixture
// (bw/rendering/pulled_cube_fixture.hpp).
struct Scene : HostScene {
  shaders::ArtifactFile vertex{};
  shaders::ArtifactFile pixel{};
  rendering::PulledCube cube{};

  // The cube's commands into the list between the back buffer's two
  // transitions; always true.
  [[nodiscard]] bool record(rhi::Device& device, rhi::CommandList& list, rhi::Texture& back_buffer,
                            rhi::Extent2 size, double elapsed) override;
};

// Loads the artifacts and makes the cube fixture for the viewport's format.
// Prints the failure line and returns false on any failure.
bool build_scene(rhi::Device& device, const std::filesystem::path& shader_directory,
                 rhi::Format target_format, Scene& scene);

} // namespace bw::hosts

#endif
