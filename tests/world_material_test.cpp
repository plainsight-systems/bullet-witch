// Independent original-program signature witnesses for the material boundary.
// Constants/samplers and vertex address-register use supplied the expected
// data; these are semantic findings, not shader bytecode or a copy of our
// classifier. Tests also reject unaudited neighboring selectors and exercise
// explicit inspection energy suppression. No assets, files, device or
// subprocess needed. Both phases retrieve I.1 (#ri-explicit), CDSA.32
// (guidelines/cpu-dsa/
// CDSA.32-transform-static-data-into-the-consumer-layout.md) and GPU.4:
// verify the prepared public dependency contract independently of hot shaders.
#include "bw/render/world_material.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <stdexcept>

namespace {
struct ProgramWitness {
  std::uint32_t flags;
  std::uint16_t variant;
  std::uint32_t features;
  std::uint8_t influences;
};
// Bit positions below are test serialization only, independent of selector
// bits.
std::uint32_t signature(const bw::render::WorldMaterialRequirements& value) {
  const std::array<bool, 18> features{
      value.normal_map,
      value.secondary_base,
      value.environment_map,
      value.vertex_color,
      value.vertex_illumination,
      value.reflection,
      value.fresnel,
      value.directional_light,
      value.local_lights,
      value.environment_shadow,
      value.shadow_reception,
      value.depth_cutout,
      value.spherical_harmonics,
      value.base_texture,
      value.ambient_occlusion,
      value.secondary_alpha_blend,
      value.alpha_fresnel,
      value.normal_required,
  };
  std::uint32_t result{};
  for (std::size_t i = 0; i < features.size(); ++i) {
    if (features.at(i)) result |= std::uint32_t{1} << i;
  }
  return result;
}
void require(bool value, const char* message) {
  if (!value) throw std::runtime_error(message);
}
// Source signature corpus: 130 prior selectors and 28 newly restored static
// keys. Expected words encode the named feature order above; influences come
// from actual original palette addressing, not a copied selector equation.
constexpr std::array<ProgramWitness, 158> kOriginalPrograms{{
    {0x00800000, 0x000, 0x205a0, 0}, {0x00800200, 0x000, 0x215a0, 0},
    {0x00800a02, 0x001, 0x235b0, 0}, {0x00800a82, 0x001, 0x235b1, 0},
    {0x00800a83, 0x001, 0x23db1, 0}, {0x00820490, 0x001, 0x265e1, 4},
    {0x00820690, 0x001, 0x275e1, 4}, {0x00820882, 0x102, 0x225f3, 0},
    {0x00820a02, 0x001, 0x235f0, 0}, {0x00820a82, 0x001, 0x235f1, 0},
    {0x00820a82, 0x102, 0x235f3, 0}, {0x00820a83, 0x001, 0x23df1, 0},
    {0x00822490, 0x001, 0x267e5, 4}, {0x00822491, 0x001, 0x26fe5, 4},
    {0x00822690, 0x001, 0x277e5, 4}, {0x00822691, 0x001, 0x27fe5, 4},
    {0x00822a82, 0x001, 0x237f5, 0}, {0x00a20a82, 0x001, 0x231f1, 0},
    {0x00a22aa2, 0x001, 0x231fd, 0}, {0x00c00a02, 0x001, 0x234b0, 0},
    {0x00c00a03, 0x001, 0x23cb0, 0}, {0x00c00a82, 0x001, 0x234b1, 0},
    {0x00c00a83, 0x001, 0x23cb1, 0}, {0x00c20a02, 0x001, 0x234f0, 0},
    {0x00c20a82, 0x001, 0x234f1, 0}, {0x00c20a83, 0x001, 0x23cf1, 0},
    {0x00c22a82, 0x001, 0x236f5, 0}, {0x00e00802, 0x001, 0x220b0, 0},
    {0x00e00803, 0x001, 0x228b0, 0}, {0x00e00882, 0x001, 0x220b1, 0},
    {0x00e00882, 0x102, 0x220b3, 0}, {0x00e00883, 0x001, 0x228b1, 0},
    {0x00e00a02, 0x001, 0x230b0, 0}, {0x00e00a03, 0x001, 0x238b0, 0},
    {0x00e00a82, 0x001, 0x230b1, 0}, {0x00e00a82, 0x102, 0x230b3, 0},
    {0x00e00a83, 0x001, 0x238b1, 0}, {0x00e20802, 0x001, 0x220f0, 0},
    {0x00e20803, 0x001, 0x228f0, 0}, {0x00e20882, 0x001, 0x220f1, 0},
    {0x00e20883, 0x001, 0x228f1, 0}, {0x00e20a02, 0x001, 0x230f0, 0},
    {0x00e20a03, 0x001, 0x238f0, 0}, {0x00e20a82, 0x001, 0x230f1, 0},
    {0x00e20a83, 0x001, 0x238f1, 0}, {0x00e22882, 0x001, 0x220f5, 0},
    {0x00e228a2, 0x001, 0x220fd, 0}, {0x00e22a82, 0x001, 0x230f5, 0},
    {0x00e22aa2, 0x001, 0x230fd, 0}, {0x02800a02, 0x001, 0x23590, 0},
    {0x02a00a03, 0x001, 0x23990, 0}, {0x02c00a02, 0x001, 0x23490, 0},
    {0x02c00a03, 0x001, 0x23c90, 0}, {0x02c00a82, 0x001, 0x23491, 0},
    {0x02e00802, 0x001, 0x22090, 0}, {0x02e00803, 0x001, 0x22890, 0},
    {0x02e00882, 0x001, 0x22091, 0}, {0x02e00882, 0x102, 0x22093, 0},
    {0x02e00883, 0x001, 0x22891, 0}, {0x02e00a02, 0x001, 0x23090, 0},
    {0x02e00a03, 0x001, 0x23890, 0}, {0x02e00a82, 0x001, 0x23091, 0},
    {0x02e00a82, 0x102, 0x23093, 0}, {0x02e00a83, 0x001, 0x23891, 0},
    {0x04a00882, 0x001, 0x22131, 0}, {0x04a00883, 0x001, 0x22931, 0},
    {0x04a00a82, 0x001, 0x23131, 0}, {0x04a00a83, 0x001, 0x23931, 0},
    {0x04a20a82, 0x001, 0x23171, 0}, {0x04a20a83, 0x001, 0x23971, 0},
    {0x04e20882, 0x001, 0x02010, 0}, {0x04e20883, 0x001, 0x02810, 0},
    {0x04e20a82, 0x001, 0x23011, 0}, {0x04e20a83, 0x001, 0x23811, 0},
    {0x06c00200, 0x001, 0x23000, 0}, {0x06c00a02, 0x001, 0x23010, 0},
    {0x06c00a03, 0x001, 0x23810, 0}, {0x06c00a82, 0x001, 0x23011, 0},
    {0x06e00000, 0x001, 0x02000, 0}, {0x06e00001, 0x001, 0x02800, 0},
    {0x06e00200, 0x001, 0x23000, 0}, {0x06e00201, 0x001, 0x23800, 0},
    {0x06e00802, 0x001, 0x02010, 0}, {0x06e00803, 0x001, 0x02810, 0},
    {0x06e00882, 0x001, 0x02010, 0}, {0x06e00a02, 0x001, 0x23010, 0},
    {0x06e00a03, 0x001, 0x23810, 0}, {0x06e00a82, 0x001, 0x23011, 0},
    {0x40800010, 0x001, 0x225a0, 3}, {0x40800210, 0x001, 0x235a0, 3},
    {0x40800411, 0x001, 0x26da0, 3}, {0x40800490, 0x001, 0x265a1, 3},
    {0x40800611, 0x001, 0x27da0, 3}, {0x40800690, 0x001, 0x275a1, 3},
    {0x42800010, 0x001, 0x22580, 3}, {0x42800091, 0x001, 0x22d81, 3},
    {0x42800210, 0x001, 0x23580, 3}, {0x42800291, 0x001, 0x23d81, 3},
    {0x80800010, 0x001, 0x225a0, 2}, {0x80800011, 0x001, 0x22da0, 2},
    {0x80800090, 0x001, 0x225a1, 2}, {0x80800210, 0x001, 0x235a0, 2},
    {0x80800211, 0x001, 0x23da0, 2}, {0x80800290, 0x001, 0x235a1, 2},
    {0x80800490, 0x001, 0x265a1, 2}, {0x80800491, 0x001, 0x26da1, 2},
    {0x80800690, 0x001, 0x275a1, 2}, {0x80800691, 0x001, 0x27da1, 2},
    {0x80822010, 0x001, 0x227e4, 2}, {0x80822011, 0x001, 0x22fe4, 2},
    {0x80822210, 0x001, 0x237e4, 2}, {0x80822211, 0x001, 0x23fe4, 2},
    {0x80822490, 0x001, 0x267e5, 2}, {0x80822491, 0x001, 0x26fe5, 2},
    {0x80822690, 0x001, 0x277e5, 2}, {0x80822691, 0x001, 0x27fe5, 2},
    {0x82800010, 0x001, 0x22580, 2}, {0x82800011, 0x001, 0x22d80, 2},
    {0x82800210, 0x001, 0x23580, 2}, {0x82800211, 0x001, 0x23d80, 2},
    {0x82800410, 0x001, 0x26580, 2}, {0x82800490, 0x001, 0x26581, 2},
    {0x82800491, 0x001, 0x26d81, 2}, {0x82800610, 0x001, 0x27580, 2},
    {0x82800690, 0x001, 0x27581, 2}, {0x82800691, 0x001, 0x27d81, 2},
    {0x82c080b0, 0x002, 0x3a48b, 2}, {0x82c082b0, 0x002, 0x3b48b, 2},
    {0xc0800010, 0x001, 0x225a0, 1}, {0xc0800011, 0x001, 0x22da0, 1},
    {0xc0800090, 0x001, 0x225a1, 1}, {0xc0800210, 0x001, 0x235a0, 1},
    {0xc0800211, 0x001, 0x23da0, 1}, {0xc0800290, 0x001, 0x235a1, 1},
    {0xc0800410, 0x001, 0x265a0, 1}, {0xc0800490, 0x001, 0x265a1, 1},
    {0xc0800491, 0x001, 0x26da1, 1}, {0xc0800610, 0x001, 0x275a0, 1},
    {0xc0800690, 0x001, 0x275a1, 1}, {0xc0800691, 0x001, 0x27da1, 1},
    {0xc0802010, 0x001, 0x227a4, 1}, {0xc0802011, 0x001, 0x22fa4, 1},
    {0xc0802210, 0x001, 0x237a4, 1}, {0xc0802211, 0x001, 0x23fa4, 1},
    {0xc0802490, 0x001, 0x267a5, 1}, {0xc0802690, 0x001, 0x277a5, 1},
    {0xc0822010, 0x001, 0x227e4, 1}, {0xc0822011, 0x001, 0x22fe4, 1},
    {0xc0822210, 0x001, 0x237e4, 1}, {0xc0822211, 0x001, 0x23fe4, 1},
    {0xc0c22010, 0x001, 0x226e4, 1}, {0xc0c22210, 0x001, 0x236e4, 1},
    {0xc2800010, 0x001, 0x22580, 1}, {0xc2800011, 0x001, 0x22d80, 1},
    {0xc2800210, 0x001, 0x23580, 1}, {0xc2800211, 0x001, 0x23d80, 1},
    {0xc2800410, 0x001, 0x26580, 1}, {0xc2800610, 0x001, 0x27580, 1},
}};
}  // namespace

void world_material_tests() {
  using bw::render::world_material_requirements;
  using bw::render::WorldLightingPolicy;
  // Every audited variant must retain all source consumers simultaneously.
  for (const auto& witness : kOriginalPrograms) {
    const auto actual =
        world_material_requirements(witness.flags, witness.variant);
    require(actual.supported && signature(actual) == witness.features &&
                actual.joint_influences == witness.influences,
            "World material differs from original program dependency witness");
    require(!world_material_requirements(witness.flags ^ 0x10000000U,
                                         witness.variant)
                 .supported,
            "Unaudited neighboring program silently selected a surface family");
    require(
        !world_material_requirements(witness.flags, 0xffff).supported,
        "Unaudited texture-layer variant silently selected a surface family");
  }
  // Local-only specular and normal mapping survive directional suppression.
  const auto local = world_material_requirements(0x04a00882, 1);
  require(local.local_lights && local.reflection && local.normal_map &&
              local.normal_required && !local.directional_light &&
              !local.shadow_reception,
          "Local-only surface incorrectly depends on directional illumination");
  const auto unshadowed = world_material_requirements(0x00e20882, 1);
  require(unshadowed.directional_light && !unshadowed.shadow_reception,
          "Authored unshadowed surface inherited directional shadows");
  const auto restricted = world_material_requirements(
      0x00822690, 1, WorldLightingPolicy::without_direct_lighting);
  require(restricted.supported && restricted.environment_map &&
              restricted.reflection && restricted.spherical_harmonics &&
              restricted.normal_map && !restricted.local_lights &&
              !restricted.directional_light && !restricted.shadow_reception &&
              !restricted.environment_shadow,
          "Inspection energy policy changed unrelated material dependencies");
}
