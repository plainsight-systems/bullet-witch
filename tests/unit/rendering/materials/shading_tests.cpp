// Unit test: public contracts of bw/rendering/materials/shading.hpp.
#include "bw/rendering/materials/shading.hpp"
#include "bw/testing/harness.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace {

namespace m = bw::materials;
using bw::testing::require;

// One cell of docs/architecture/materials.md, "Attributes".
enum class Cell : std::uint8_t { No, Yes, Translucent, Masked, OpaqueAndMasked };

[[nodiscard]] bool cell_active(Cell cell, m::BlendMode blend) {
  switch (cell) {
  case Cell::No:
    return false;
  case Cell::Yes:
    return true;
  case Cell::Translucent:
    return blend == m::BlendMode::Translucent || blend == m::BlendMode::Additive ||
           blend == m::BlendMode::Modulate;
  case Cell::Masked:
    return blend == m::BlendMode::Masked;
  case Cell::OpaqueAndMasked:
    return blend == m::BlendMode::Opaque || blend == m::BlendMode::Masked;
  }
  return false;
}

// (shading.hpp, contract 1 and 2; materials.md, "Attributes"): the table
// cell by cell for every blend mode, columns in ShadingModel order (unlit,
// lit, subsurface, hair), rows in MaterialAttribute order; each model's mask
// is the attributes some blend mode activates.
void attribute_table() {
  using C = Cell;
  constexpr C Y = C::Yes;
  constexpr C N = C::No;
  constexpr std::array<std::array<Cell, m::kShadingModelCount>, m::kMaterialAttributeCount> table{{
      {N, Y, Y, Y},                                 // diffuse albedo
      {N, Y, Y, Y},                                 // F0
      {N, Y, Y, Y},                                 // F90
      {N, Y, Y, Y},                                 // roughness
      {N, Y, Y, Y},                                 // normal
      {N, N, N, Y},                                 // tangent
      {Y, Y, Y, Y},                                 // emissive
      {N, Y, Y, Y},                                 // ambient occlusion
      {N, N, Y, N},                                 // subsurface color
      {C::Translucent, C::Translucent, Y, Y},       // opacity
      {C::Masked, C::Masked, C::Masked, C::Masked}, // opacity mask
      {Y, Y, Y, Y},                                 // world position offset
      {C::OpaqueAndMasked, C::OpaqueAndMasked, C::OpaqueAndMasked, C::OpaqueAndMasked}, // pdo
  }};
  for (std::uint32_t model = 0; model < m::kShadingModelCount; ++model) {
    std::uint32_t mask = 0;
    for (std::uint32_t a = 0; a < m::kMaterialAttributeCount; ++a) {
      for (std::uint32_t b = 0; b < m::kBlendModeCount; ++b) {
        const auto blend = static_cast<m::BlendMode>(b);
        const bool expected = cell_active(table[a][model], blend);
        require(m::attribute_active(static_cast<m::MaterialAttribute>(a),
                                    static_cast<m::ShadingModel>(model), blend) == expected,
                (model * 100U) + (a * 10U) + b, "attribute_active is the document's cell");
        mask |= expected ? (1U << a) : 0U;
      }
    }
    require(m::attribute_mask(static_cast<m::ShadingModel>(model)) == mask, model,
            "the mask has every attribute some blend mode activates");
  }
}

using Scenario = bw::testing::Scenario<void>;
constexpr std::array kScenarios{
    Scenario{"attribute_table", &attribute_table},
};

} // namespace

int main(int argc, char** argv) { return bw::testing::run_scenario(argc, argv, kScenarios); }
