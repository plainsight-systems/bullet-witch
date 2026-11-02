#ifndef BW_SHADER_COMPILER_ENUMERATION_HPP
#define BW_SHADER_COMPILER_ENUMERATION_HPP
#include "bw/rendering/shaders/compiler_options.hpp"
#include "bw/rendering/shaders/permutation.hpp"
#include "bw/rendering/shaders/root_layout.hpp"

#include <span>
#include <string>

// Responsibility: the text of shader_compiler's enumerate mode, shared by the
// tool and by the configure-time enumerator (tools/shader_compiler/enumerate),
// which links the engine shader module and this file only.
//
// Contract: appends append_enumerate_line (compiler_options.hpp) for every
// shader in `registry` in registry order, every permutation
// enumerate_permutations admits in ascending order, and every target in the
// order given. Nothing else is appended. Cannot fail.
//
// Core guidelines read: I.1 (the registry and targets are parameters), R.14
// (spans for both), SL.str.2. Performance guidelines read: COPY.7 (the
// permutation buffer is sized once per shader).
namespace bw::shader_compiler {

void append_enumeration(std::span<const shaders::ShaderDescription> registry,
                        std::span<const shaders::ShaderTarget> targets, std::string& out);

} // namespace bw::shader_compiler
#endif
