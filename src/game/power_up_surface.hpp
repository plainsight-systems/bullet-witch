// Original power-up sprite/font resource owner. Borrows ResourceService and
// SpriteOutput for its lifetime; both outlive this stable main-thread object.
// begin requests resources, ready prepares art/font/text once, show emits
// native layout from copied progression/selection and supplied frontend
// channels, end releases commands, textures and resource interests. No
// progression mutation. Failed setup unwinds acquired resources; invalid
// snapshots throw before draw. Init work scales with resource bytes; frame work
// uses fixed sprite storage. Core F.7 (#rf-smart) restricts nonowning borrows;
// performance MEM.9 (guidelines/memory/MEM.9-embedded-allocate-at-init.md)
// bounds storage. Source ownership and geometry: power-up-presentation.md.
#pragma once
#include <memory>

#include "bw/render/sprites.hpp"
#include "bw/resources/service.hpp"
#include "power_up_presentation.hpp"
namespace bw::game {
class PowerUpSurface final : public PowerUpOutput {
 public:
  PowerUpSurface(resources::ResourceService& resources,
                 render::SpriteOutput& output,
                 resources::ArchiveBytes input_icons);
  ~PowerUpSurface() override;
  PowerUpSurface(const PowerUpSurface&) = delete;
  PowerUpSurface& operator=(const PowerUpSurface&) = delete;
  PowerUpSurface(PowerUpSurface&&) = delete;
  PowerUpSurface& operator=(PowerUpSurface&&) = delete;
  void begin() override;
  bool ready() override;
  void show(const PowerUpPresentation& presentation) override;
  void end() noexcept override;

 private:
  struct State;
  std::shared_ptr<State> state_;
};
}  // namespace bw::game
