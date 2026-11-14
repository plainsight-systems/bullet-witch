#ifndef BW_RENDERING_POSTPROCESS_POST_SUPPORT_HPP
#define BW_RENDERING_POSTPROCESS_POST_SUPPORT_HPP

#include "bw/rendering/rhi/device.hpp"
#include "bw/rendering/rhi/format.hpp"
#include "bw/rendering/rhi/pipeline_states.hpp"
#include "bw/rendering/rhi/resources.hpp"
#include "bw/rendering/shaders/artifact_file.hpp"

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

// Responsibility: what the module's passes share when they are made and
// recorded: reading an artifact, making a compute pipeline and a full-screen
// raster pipeline from artifacts, the clamped bilinear sampler, and the
// block bytes a uniform buffer is made with. Not a public header: nothing
// outside the postprocess module's directory includes it.
namespace bw::postprocess::detail {

// The outcome of a making step: empty text on success; otherwise what failed,
// for the caller's detail, and which kind of step it was.
enum class MakeStep : std::uint8_t { None, Artifact, State, Pipeline };
struct MakeFailure {
  MakeStep step{MakeStep::None};
  std::string detail{};
  [[nodiscard]] bool ok() const noexcept { return step == MakeStep::None; }
};

// The text of a failed RHI result: what was made, the code and the detail.
template <class Code>
[[nodiscard]] std::string result_text(std::string_view what, Code code, std::string_view detail) {
  return std::string(what) + ": error " + std::to_string(static_cast<unsigned>(code)) + ": " +
         std::string(detail);
}

// A compute pipeline from the artifact of `shader` (permutation 0), named
// after the shader.
[[nodiscard]] MakeFailure make_compute_pipeline(rhi::Device& device,
                                                const std::filesystem::path& shader_directory,
                                                std::string_view shader,
                                                rhi::Ref<rhi::ComputePipeline>& out);

// A raster pipeline of the module's vertex shader (post_shaders.hpp,
// kFullscreenVertexShader) and the pixel shader `pixel`, writing one colour
// target of `format`, no depth, no blending, no culling.
[[nodiscard]] MakeFailure make_fullscreen_pipeline(rhi::Device& device,
                                                   const std::filesystem::path& shader_directory,
                                                   std::string_view pixel, rhi::Format format,
                                                   std::string_view debug_name,
                                                   rhi::Ref<rhi::GraphicsPipeline>& out);

// The sampler every raster pass of the module reads with: bilinear, clamped
// at the edge on all three axes, no mips.
[[nodiscard]] MakeFailure make_clamped_sampler(rhi::Device& device, rhi::Ref<rhi::Sampler>& out);

} // namespace bw::postprocess::detail

#endif
