// Original mission loading presentation owns its selected title background,
// system art and font. Resource service/output outlive this main-thread owner.
// prepare performs setup once; show submits bounded commands without resource
// loads. Original item indexes loading text independently of background choice.
// The caller supplies elapsed presentation time and clears this owner before
// publishing the world. Errors unwind partial leases/textures. Core I.1
// (#ri-explicit) makes time and readiness visible; implementation E.6
// (#re-raii) retains partial owners. Performance GPU.1
// (guidelines/gpu/GPU.1-keep-data-on-device.md) keeps image pixels resident.
// Source and production frame-pumping comparison: loading-presentation.md.
#pragma once
#include <memory>
#include <string_view>

#include "bw/content/resources/service.hpp"
#include "bw/render/output/sprites.hpp"

namespace bw::game {
class LoadingSurface final {
 public:
  LoadingSurface(resources::ResourceService& resources,
                 render::SpriteOutput& renderer);
  ~LoadingSurface();
  LoadingSurface(const LoadingSurface&) = delete;
  LoadingSurface& operator=(const LoadingSurface&) = delete;
  LoadingSurface(LoadingSurface&&) = delete;
  LoadingSurface& operator=(LoadingSurface&&) = delete;
  void prepare(std::string_view selected_background,
               resources::ArchiveBytes localized_text,
               std::span<const std::uint8_t> controller_icons);
  // Return the separate whole-screen fade; the host applies it after sprite
  // composition so overlapping artwork retains its source coverage.
  float show(unsigned item, double elapsed_seconds);
  void end() noexcept;

 private:
  struct State;
  std::shared_ptr<State> state_;
};
}  // namespace bw::game
