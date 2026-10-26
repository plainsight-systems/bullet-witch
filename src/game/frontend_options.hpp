// Frontend Options presentation boundary. The caller owns this stable output
// through FrontEnd teardown; only copied observations and explicit view
// residency operations cross it. Profile, input devices and clocks are never
// exposed. Switching views retires commands before releasing textures; failure
// leaves an unready but destructible owner. Preparation allocates once per
// transition; ordinary show calls use fixed resident data. Core I.1
// (#ri-explicit) and performance MEM.9 (guidelines/memory/
// MEM.9-embedded-allocate-at-init.md), consulted for design and implementation,
// keep ownership explicit and frame storage bounded. Original task4d4c10
// shares Options/controller policy with mission pause; frontend composition
// owns only its extra child routes. See frontend-options.md.
#pragma once
#include "pause_options.hpp"
namespace bw::game {
enum class FrontendOptionsView : std::uint8_t { none, options, controls };
class FrontendOptionsOutput {
 public:
  FrontendOptionsOutput() = default;
  virtual ~FrontendOptionsOutput() = default;
  FrontendOptionsOutput(const FrontendOptionsOutput&) = delete;
  FrontendOptionsOutput& operator=(const FrontendOptionsOutput&) = delete;
  FrontendOptionsOutput(FrontendOptionsOutput&&) = delete;
  FrontendOptionsOutput& operator=(FrontendOptionsOutput&&) = delete;
  virtual void activate(FrontendOptionsView view) = 0;
  virtual bool ready() = 0;
  virtual void show(const PauseOptionsPresentation& view) = 0;
  virtual void show(const ControllerOptionsPresentation& view) = 0;
  virtual void end() noexcept = 0;
};
struct FrontendOptionsServices {
  FrontendOptionsServices(PauseOptionsServices settings_control,
                          FrontendOptionsOutput& presentation_output)
      : settings(settings_control), output(presentation_output) {}
  PauseOptionsServices settings;
  std::reference_wrapper<FrontendOptionsOutput> output;
};
}  // namespace bw::game
