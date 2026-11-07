#ifndef BW_HOSTS_MACOS_SCENE_SUPPORT_HPP
#define BW_HOSTS_MACOS_SCENE_SUPPORT_HPP

#include "bw/rendering/rhi/format.hpp"
#include "bw/rendering/shaders/artifact_file.hpp"

#include <filesystem>
#include <string_view>

// Responsibility: what game_host's scenes share: the target's aspect, the
// angle of a turn and loading a shader artifact with its failure line.
// Host-private: a source of the game_host target, included by no test.
namespace bw::hosts {

// Permutation 0 of a registry shader from `directory`
// (bw/rendering/shaders/artifact_file.hpp). Prints the failure line and
// returns false on any failure.
bool load_shader(const std::filesystem::path& directory, std::string_view name,
                 shaders::ArtifactFile& file);

float aspect_of(rhi::Extent2 size);

// The angle of a turn at a fixed rate, within one revolution.
float turn_angle(double elapsed_seconds, double radians_per_second);

} // namespace bw::hosts

#endif
