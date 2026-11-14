#include "bw/rendering/postprocess/tone_mapping.hpp"

#include <cmath>
#include <numbers>

// Responsibility: the out-of-line pure function of
// bw/rendering/postprocess/tone_mapping.hpp (contract 3, the vignette). The
// ToneMapping object, its declaration and its pass are
// tone_mapping_pass.cpp's.
namespace bw::postprocess {

// The vignette space is the viewport's -1 to 1 position scaled by
// (1, aspect) * sqrt(2) / sqrt(1 + aspect^2); the mask is the cosine fourth
// falloff, 1 when the intensity is not above 0. The grade has one vignette
// value and the lens is centred (tone_mapping.hpp).
// The four scalars in the order the header declares them.
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
float vignette_mask(float u, float v, float aspect, float intensity) noexcept {
  if (intensity <= 0.0F) {
    return 1.0F;
  }
  const float scale = std::numbers::sqrt2_v<float> / std::sqrt(1.0F + aspect * aspect);
  const float qx = (2.0F * u - 1.0F) * scale * intensity;
  const float qy = (2.0F * v - 1.0F) * aspect * scale * intensity;
  const float inverse = 1.0F / (qx * qx + qy * qy + 1.0F);
  return inverse * inverse;
}

} // namespace bw::postprocess
