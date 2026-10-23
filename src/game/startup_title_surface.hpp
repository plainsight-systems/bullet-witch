// Concrete game title asset composition behind TitleSurface. Borrows the
// encoded resource service and sprite renderer, both outliving this object;
// owns exclusive resource tags, retained leases and uploaded texture handles.
// Construction takes a loading-screen selector [0,9], supplied per activation.
// Main-thread only; parsing/upload failure throws and RAII teardown releases
// partial construction. Algorithm: request title/font -> decode/upload ->
// request selected loading group -> decode/upload -> publish ready -> submit
// original title sprites and font glyphs -> retire draw commands and assets.
// Fixed draw storage; images decode/upload once per activation. No GPU
// readback. Design MCP R.20 (#rr-owner) retains archive lifetime; performance
// GPU.1 (guidelines/gpu/GPU.1-keep-data-on-device.md) keeps textures resident.
// E.6
// (#re-raii) and COPY.4
// (guidelines/copy-move/COPY.4-rule-of-five-rule-of-zero.md) preserve cleanup
// and avoid owned payload copies during implementation.
#pragma once
#include <functional>
#include <memory>

#include "bw/render/sprites.hpp"
#include "bw/resources/service.hpp"
#include "title_surface.hpp"

namespace bw::game {
class StartupTitleSurface final : public TitleSurface {
 public:
  StartupTitleSurface(resources::ResourceService& resources,
                      render::SpriteOutput& renderer,
                      std::function<unsigned()> choose_loading);
  ~StartupTitleSurface() override;
  StartupTitleSurface(const StartupTitleSurface&) = delete;
  StartupTitleSurface& operator=(const StartupTitleSurface&) = delete;
  StartupTitleSurface(StartupTitleSurface&&) = delete;
  StartupTitleSurface& operator=(StartupTitleSurface&&) = delete;
  void begin() override;
  bool ready() override;
  void show(bool visible, TitlePresentation presentation) override;
  void end() noexcept override;

 private:
  struct State;
  std::unique_ptr<State> state_;
};
}  // namespace bw::game
