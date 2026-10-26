// Original sprite shader compatibility: add the per-sprite light constant to
// vertex color, then saturate before texture modulation. The PC shader pair is
// vs_2_0/ps_2_0; its COLOR interpolators clamp to [0,1], unlike unrestricted
// Metal varyings. This is not framebuffer additive blending. Pure copied
// values, no ownership/allocation/clock. Both-phase Core I.1/E.4 and
// performance MEM.9 apply as in controller_settings.hpp. Evidence:
// native-pause/controller-options.md.
#pragma once
#include <algorithm>
#include <cmath>
#include <stdexcept>

#include "bw/render/output/sprites.hpp"
namespace bw::game {
inline render::Color original_sprite_tint(render::Color base,
                                          render::Color light) {
  const auto channel = [](float value) {
    if (!std::isfinite(value)) {
      throw std::invalid_argument("Nonfinite source sprite color");
    }
    return std::clamp(value, 0.F, 1.F);
  };
  return {channel(base.red + light.red), channel(base.green + light.green),
          channel(base.blue + light.blue), channel(base.alpha + light.alpha)};
}
}  // namespace bw::game
