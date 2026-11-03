#include "bw/rendering/shaders/permutation.hpp"

#include "bw/rendering/shaders/parameter_block.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <span>
#include <string_view>

// Responsibility: the out-of-line parts of bw/rendering/shaders/permutation.hpp:
// the enumeration of a shader's build set, the registry of every shader the
// engine builds, and lookup by name.
//
// Contract:
//   - enumerate_permutations writes every id below permutation_count that
//     should_compile admits (all ids when it is null), strictly ascending, and
//     returns how many. A span shorter than permutation_count violates the
//     header's precondition; that is a programming error and the function
//     aborts rather than return a short build set that would look complete.
//   - shader_registry() is one constexpr table with unique names, checked at
//     compile time below.
//   - find_shader returns the entry whose name is equal, or null.
//
// Algorithm, enumerate (Unreal f45729c0b6f097038e72b52d366a62baeca3275b
// Engine/Source/Runtime/RenderCore/Private/Shader.cpp:463-466 as the header cites:
// each permutation id is offered to the per-type predicate):
//   1. Check the output capacity.
//   2. For id = 0 .. permutation_count - 1, keep id when the predicate admits it.
//
// Design and cost: O(permutation_count) predicate calls at build time; the
// registry is read-only data with no initialization at runtime. find_shader
// is a linear scan: the registry holds a few dozen entries at most (CDSA.9:
// tiny arrays are scanned, not searched).
//
// Retrieved rules: SL.con.3 (writes never pass the output span; the capacity
// is checked before the loop instead of per element), SL.str.2 (names are
// string_views into static storage), E.27 (no error channel is invented for a
// precondition; violation is fatal and visible). Performance: CDSA.9 (linear
// scan for a tiny table), MEM.9 (no allocation; the caller owns the output).
namespace bw::shaders {
namespace {

// Per-draw block of the fixture shader. Offsets: color 0, scale 16,
// texture 24, sampler 28; size 32, dense with no padding.
struct alignas(kBlockAlignment) FullscreenTriangleDraw {
  Float4 color{};
  Float2 scale{};
  TextureIndex texture{};
  SamplerIndex sampler{};
};

} // namespace
} // namespace bw::shaders

// The macro stringizes its first argument as the block name, which becomes an
// HLSL identifier, so the type is named unqualified here.
using bw::shaders::FullscreenTriangleDraw;
BW_PARAMETER_BLOCK_LAYOUT(FullscreenTriangleDraw,
                          BW_PARAMETER_MEMBER(FullscreenTriangleDraw, color),
                          BW_PARAMETER_MEMBER(FullscreenTriangleDraw, scale),
                          BW_PARAMETER_MEMBER(FullscreenTriangleDraw, texture),
                          BW_PARAMETER_MEMBER(FullscreenTriangleDraw, sampler));

namespace bw::shaders {
namespace {

// The fixture shader the integration tests compile. source_path names a file
// under the shader source root that step C of the shader-pipeline slice adds,
// once a compiler can prove it; nothing in this module opens it.
struct FullscreenTriangle {
  using FlipY = BoolDimension<"BW_FLIP_Y">;
  using Domain = PermutationDomain<FlipY>;
  static constexpr std::string_view name = "fullscreen_triangle";
  static constexpr std::string_view source_path = "fullscreen_triangle.hlsl";
  static constexpr std::string_view entry_point = "main";
  static constexpr ShaderStage stage = ShaderStage::Vertex;
  static constexpr std::span<const BlockLayout* const> static_blocks{};
  static constexpr const BlockLayout* draw_block = &BlockLayoutOf<FullscreenTriangleDraw>::value;
  [[nodiscard]] static constexpr bool should_compile(const Domain& /*domain*/) noexcept {
    return true;
  }
};

// The fixture pixel shader the RHI integration tests pair with
// FullscreenTriangle: it writes the interpolated color to target 0.
struct FullscreenTriangleColor {
  using Domain = PermutationDomain<>;
  static constexpr std::string_view name = "fullscreen_triangle_color";
  static constexpr std::string_view source_path = "fullscreen_triangle_color.hlsl";
  static constexpr std::string_view entry_point = "main";
  static constexpr ShaderStage stage = ShaderStage::Pixel;
  static constexpr std::span<const BlockLayout* const> static_blocks{};
  static constexpr const BlockLayout* draw_block = nullptr;
  [[nodiscard]] static constexpr bool should_compile(const Domain& /*domain*/) noexcept {
    return true;
  }
};

// Every shader the engine builds. A renderer slice adds an entry here and its
// HLSL under the shader root.
constexpr std::array kRegistry{
    describe_shader<FullscreenTriangle>(),
    describe_shader<FullscreenTriangleColor>(),
};

[[nodiscard]] consteval bool registry_names_unique() {
  for (std::size_t i = 0; i < kRegistry.size(); ++i) {
    for (std::size_t j = i + 1; j < kRegistry.size(); ++j) {
      if (kRegistry[i].name == kRegistry[j].name) {
        return false;
      }
    }
  }
  return true;
}
static_assert(registry_names_unique(), "shader registry names are unique");

} // namespace

std::size_t enumerate_permutations(const ShaderDescription& shader,
                                   std::span<PermutationId> out) noexcept {
  // Step 1: precondition of the header; a short span is a programming error.
  if (out.size() < shader.permutation_count) {
    std::abort();
  }
  // Step 2: ascending walk; the predicate is pure, so the set is the same on
  // every host.
  std::size_t written = 0;
  for (std::uint32_t id = 0; id < shader.permutation_count; ++id) {
    const PermutationId candidate{id};
    if (shader.should_compile == nullptr || shader.should_compile(candidate)) {
      out[written] = candidate;
      ++written;
    }
  }
  return written;
}

std::span<const ShaderDescription> shader_registry() noexcept { return kRegistry; }

const ShaderDescription* find_shader(std::span<const ShaderDescription> registry,
                                     std::string_view name) noexcept {
  for (const ShaderDescription& shader : registry) {
    if (shader.name == name) {
      return &shader;
    }
  }
  return nullptr;
}

} // namespace bw::shaders
