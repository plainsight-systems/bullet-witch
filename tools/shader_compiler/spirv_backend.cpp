#include "backends.hpp"

#include <spirv-tools/libspirv.hpp>
#include <spirv-tools/optimizer.hpp>
#include <spirv_reflect.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// Responsibility: the SPIR-V back end of backends.hpp: optimization, debug
// stripping and validation through SPIRV-Tools (docs/architecture/shaders.md,
// Dependencies: "SPIR-V optimization"), reflection through SPIRV-Reflect.
//
// Contract: as backends.hpp states. A module whose byte size is not a
// multiple of four is a CompileError. SPIRV-Reflect's module is destroyed on
// every path.
//
// Algorithm:
//   - validate_spirv: spvtools::SpirvTools::Validate for SPV_ENV_VULKAN_1_3
//     with scalar block layout, the validator option DXC itself sets for
//     -fvk-use-dx-layout. This is the tool's own check of the artifact it
//     writes, after optimization and any stripping.
//   - optimize_spirv: one Optimizer with the performance passes, then the
//     strip-debug pass when symbols were asked for, its internal validator
//     off. The optimizer is built for SPV_ENV_VULKAN_1_3, the one target
//     environment of this tool (compiler_options.hpp,
//     -fspv-target-env=vulkan1.3) and the environment the validator checks
//     against. Reflection info is kept.
//   - reflect_spirv: descriptor bindings with accessed set, so a binding the
//     shader declares and never reads is not reported, matching the DXIL
//     targets, where the optimizer removes unread resources. Uniform buffers
//     classify through classify_vulkan_uniform_buffer, everything else
//     through descriptor_array_for_vulkan_binding. The push constant block
//     uses the root constants when its member at kVulkanRootConstantOffset
//     is not flagged unused; the block itself is present whenever
//     a block index is read, so its presence says nothing about the root
//     constants. Vertex inputs are filtered by keeps_vertex_input (BuiltIn or
//     an "SV_" semantic) and keep their location.
//
// Retrieved rules: R.1 (the reflect module is released on every path),
// E.27, SL.con.3 (sizes checked before reinterpretation). Performance:
// COPY.1.
namespace bw::shader_compiler::detail {
namespace {

std::optional<std::vector<std::uint32_t>> to_words(std::span<const std::byte> module) {
  if (module.empty() || module.size() % sizeof(std::uint32_t) != 0U) {
    return std::nullopt;
  }
  std::vector<std::uint32_t> words(module.size() / sizeof(std::uint32_t));
  std::memcpy(words.data(), module.data(), module.size());
  return words;
}

// Owns one SPIRV-Reflect module.
class ReflectModule {
public:
  explicit ReflectModule(std::span<const std::byte> module) noexcept
      : result_(spvReflectCreateShaderModule2(SPV_REFLECT_MODULE_FLAG_NONE, module.size(),
                                              module.data(), &module_)) {}
  ReflectModule(const ReflectModule&) = delete;
  ReflectModule& operator=(const ReflectModule&) = delete;
  ReflectModule(ReflectModule&&) = delete;
  ReflectModule& operator=(ReflectModule&&) = delete;
  ~ReflectModule() {
    if (result_ == SPV_REFLECT_RESULT_SUCCESS) {
      spvReflectDestroyShaderModule(&module_);
    }
  }
  [[nodiscard]] bool ok() const noexcept { return result_ == SPV_REFLECT_RESULT_SUCCESS; }
  [[nodiscard]] SpvReflectShaderModule* get() noexcept { return &module_; }

private:
  SpvReflectShaderModule module_{};
  SpvReflectResult result_{SPV_REFLECT_RESULT_NOT_READY};
};

// Joins message pieces without a temporary per '+'.
std::string message(std::initializer_list<std::string_view> pieces) {
  std::string text;
  for (const std::string_view piece : pieces) {
    text += piece;
  }
  return text;
}

BackendStatus reflect_failure(const char* what) {
  return BackendStatus{std::string{"SPIRV-Reflect "} + what + " failed",
                       shaders::ExitCode::CompileError};
}

} // namespace

BackendStatus validate_spirv(std::span<const std::byte> module) {
  const std::optional<std::vector<std::uint32_t>> words = to_words(module);
  if (!words.has_value()) {
    return BackendStatus{"SPIR-V module size is not a multiple of four",
                         shaders::ExitCode::CompileError};
  }
  spvtools::SpirvTools tools(SPV_ENV_VULKAN_1_3);
  std::string messages;
  tools.SetMessageConsumer([&messages](spv_message_level_t, const char*,
                                       const spv_position_t& position, const char* message) {
    messages += "word ";
    messages += std::to_string(position.index);
    messages += ": ";
    messages += message;
    messages += '\n';
  });
  spvtools::ValidatorOptions options;
  options.SetScalarBlockLayout(true);
  if (!tools.Validate(words->data(), words->size(), options)) {
    return BackendStatus{"SPIR-V validation (Vulkan 1.3) failed:\n" + messages,
                         shaders::ExitCode::CompileError};
  }
  return BackendStatus{};
}

BackendStatus optimize_spirv(std::span<const std::byte> module, bool strip_debug,
                             std::vector<std::byte>& out) {
  const std::optional<std::vector<std::uint32_t>> words = to_words(module);
  if (!words.has_value()) {
    return BackendStatus{"SPIR-V module size is not a multiple of four",
                         shaders::ExitCode::CompileError};
  }
  spvtools::Optimizer optimizer(SPV_ENV_VULKAN_1_3);
  std::string messages;
  optimizer.SetMessageConsumer(
      [&messages](spv_message_level_t, const char*, const spv_position_t&, const char* message) {
        messages += message;
        messages += '\n';
      });
  optimizer.RegisterPerformancePasses();
  if (strip_debug) {
    optimizer.RegisterPass(spvtools::CreateStripDebugInfoPass());
  }
  spvtools::OptimizerOptions options;
  options.set_run_validator(false);
  std::vector<std::uint32_t> optimized;
  if (!optimizer.Run(words->data(), words->size(), &optimized, options)) {
    return BackendStatus{"SPIR-V optimization failed:\n" + messages,
                         shaders::ExitCode::CompileError};
  }
  out.resize(optimized.size() * sizeof(std::uint32_t));
  std::memcpy(out.data(), optimized.data(), out.size());
  return BackendStatus{};
}

BackendStatus reflect_spirv(std::span<const std::byte> module, shaders::ShaderStage stage,
                            BackendReflection& out) {
  out = BackendReflection{};
  ReflectModule reflected(module);
  if (!reflected.ok()) {
    return reflect_failure("spvReflectCreateShaderModule2");
  }

  std::uint32_t binding_count = 0;
  if (spvReflectEnumerateDescriptorBindings(reflected.get(), &binding_count, nullptr) !=
      SPV_REFLECT_RESULT_SUCCESS) {
    return reflect_failure("spvReflectEnumerateDescriptorBindings");
  }
  std::vector<SpvReflectDescriptorBinding*> bindings(binding_count);
  if (spvReflectEnumerateDescriptorBindings(reflected.get(), &binding_count, bindings.data()) !=
      SPV_REFLECT_RESULT_SUCCESS) {
    return reflect_failure("spvReflectEnumerateDescriptorBindings");
  }
  for (const SpvReflectDescriptorBinding* binding : bindings) {
    if (binding == nullptr || binding->accessed == 0U) {
      continue;
    }
    const std::string_view name = binding->name != nullptr ? binding->name : "";
    const std::string where = message(
        {"set ", std::to_string(binding->set), ", binding ", std::to_string(binding->binding)});
    if (binding->descriptor_type == SPV_REFLECT_DESCRIPTOR_TYPE_UNIFORM_BUFFER) {
      const std::optional<BlockBinding> classified =
          classify_vulkan_uniform_buffer(binding->set, binding->binding);
      if (!classified.has_value()) {
        return BackendStatus{message({"uniform buffer '", name, "' at ", where,
                                      " is not an element of the root layout"}),
                             shaders::ExitCode::ContractViolation};
      }
      BoundBlock block{*classified, {}};
      for (std::uint32_t m = 0; m < binding->block.member_count; ++m) {
        const SpvReflectBlockVariable& member = binding->block.members[m];
        block.members.push_back(
            ReflectedMember{member.name != nullptr ? member.name : "", member.offset});
      }
      out.blocks.push_back(std::move(block));
      continue;
    }
    const std::optional<shaders::DescriptorArray> array =
        descriptor_array_for_vulkan_binding(binding->set, binding->binding);
    if (!array.has_value()) {
      return BackendStatus{message({"resource '", name, "' at ", where,
                                    " is not a descriptor array of the root layout"}),
                           shaders::ExitCode::ContractViolation};
    }
    out.descriptor_arrays |= 1U << static_cast<std::uint32_t>(*array);
  }

  std::uint32_t push_count = 0;
  if (spvReflectEnumeratePushConstantBlocks(reflected.get(), &push_count, nullptr) !=
      SPV_REFLECT_RESULT_SUCCESS) {
    return reflect_failure("spvReflectEnumeratePushConstantBlocks");
  }
  std::vector<SpvReflectBlockVariable*> push_blocks(push_count);
  if (spvReflectEnumeratePushConstantBlocks(reflected.get(), &push_count, push_blocks.data()) !=
      SPV_REFLECT_RESULT_SUCCESS) {
    return reflect_failure("spvReflectEnumeratePushConstantBlocks");
  }
  for (const SpvReflectBlockVariable* block : push_blocks) {
    if (block == nullptr) {
      continue;
    }
    for (std::uint32_t m = 0; m < block->member_count; ++m) {
      const SpvReflectBlockVariable& member = block->members[m];
      if (member.offset == shaders::kVulkanRootConstantOffset &&
          (member.flags & SPV_REFLECT_VARIABLE_FLAGS_UNUSED) == 0U) {
        out.uses_root_constants = true;
      }
    }
  }

  if (stage == shaders::ShaderStage::Vertex) {
    std::uint32_t input_count = 0;
    if (spvReflectEnumerateInputVariables(reflected.get(), &input_count, nullptr) !=
        SPV_REFLECT_RESULT_SUCCESS) {
      return reflect_failure("spvReflectEnumerateInputVariables");
    }
    std::vector<SpvReflectInterfaceVariable*> inputs(input_count);
    if (spvReflectEnumerateInputVariables(reflected.get(), &input_count, inputs.data()) !=
        SPV_REFLECT_RESULT_SUCCESS) {
      return reflect_failure("spvReflectEnumerateInputVariables");
    }
    struct LocatedInput {
      std::string_view semantic{};
      std::uint32_t location{};
    };
    std::vector<LocatedInput> located;
    for (const SpvReflectInterfaceVariable* input : inputs) {
      if (input != nullptr &&
          keeps_vertex_input(input->semantic != nullptr ? input->semantic : "",
                             (input->decoration_flags & SPV_REFLECT_DECORATION_BUILT_IN) != 0U)) {
        located.push_back(
            LocatedInput{input->semantic != nullptr ? input->semantic : "", input->location});
      }
    }
    std::sort(located.begin(), located.end(),
              [](const LocatedInput& a, const LocatedInput& b) { return a.location < b.location; });
    for (const LocatedInput& input : located) {
      const std::string location = std::to_string(input.location);
      // DXC numbers stage inputs from 0 in declaration order, which is the
      // ordinal the DXIL targets record; a gap would make the reflection
      // differ across targets, so it is refused rather than renumbered.
      if (input.location != out.vertex_inputs.size()) {
        return BackendStatus{
            message({"vertex input locations are not dense from 0 (location ", location, ")"}),
            shaders::ExitCode::ContractViolation};
      }
      const std::optional<Semantic> semantic = split_semantic(input.semantic);
      if (!semantic.has_value()) {
        return BackendStatus{message({"vertex input at location ", location,
                                      " has no HLSL semantic (-fspv-reflect)"}),
                             shaders::ExitCode::ContractViolation};
      }
      out.vertex_inputs.push_back(BackendVertexInput{*semantic, input.location});
    }
  }
  if (stage == shaders::ShaderStage::Compute && reflected.get()->entry_point_count > 0U) {
    const auto& local_size = reflected.get()->entry_points[0].local_size;
    out.thread_group_size = {local_size.x, local_size.y, local_size.z};
  }
  return BackendStatus{};
}

} // namespace bw::shader_compiler::detail
