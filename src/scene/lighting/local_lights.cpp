// Reconstruct the source active-light consumer (5f6e80/5461f0/546070).
// Header owns lifetime/cost/capability contracts; SL.con.3 validates
// preparation and query bounds, MEM.9 uses one prepared scratch slot per
// admitted emitter. The view path preserves the separate source camera/frustum
// candidate list and fixed four-light rank. Both-phase I.1 (#ri-explicit) names
// model versus view admission; MEM.9 retains existing per-emitter scratch.
#include "bw/scene/lighting/local_lights.hpp"

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
  if (!std::isfinite(radius)) {
    throw std::invalid_argument("Invalid light influence radius");
  }
  return radius;
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
      settings.maximum_direct > kMaximumLocalLights ||
      settings.overflow_irradiance) {
    throw std::invalid_argument("Unsupported local-light selection settings");
  }
  const float threshold =
      settings.contribution_threshold + settings.selection_margin;
  if (!std::isfinite(threshold)) {
    throw std::invalid_argument("Invalid light threshold");
  }
  lights_.reserve(lights.size());
  for (const auto& light : lights) {
    if (!math::all_finite(light.position) || !math::all_finite(light.diffuse) ||
        !math::all_finite(light.specular) ||
        !math::all_finite(light.attenuation) ||
        std::ranges::any_of(light.diffuse, [](float v) { return v < 0; }) ||
        std::ranges::any_of(light.specular, [](float v) { return v < 0; }) ||
        std::ranges::any_of(light.attenuation, [](float v) { return v < 0; }) ||
        (light.attenuation[1] == 0 && light.attenuation[2] == 0)) {
      throw std::invalid_argument("Invalid authored local light");
    }
    lights_.push_back(
        {light, influence_radius(light, threshold),
         influence_radius(light, settings.contribution_threshold)});
  }
  candidates_.resize(lights_.size());
  view_indices_.resize(lights_.size());
  active_indices_.resize(lights_.size());
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
    const auto delta = math::subtract(emitter.light.position, query.center);
    const float squared =
        delta[1] * delta[1] + delta[0] * delta[0] + delta[2] * delta[2];
    const float distance =
        static_cast<float>(std::sqrt(static_cast<double>(squared)));
    if (squared > 0 && !(distance - query.radius < emitter.radius)) continue;
    candidates_[count++] = {i, rank(i, query)};
  }
  return finish_selection(count, settings_.maximum_direct);
}
SelectedLocalLights LocalLights::select_view(const LocalLightViewQuery& query) {
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
  std::size_t count = 0;
  for (std::size_t i = 0; i < lights_.size(); ++i) {
    const auto& emitter = lights_[i];
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
    candidates_[item] = {i, rank(i, rank_query)};
  }
  view_prepared_ = true;
  return finish_selection(count, kMaximumLocalLights);
}
void LocalLights::order_view_candidates(std::size_t count) {
  std::size_t gap = count;
  bool swapped = count > 1;
  constexpr std::size_t kNumerator = 10, kDenominator = 13, kGapNine = 9,
                        kGapTen = 10, kGapEleven = 11;
  while (gap > 1 || swapped) {
    gap = (gap * kNumerator) / kDenominator;
    if (gap == 0) gap = 1;
    if (gap == kGapNine || gap == kGapTen) gap = kGapEleven;
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
float LocalLights::rank(std::size_t index, const LocalLightQuery& query) const {
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
  return math::accumulated_dot(math::scale(rgba, factor), kLuminance);
}
SelectedLocalLights LocalLights::finish_selection(std::size_t count,
                                                  std::size_t maximum) {
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
  return result;
}
}  // namespace bw::scene
