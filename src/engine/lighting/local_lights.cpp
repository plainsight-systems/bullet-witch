// Reconstruct the source active-light consumer (5f6e80/5461f0/546070).
// Header owns lifetime/cost/capability contracts; SL.con.3 validates
// preparation and query bounds, MEM.9 uses one prepared scratch slot per
// admitted emitter. The view path preserves the separate source camera/frustum
// candidate list and fixed four-light rank. Both-phase I.1 (#ri-explicit) names
// model versus view admission; MEM.9 retains existing per-emitter scratch.
// Overflow implementation/design retrieval: I.1 exposes unscaled residual
// coefficients; MEM.9 stores ranked radiance once in prepared candidate slots.
// Publication implementation: I.1 separates validated values from committed
// candidate lists; MEM.9 requires no allocation in either phase.
#include "bw/lighting/local_lights.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
namespace bw::scene {
namespace {
float influence_radius(const LocalLight& light, float threshold) {
  if (light.attenuation[0] == 0 || !(light.attenuation[1] * threshold > 0)) {
    return std::numeric_limits<float>::max();
  }
  const float peak = *std::ranges::max_element(light.diffuse);
  const float base = (peak - light.attenuation[2] * threshold) /
                     (light.attenuation[1] * threshold);
  const float radius = static_cast<float>(
      std::pow(static_cast<double>(base),
               static_cast<double>(1.F / light.attenuation[0])));
  // Original fractional power can produce an unordered radius as a fading
  // light falls below the attenuation threshold. Preserve it internally:
  // frustum comparisons admit it, noncoincident model-distance tests do not.
  // Published positions/colors remain finite; no radius reaches the shader.
  return radius;
}
// Original projection uses receiver-to-emitter world direction, the signed
// real SH basis and one recovered energy multiplier. Preserve multiplication
// order: basis * projection scale * attenuated RGB, then sorted accumulation.
void add_overflow_irradiance(IrradianceCoefficients& coefficients,
                             const math::Vector3<float>& direction,
                             const math::Vector3<float>& radiance) {
  const float squared = math::accumulated_dot(direction, direction);
  if (!(squared > 0)) {
    return;
  }
  const float inverse_length =
      1.F / static_cast<float>(std::sqrt(static_cast<double>(squared)));
  const auto unit = math::scale(direction, inverse_length);
  const float x = unit[0], y = unit[1], z = unit[2];
  constexpr float kConstant = .282094806432724F, kLinear = .48860251903533936F,
                  kMixedQuadratic = 1.0925484895706177F,
                  kZonalQuadratic = .31539157032966614F,
                  kDifferenceQuadratic = .5462742447853088F,
                  kProjectionScale = 2.9567911624908447F,
                  kZonalMultiplier = 3.F;
  const std::array<float, kIrradianceCoefficientCount> basis{
      kConstant,
      -y * kLinear,
      z * kLinear,
      -x * kLinear,
      (x * y) * kMixedQuadratic,
      -(z * y) * kMixedQuadratic,
      ((z * kZonalMultiplier) * z - 1.F) * kZonalQuadratic,
      -(x * z) * kMixedQuadratic,
      (x * x - y * y) * kDifferenceQuadratic};
  for (std::size_t i = 0; i < basis.size(); ++i) {
    coefficients.at(i) =
        math::add(coefficients.at(i),
                  math::scale(radiance, basis.at(i) * kProjectionScale));
  }
}
}  // namespace
LocalLights::LocalLights(std::span<const LocalLight> lights,
                         LocalLightSettings settings,
                         const std::shared_ptr<memory::Domain>& domain)
    : lights_(memory::Allocator<Emitter>(domain)),
      candidates_(memory::Allocator<Candidate>(domain)),
      view_indices_(memory::Allocator<std::size_t>(domain)),
      active_indices_(memory::Allocator<std::size_t>(domain)),
      settings_(settings) {
  if (!std::isfinite(settings.contribution_threshold) ||
      !std::isfinite(settings.selection_margin) ||
      !(settings.contribution_threshold > 0) || settings.selection_margin < 0 ||
      settings.maximum_direct > kMaximumLocalLights) {
    throw std::invalid_argument("Unsupported local-light selection settings");
  }
  static_count_ = lights.size();
  lights_.reserve(lights.size());
  for (const auto& light : lights) {
    lights_.push_back(prepare_light(light));
  }
  candidates_.resize(lights_.size());
  view_indices_.resize(lights_.size());
  active_indices_.resize(lights_.size());
}
// Validate a complete candidate before publishing it into prepared storage.
// E.4 (#re-design-invariants) preserves live state on rejection; MEM.9
// (guidelines/memory/MEM.9-embedded-allocate-at-init.md) keeps updates bounded.
LocalLights::Emitter LocalLights::prepare_light(const LocalLight& light) const {
  const float threshold =
      settings_.contribution_threshold + settings_.selection_margin;
  if (!std::isfinite(threshold) || !math::all_finite(light.position) ||
      !math::all_finite(light.diffuse) || !math::all_finite(light.specular) ||
      !math::all_finite(light.attenuation) ||
      std::ranges::any_of(light.diffuse, [](float v) { return v < 0; }) ||
      std::ranges::any_of(light.specular, [](float v) { return v < 0; }) ||
      std::ranges::any_of(light.attenuation, [](float v) { return v < 0; }) ||
      (light.attenuation[1] == 0 && light.attenuation[2] == 0)) {
    throw std::invalid_argument("Invalid authored local light");
  }
  return {light, true, influence_radius(light, threshold),
          influence_radius(light, settings_.contribution_threshold)};
}
void LocalLights::reserve_dynamic(std::size_t count) {
  if (lights_.size() != static_count_ ||
      count > lights_.max_size() - static_count_) {
    throw std::logic_error(
        "Dynamic light storage already prepared or too large");
  }
  const auto total = static_count_ + count;
  // Reserve every vector before changing logical sizes, so allocation failure
  // leaves the previous published lights/query scratch usable.
  lights_.reserve(total);
  candidates_.reserve(total);
  view_indices_.reserve(total);
  active_indices_.reserve(total);
  lights_.resize(total, Emitter{.light = {}, .enabled = false});
  candidates_.resize(total);
  view_indices_.resize(total);
  active_indices_.resize(total);
  view_prepared_ = false;
}
void LocalLights::clear_static_emitters() {
  for (auto& emitter : std::span(lights_).first(static_count_)) {
    emitter.enabled = false;
  }
  view_prepared_ = false;
}
void LocalLights::validate_dynamic(
    std::size_t index, const std::optional<LocalLight>& light) const {
  if (index < static_count_ || index >= lights_.size()) {
    throw std::out_of_range("Dynamic light index outside reserved slots");
  }
  if (light) (void)prepare_light(*light);
}
void LocalLights::update_dynamic(std::size_t index,
                                 const std::optional<LocalLight>& light) {
  if (index < static_count_ || index >= lights_.size()) {
    throw std::out_of_range("Dynamic light index outside reserved slots");
  }
  auto& destination = lights_.at(index);
  auto candidate =
      light ? prepare_light(*light) : Emitter{.light = {}, .enabled = false};
  if (destination.enabled && candidate.enabled) {
    candidate.previous_view_depth = destination.previous_view_depth;
  }
  destination = candidate;
  view_prepared_ = false;
}
SelectedLocalLights LocalLights::select(const LocalLightQuery& query) {
  return select_model(query, false);
}
SelectedLocalLights LocalLights::select_active(const LocalLightQuery& query) {
  if (!view_prepared_) {
    throw std::logic_error("Model light query requires a published view");
  }
  return select_model(query, true);
}
SelectedLocalLights LocalLights::select_model(const LocalLightQuery& query,
                                              bool active) {
  if (!math::all_finite(query.center) || !std::isfinite(query.radius) ||
      query.radius < 0) {
    throw std::invalid_argument("Invalid model light query");
  }
  std::size_t count = 0;
  const auto total = active ? active_count_ : lights_.size();
  for (std::size_t item = 0; item < total; ++item) {
    const auto i = active ? active_indices_[item] : item;
    const auto& emitter = lights_[i];
    if (!emitter.enabled) {
      continue;
    }
    const auto delta = math::subtract(emitter.light.position, query.center);
    const float squared =
        delta[1] * delta[1] + delta[0] * delta[0] + delta[2] * delta[2];
    const float distance =
        static_cast<float>(std::sqrt(static_cast<double>(squared)));
    if (squared > 0 && !(distance - query.radius < emitter.radius)) {
      continue;
    }
    candidates_[count++] = rank(i, query);
  }
  return finish_selection(count, settings_.maximum_direct,
                          settings_.overflow_irradiance);
}
void LocalLights::validate_view(const LocalLightViewQuery& query) {
  if (!math::all_finite(query.eye) ||
      !std::isfinite(query.frustum.depth_range) ||
      !(query.frustum.depth_range > 0)) {
    throw std::invalid_argument("Invalid view light query");
  }
  constexpr float kUnitTolerance = .0001F;
  for (const auto& plane : query.frustum.planes) {
    const math::Vector3<float> normal{plane[0], plane[1], plane[2]};
    if (!math::all_finite(plane) ||
        std::abs(math::dot(normal, normal) - 1.F) > kUnitTolerance) {
      throw std::invalid_argument("Invalid view light frustum");
    }
  }
}
SelectedLocalLights LocalLights::select_view(const LocalLightViewQuery& query) {
  validate_view(query);
  std::size_t count = 0;
  for (std::size_t i = 0; i < lights_.size(); ++i) {
    const auto& emitter = lights_[i];
    if (!emitter.enabled) {
      continue;
    }
    if (math::intersects(query.frustum, emitter.light.position,
                         emitter.visibility_radius)) {
      view_indices_[count++] = i;
    }
  }
  // Original scene preparation orders the first list using last publication's
  // depth, then updates that depth while deriving the tighter actor subset.
  order_view_candidates(count);
  active_count_ = 0;
  constexpr float kCameraReceiverRadius = .01F;
  const LocalLightQuery rank_query{query.eye, kCameraReceiverRadius};
  for (std::size_t item = 0; item < count; ++item) {
    const auto i = view_indices_[item];
    auto& emitter = lights_[i];
    if (math::intersects(query.frustum, emitter.light.position,
                         emitter.radius)) {
      active_indices_[active_count_++] = i;
      const auto& plane = query.frustum.planes.back();
      const auto& position = emitter.light.position;
      const float distance = plane[0] * position[0] + plane[1] * position[1] +
                             plane[2] * position[2] + plane[3];
      emitter.previous_view_depth =
          (query.frustum.depth_range + distance) - emitter.radius;
    } else {
      emitter.previous_view_depth = -std::numeric_limits<float>::max();
    }
    // The global array retains a candidate even if the tighter cull removes
    // its active-list node. Both source consumers keep this ordered snapshot.
    candidates_[item] = rank(i, rank_query);
  }
  view_prepared_ = true;
  return finish_selection(count, kMaximumLocalLights, false);
}
void LocalLights::order_view_candidates(std::size_t count) {
  std::size_t gap = count;
  bool swapped = count > 1;
  constexpr std::size_t kNumerator = 10, kDenominator = 13, kGapNine = 9,
                        kGapTen = 10, kGapEleven = 11;
  while (gap > 1 || swapped) {
    gap = (gap * kNumerator) / kDenominator;
    if (gap == 0) {
      gap = 1;
    }
    if (gap == kGapNine || gap == kGapTen) {
      gap = kGapEleven;
    }
    swapped = false;
    for (std::size_t i = 0; i + gap < count; ++i) {
      if (lights_[view_indices_[i]].previous_view_depth >
          lights_[view_indices_[i + gap]].previous_view_depth) {
        std::swap(view_indices_[i], view_indices_[i + gap]);
        swapped = true;
      }
    }
  }
}
LocalLights::Candidate LocalLights::rank(std::size_t index,
                                         const LocalLightQuery& query) const {
  const auto& emitter = lights_[index];
  const auto delta = math::subtract(emitter.light.position, query.center);
  const float squared =
      delta[1] * delta[1] + delta[0] * delta[0] + delta[2] * delta[2];
  const float distance =
      static_cast<float>(std::sqrt(static_cast<double>(squared)));
  const float edge = std::max(distance - query.radius, 0.F);
  const auto& attenuation = emitter.light.attenuation;
  const float power = static_cast<float>(
      std::pow(static_cast<double>(edge), static_cast<double>(attenuation[0])));
  const float factor = 1.F / (power * attenuation[1] + attenuation[2]);
  // Preserve source RGBA dot: alpha*zero retains unordered coincidence scores.
  const math::Vector<float, 4> rgba{emitter.light.diffuse[0],
                                    emitter.light.diffuse[1],
                                    emitter.light.diffuse[2], 1};
  constexpr math::Vector<float, 4> kLuminance{
      .298911988735199F, .5866109728813171F, .11447799950838089F, 0};
  const auto radiance = math::scale(rgba, factor);
  return {index,
          math::accumulated_dot(radiance, kLuminance),
          {radiance[0], radiance[1], radiance[2]},
          delta};
}
SelectedLocalLights LocalLights::finish_selection(std::size_t count,
                                                  std::size_t maximum,
                                                  bool accumulate_irradiance) {
  // Original descending comb sort; equal scores never directly swap. Preserve
  // its gap sequence and source registration order rather than a new heuristic.
  std::size_t gap = count;
  bool swapped = true;
  constexpr std::size_t kShrink = 0xc4ec, kShift = 16, kGapNine = 9,
                        kGapTen = 10, kGapEleven = 11;
  while (gap > 1 || swapped) {
    gap = (gap * kShrink) >> kShift;
    if (gap == 0) {
      gap = 1;
    } else if (gap == kGapNine || gap == kGapTen) {
      gap = kGapEleven;
    }
    swapped = false;
    for (std::size_t i = 0; i + gap < count; ++i) {
      if (candidates_[i].luminance < candidates_[i + gap].luminance) {
        std::swap(candidates_[i], candidates_[i + gap]);
        swapped = true;
      }
    }
  }
  SelectedLocalLights result;
  result.count = std::min(count, maximum);
  result.contribution_threshold = settings_.contribution_threshold;
  for (std::size_t i = 0; i < result.count; ++i) {
    result.lights.at(i) = lights_[candidates_[i].index].light;
  }
  if (accumulate_irradiance) {
    // Only lights beyond the direct budget contribute. This is the original
    // irradiance projection, not an ambient estimate from total light energy.
    for (std::size_t i = result.count; i < count; ++i) {
      const auto& candidate = candidates_[i];
      add_overflow_irradiance(result.residual_irradiance,
                              candidate.receiver_to_light,
                              candidate.attenuated_diffuse);
    }
  }
  return result;
}
}  // namespace bw::scene
