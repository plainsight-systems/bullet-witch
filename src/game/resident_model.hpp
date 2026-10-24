// Borrow a mission-owned immutable model and its resident device identity.
// The mission must outlive every view; this value owns neither asset nor GPU
// storage. Scene construction may copy this small association once, then reuse
// it without catalog searches or uploads. No mutation or synchronization.
// Both MCP phases C.32 (#rc-dtor-ptr) makes borrowing explicit; MEM.9
// (guidelines/memory/MEM.9-embedded-allocate-at-init.md) keeps association work
// at scene setup. Control-point bytes borrow the same mission archives until
// prepared by an animation consumer; I.1 exposes that immutable lifetime.
// See stage-scene-presentation.md.
#pragma once
#include <functional>

#include "bw/formats/model.hpp"
#include "bw/formats/stage_definitions.hpp"
#include "bw/render/world.hpp"
namespace bw::game {
struct ResidentModelView {
  std::string_view name;
  std::reference_wrapper<const formats::ModelDefinition> definition;
  render::WorldModelHandle handle;
  std::span<const std::int32_t> body_joints;
  const formats::PhysicsCollisionDefinition* physics_collision{};
  std::span<const std::uint8_t> control_points{};
};
}  // namespace bw::game
