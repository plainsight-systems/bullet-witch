#ifndef BW_HOSTS_MACOS_CUBE_SCENE_HPP
#define BW_HOSTS_MACOS_CUBE_SCENE_HPP

#include "host_scene.hpp"

#include "bw/math/matrix/matrix4.hpp"
#include "bw/rendering/graph/builder.hpp"
#include "bw/rendering/graph/declaration.hpp"
#include "bw/rendering/pulled_cube_fixture.hpp"
#include "bw/rendering/rhi/device.hpp"
#include "bw/rendering/rhi/format.hpp"
#include "bw/rendering/rhi/resources.hpp"
#include "bw/rendering/shaders/artifact_file.hpp"

#include <filesystem>

// Responsibility: game_host's draw without --asset: the fixture cube
// (bw/rendering/pulled_cube_fixture.hpp) in one raster pass declared on the
// host's frame graph, clearing the back buffer. The matrices are made by
// bw/math/matrix/matrix4.hpp and bw/rendering/scene/view.hpp; the scene
// states the cube's camera, composes the cube's turn with its view, and
// writes no matrix element, resource description or pass description.
// Host-private: a source of the game_host target, included by no test.
namespace bw::hosts {

// Everything the cube draw holds: the two artifacts, the cube fixture
// (bw/rendering/pulled_cube_fixture.hpp), the frame's matrix and the pass's
// record function.
struct Scene : HostScene {
  // Records the cube with the frame's matrix inside the pass the graph
  // opened.
  struct Record {
    Scene* scene;
    void operator()(graph::RecordContext& context) const;
  };

  shaders::ArtifactFile vertex{};
  shaders::ArtifactFile pixel{};
  rendering::PulledCube cube{};
  math::Matrix4f world_to_clip{};
  Record record{this};

  // One raster pass that clears `target` and draws the cube; always true.
  [[nodiscard]] bool declare(rhi::Device& device, graph::FrameGraph& graph,
                             graph::TextureHandle target, rhi::Extent2 size,
                             double elapsed) override;
};

// Loads the artifacts and makes the cube fixture for the viewport's format.
// Prints the failure line and returns false on any failure.
bool build_scene(rhi::Device& device, const std::filesystem::path& shader_directory,
                 rhi::Format target_format, Scene& scene);

} // namespace bw::hosts

#endif
