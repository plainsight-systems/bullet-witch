#include "bw/shader_compiler/enumeration.hpp"

#include <cstddef>
#include <span>
#include <string>
#include <vector>

// Responsibility: bw/shader_compiler/enumeration.hpp. One loop over the
// registry, the enumerated permutations and the targets, in the order
// compiler_options.hpp states for enumerate output.
//
// Retrieved rules: I.1, R.14, SL.con.3 (the permutation buffer holds
// permutation_count ids, the precondition of enumerate_permutations).
namespace bw::shader_compiler {

void append_enumeration(std::span<const shaders::ShaderDescription> registry,
                        std::span<const shaders::ShaderTarget> targets, std::string& out) {
  std::vector<shaders::PermutationId> ids;
  for (const shaders::ShaderDescription& shader : registry) {
    ids.assign(shader.permutation_count, shaders::PermutationId{});
    const std::size_t count = shaders::enumerate_permutations(shader, ids);
    for (std::size_t i = 0; i < count; ++i) {
      for (const shaders::ShaderTarget target : targets) {
        shaders::append_enumerate_line(shader, ids[i], target, out);
      }
    }
  }
}

} // namespace bw::shader_compiler
