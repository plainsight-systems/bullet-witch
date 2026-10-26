// Responsibility grouping preserves the existing contract and owner policies.
// Design Core SF.11 (#rs-contained), implementation SF.13
// (#rs-portable-header-id), and both-phase performance COPY.4
// (guidelines/copy-move/COPY.4-rule-of-five-rule-of-zero.md) keep direct
// includes portable and preserve special members without extra payload copies.
// Shared native frontend/in-mission Options and calibration presentation. A
// stable main-thread owner borrows resource/render capabilities, owns prepared
// art, archive bytes and bounded draw commands, and receives only copied
// settings. No input, profile mutation or audio/display settings authority is
// retained. begin/ready prepare tag22 residency; show performs no
// uploads/allocations; end retires partial or complete residency. Native
// animated panel positions, labels and calibration card come from the original
// menu data. Costume is the deliberate added mission row; frontend uses its
// original ten-row context. External child owners render themselves after
// Options conceals. Core E.6/R.14 and performance MEM.9/GPU.1 retrieved for
// both design and implementation require bounded views, RAII cleanup and
// resident textures.
#pragma once
#include <memory>

#include "bw/content/resources/service.hpp"
#include "bw/render/output/sprites.hpp"
#include "options.hpp"
namespace bw::game {
class OptionsSurface final : public OptionsOutput {
 public:
  OptionsSurface(resources::ResourceService& resources,
                 render::SpriteOutput& output,
                 resources::ArchiveBytes input_icons,
                 resources::ArchiveBytes menu_text,
                 OptionsContext context = OptionsContext::mission);
  ~OptionsSurface() override;
  OptionsSurface(const OptionsSurface&) = delete;
  OptionsSurface& operator=(const OptionsSurface&) = delete;
  OptionsSurface(OptionsSurface&&) = delete;
  OptionsSurface& operator=(OptionsSurface&&) = delete;
  void begin();
  bool ready();
  void end() noexcept;
  void show(const OptionsPresentation& presentation) override;

 private:
  struct State;
  std::shared_ptr<State> state_;
};
}  // namespace bw::game
