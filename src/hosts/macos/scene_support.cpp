#include "scene_support.hpp"

#include "bw/rendering/rhi/format.hpp"
#include "bw/rendering/shaders/artifact_file.hpp"
#include "bw/rendering/shaders/permutation.hpp"

#include <cmath>
#include <filesystem>
#include <iostream>
#include <string_view>

namespace bw::hosts {

bool load_shader(const std::filesystem::path& directory, std::string_view name,
                 shaders::ArtifactFile& file) {
  const shaders::ArtifactFileFailure failure =
      shaders::read_artifact_file(directory, name, shaders::PermutationId{}, file);
  if (failure.ok()) {
    return true;
  }
  std::cerr << "game_host: " << shaders::artifact_file_failure_text(failure, name) << '\n';
  return false;
}

float aspect_of(rhi::Extent2 size) {
  return static_cast<float>(size.width) / static_cast<float>(size.height);
}

float turn_angle(double elapsed_seconds, double radians_per_second) {
  return static_cast<float>(std::fmod(elapsed_seconds * radians_per_second, 6.283185307179586));
}

} // namespace bw::hosts
