// Immutable mission asset observations shared by simulation and presentation.
// Definitions/device handles remain mission-owned and outlive both consumers;
// these views neither admit runtime mutation nor own a world/renderer lifetime.
// Design I.1 (#ri-explicit), C.9 (#rc-private); implementation E.6 (#re-raii)
// keeps resource cleanup with owners. Both-phase MEM.9
// (guidelines/memory/MEM.9-embedded-allocate-at-init.md): spans copy no
// payload.
#pragma once
#include <functional>

#include "bw/content/stage.hpp"
#include "bw/formats/background_placement.hpp"
#include "resident_model.hpp"
namespace bw::game {
// Definitions and model handles have identical order. Their mission owner must
// outlive the view; the renderer validates each handle at submission.
struct StageViewSource {
  std::reference_wrapper<const content::StageDefinition> definition;
  std::span<const render::WorldModelHandle> models;
  std::span<const render::WorldTextureHandle> textures{};
};
// Optional placed-prop snapshot; all borrowed inputs are consumed during view
// construction. This does not execute mission start or advance physics.
struct StageBackgroundSource {
  const formats::BackgroundPlacementDefinition* placements{};
  std::span<const ResidentModelView> models;
};
// Original mission light publications, borrowed only during construction.
// Alicia requires character data; enemy is retained as an explicit separate
// source capability for future actor admission, not sampled for Alicia.
struct StageCharacterLightSource {
  const content::UserDefinition* character{};
  const content::UserDefinition* enemy{};
};
}  // namespace bw::game
