// Import the character body's authored native recipe from purchased witch.exe.
// Borrow PE bytes for this call only; return copied bounded records. The native
// owner has sixteen body/joint slots. Missing prefix yields nullopt; malformed
// or unsupported executable/table data throws resources::Error. No executable
// code is loaded or run. The original .data recipe is readable in the purchased
// Steam executable; no unpacked analysis image or generated fixture is needed.
// Algorithm: validate PE32 sections -> resolve original prefix table -> copy
// named body geometry/masses and joint topology/axes. Prefix matching is ASCII
// case-insensitive like the source; pass the model family without "model.".
// World startup resolves
// names against its skeleton and creates provider resources. No simulation,
// solver choice, or skeletal mutation belongs here. Load-time O(sections *
// records), bounded storage with no allocation. Core R.2 (#rr-use-ptr) bounds
// borrowed bytes; performance CDSA.32 (transform-static-data-into-the-consumer-
// layout) converts once into copied consumer records. Source mapping:
// research/engine/subsystems/first-combat-physics-boundary.md.
#pragma once
#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

#include "bw/math/vector.hpp"

namespace bw::game {
inline constexpr std::size_t character_physics_capacity = 16;
inline constexpr std::size_t character_physics_name_bytes = 32;
struct CharacterPhysicsBody {
  std::array<char, character_physics_name_bytes> bone{};
  // Native kinds 1 and 2 both construct boxes; kind 2 converts length/radius.
  enum class Geometry : std::uint8_t { sphere, box };
  math::Vector3<float> half_extent{};
  math::Vector3<float> local_center{};
  float mass{}, radius{};
  float linear_damping{}, angular_damping{}, skin_width{};
  Geometry geometry{};
  std::string_view bone_name() const;
};
struct CharacterPhysicsJoint {
  enum class Kind : std::uint8_t { spherical, hinge };
  std::uint32_t first{}, second{}, group{};
  // Authored direction transformed by the first body's initial orientation;
  // second body's initial origin supplies the global anchor.
  math::Vector3<float> axis{};
  float minimum_angle{}, maximum_angle{}, swing_angle{};
  float twist_stiffness{}, twist_damping{}, swing_stiffness{}, swing_damping{};
  float limit_restitution{}, limit_hardness{};
  Kind kind{};
};
struct CharacterPhysicsRecipe {
  std::array<CharacterPhysicsBody, character_physics_capacity> bodies{};
  std::array<CharacterPhysicsJoint, character_physics_capacity> joints{};
  std::uint32_t body_count{}, joint_count{};
};
std::optional<CharacterPhysicsRecipe> read_character_physics_recipe(
    std::span<const std::uint8_t> executable, std::string_view model_prefix);
// Original scene settings, copied from purchased native tables. Keep static
// and sliding friction distinct even when a provider has one friction value.
inline constexpr std::size_t scene_physics_material_count = 14;
struct ScenePhysicsMaterial {
  float restitution{}, static_friction{}, dynamic_friction{};
  std::uint32_t combine_mode{};
};
struct ScenePhysicsRecipe {
  math::Vector3<float> gravity{};
  std::array<ScenePhysicsMaterial, scene_physics_material_count> materials{};
};
ScenePhysicsRecipe read_scene_physics_recipe(
    std::span<const std::uint8_t> executable);
}  // namespace bw::game
