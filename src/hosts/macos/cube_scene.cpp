#include "cube_scene.hpp"

#include "scene_support.hpp"

#include "bw/math/matrix/matrix4.hpp"
#include "bw/math/rotation/quaternion.hpp"
#include "bw/math/vector/vector3.hpp"
#include "bw/rendering/geometry/pulled_mesh_fixture.hpp"
#include "bw/rendering/pulled_cube_fixture.hpp"
#include "bw/rendering/rhi/command_list.hpp"
#include "bw/rendering/rhi/device.hpp"
#include "bw/rendering/rhi/format.hpp"
#include "bw/rendering/rhi/resources.hpp"
#include "bw/rendering/scene/view.hpp"
#include "bw/rendering/target_pass.hpp"

#include <filesystem>
#include <iostream>

namespace bw::hosts {

namespace {

// The cube's camera: 40 degrees top to bottom, +y up.
constexpr float kVerticalFov = 40.0F * 3.14159265358979F / 180.0F;

// The cube turning about a tilted axis, seen by a camera at (3.0, 2.4, 4.2)
// looking at the origin, near 0.1, far 100. The cube's bounding sphere
// stays inside the view at every angle, so nothing clips. Faces toward the
// camera are counter-clockwise on screen, which is what the pipeline's cull
// of clockwise faces keeps.
math::Matrix4f cube_world_to_clip(double elapsed_seconds, rhi::Extent2 size) {
  const math::Quaternion4f turn = math::Quaternion4f::from_axis_angle(
      math::Vector3f{0.3F, 1.0F, 0.2F}.safe_normal(), turn_angle(elapsed_seconds, 0.9));
  bw::scene::PerspectiveView view{};
  view.eye = math::Vector3f{3.0F, 2.4F, 4.2F};
  view.vertical_fov = kVerticalFov;
  view.near_plane = 0.1F;
  view.far_plane = 100.0F;
  return math::Matrix4f::rotation_translation(turn, math::Vector3f::zero) *
         bw::scene::world_to_clip(view, aspect_of(size));
}

} // namespace

bool build_scene(rhi::Device& device, const std::filesystem::path& shader_directory,
                 rhi::Format target_format, Scene& scene) {
  if (!load_shader(shader_directory, geometry::kPulledMeshShader, scene.vertex) ||
      !load_shader(shader_directory, geometry::kVertexColorShader, scene.pixel)) {
    return false;
  }
  const rendering::PulledCubeFailure cube = rendering::create_pulled_cube(
      device, scene.vertex.artifact, scene.pixel.artifact, target_format, scene.cube);
  if (!cube.ok()) {
    std::cerr << "game_host: the cube draw failed: error " << static_cast<unsigned>(cube.error)
              << ": " << cube.detail << '\n';
    return false;
  }
  return true;
}

bool Scene::record(rhi::Device& /*device*/, rhi::CommandList& list, rhi::Texture& back_buffer,
                   rhi::Extent2 size, double elapsed) {
  rendering::begin_cleared_pass(list, back_buffer, nullptr, size, "game_host frame");
  rendering::record_pulled_cube(list, cube, cube_world_to_clip(elapsed, size));
  list.end_render_pass();
  return true;
}

} // namespace bw::hosts
