// Synthetic PE sections exercise decoder ownership, body conversion and invalid
// topology. This fixture is authored format data, not copied native tables.
#include "game/content/original/character_physics_recipe.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <stdexcept>
#include <vector>

#include "bw/content/formats/common.hpp"

namespace {
void require(bool condition) {
  if (!condition) throw std::runtime_error("Character recipe contract failed");
}
void word(std::vector<std::uint8_t>& bytes, std::size_t offset,
          std::uint32_t value) {
  for (std::size_t i = 0; i < 4; ++i) {
    bytes.at(offset + i) = static_cast<std::uint8_t>(value >> (i * 8));
  }
}
std::vector<std::uint8_t> image() {
  std::vector<std::uint8_t> bytes(0x3000);
  word(bytes, 0, 0x5a4d);
  word(bytes, 0x3c, 0x80);
  word(bytes, 0x80, 0x4550);
  word(bytes, 0x84, 0x1014c);
  word(bytes, 0x94, 0xe0);
  word(bytes, 0x98, 0x10b);
  word(bytes, 0x98 + 28, 0x400000);
  const std::size_t section = 0x178;
  word(bytes, section + 12, 0x564000);
  word(bytes, section + 16, 0x2000);
  word(bytes, section + 20, 0x1000);
  const auto text = [&](std::size_t offset, std::string_view value) {
    std::ranges::copy(value,
                      bytes.begin() + static_cast<std::ptrdiff_t>(offset));
  };
  text(0x1000, "EA0");
  text(0x1020, "RAGDOLL_TEST");
  text(0x1040, "root");
  text(0x1060, "limb");
  word(bytes, 0x1d30, 0x964000);
  word(bytes, 0x1d34, 0x964600);
  word(bytes, 0x1600, 0x964020);
  word(bytes, 0x1604, 0x964680);
  word(bytes, 0x1608, 2);
  word(bytes, 0x160c, 1);
  word(bytes, 0x1610, 0x964700);
  word(bytes, 0x1614, 0x964800);
  word(bytes, 0x1680, 0x964040);
  word(bytes, 0x1684, 0x964060);
  // Sphere, then source length/radius record converted to a local box.
  word(bytes, 0x1704, std::bit_cast<std::uint32_t>(3.0F));
  word(bytes, 0x1708, std::bit_cast<std::uint32_t>(0.25F));
  word(bytes, 0x1720, 2);
  word(bytes, 0x1724, std::bit_cast<std::uint32_t>(2.0F));
  word(bytes, 0x1728, std::bit_cast<std::uint32_t>(0.6F));
  word(bytes, 0x172c, std::bit_cast<std::uint32_t>(0.1F));
  word(bytes, 0x1734, std::bit_cast<std::uint32_t>(0.05F));
  word(bytes, 0x1800, 1);
  word(bytes, 0x1808, 1);
  word(bytes, 0x180c, 2);
  word(bytes, 0x1810, std::bit_cast<std::uint32_t>(1.0F));
  word(bytes, 0x28ec, std::bit_cast<std::uint32_t>(-9.81F));
  for (std::size_t i = 0; i < 13; ++i) {
    const auto base = 0x26f0 + i * 20;
    word(bytes, base, static_cast<std::uint32_t>(i + 1));
    word(bytes, base + 4, static_cast<std::uint32_t>(i % 4));
    word(bytes, base + 8, std::bit_cast<std::uint32_t>(.125F));
    word(bytes, base + 12, std::bit_cast<std::uint32_t>(.75F));
    word(bytes, base + 16, std::bit_cast<std::uint32_t>(.25F));
  }
  return bytes;
}
template <class Function>
void rejects(Function operation) {
  bool caught = false;
  try {
    operation();
  } catch (const bw::resources::Error&) {
    caught = true;
  }
  require(caught);
}
}  // namespace
void character_physics_recipe_tests() {
  auto bytes = image();
  const auto scene = bw::game::read_scene_physics_recipe(bytes);
  require(scene.gravity[1] == -9.81F &&
          scene.materials[1].static_friction == .75F &&
          scene.materials[1].dynamic_friction == .25F &&
          scene.materials[4].combine_mode == 3);
  word(bytes, 0x26f4, 4);
  rejects([&] { (void)bw::game::read_scene_physics_recipe(bytes); });
  word(bytes, 0x26f4, 0);
  const auto recipe = bw::game::read_character_physics_recipe(bytes, "EA0");
  if (!recipe) throw std::runtime_error("Missing synthetic recipe");
  require(recipe->body_count == 2 && recipe->joint_count == 1);
  require(recipe->bodies[0].bone_name() == "root" &&
          recipe->bodies[0].radius == .25F);
  require(recipe->bodies[1].bone_name() == "limb" &&
          std::abs(recipe->bodies[1].half_extent[0] - .3F) < .00001F &&
          std::abs(recipe->bodies[1].local_center[0] - .35F) < .00001F);
  require(recipe->joints[0].first == 0 && recipe->joints[0].second == 1 &&
          recipe->joints[0].minimum_angle < 0 &&
          recipe->joints[0].maximum_angle > 0);
  require(
      bw::game::read_character_physics_recipe(bytes, "ea0variant").has_value());
  require(!bw::game::read_character_physics_recipe(bytes, "NONE"));
  word(bytes, 0x1808, 2);
  rejects([&] { (void)bw::game::read_character_physics_recipe(bytes, "EA0"); });
  bytes.resize(0x1800);
  rejects([&] { (void)bw::game::read_character_physics_recipe(bytes, "EA0"); });
  // Returned record lifetime is independent of its input image.
  bytes.clear();
  require(recipe->bodies[1].bone_name() == "limb");
}
