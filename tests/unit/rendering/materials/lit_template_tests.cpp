// Unit test: public contracts of bw/rendering/materials/lit_template.hpp.
#include "bw/rendering/geometry/vertex_streams.hpp"
#include "bw/rendering/materials/lit_template.hpp"
#include "bw/testing/harness.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace {

namespace m = bw::materials;
using bw::geometry::VertexAttribute;
using bw::testing::require;

// (lit_template.hpp, contract 2): position and normal always, texture
// coordinate set 0 with a base color or normal map, tangent with a normal
// map, nothing for the occlusion and reflectance map or masked.
void vertex_attributes() {
  const auto bit = [](VertexAttribute a) { return 1U << static_cast<std::uint32_t>(a); };
  const std::uint32_t always = bit(VertexAttribute::Position) | bit(VertexAttribute::Normal);
  const std::uint32_t uv = bit(VertexAttribute::TexCoord0);
  struct Row {
    std::string_view name;
    m::LitSwitches switches;
    std::uint32_t expected;
  };
  const std::array<Row, 5> rows{{
      {"no switch", {}, always},
      {"base color map", {.base_color_map = true}, always | uv},
      {"normal map", {.normal_map = true}, always | uv | bit(VertexAttribute::Tangent)},
      {"base and occlusion reflectance",
       {.base_color_map = true, .occlusion_reflectance_map = true},
       always | uv},
      {"base and masked", {.base_color_map = true, .masked = true}, always | uv},
  }};
  for (std::size_t i = 0; i < rows.size(); ++i) {
    require(m::lit_vertex_attributes(rows[i].switches) == rows[i].expected, i, rows[i].name);
  }
}

using Scenario = bw::testing::Scenario<void>;
constexpr std::array kScenarios{
    Scenario{"vertex_attributes", &vertex_attributes},
};

} // namespace

int main(int argc, char** argv) { return bw::testing::run_scenario(argc, argv, kScenarios); }
