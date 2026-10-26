// Source local-light selection. The owning game component publishes enabled
// emitter positions/colors/attenuation after attachment updates. Construction
// validates and copies them into the supplied domain; replace/update belongs to
// that owner, not a shader or asset-name whitelist. Selection uses model-group
// center and unscaled authored group radius, source distance admission and
// descending attenuated luminance; retain up to four direct pixel lights.
// Optional overflow projects only the unselected model candidates into the
// original nine-coefficient SH basis, preserving sorted accumulation order.
// Current source initialization leaves this disabled. Setup O(lights), sampling
// original comb sort O(lights^2) worst-case with fixed prepared scratch and no
// allocation. Owner-thread queries, not concurrent. Both phases: I.1/SL.con.3
// expose capabilities and bounded packets; MEM.9 and CDSA.32 prepare source
// consumer data once. See character-light-sampling.md. View selection
// independently uses source sphere/ frustum admission and ranks at the eye with
// fixed capacity four. Both-phase I.1 (#ri-explicit) keeps the two query
// contracts explicit; MEM.9 retains preparation-only allocation. See
// lighting-scene-producer-contract.md.
#pragma once
#include <span>

#include "bw/math/frustum.hpp"
#include "bw/memory/allocator.hpp"
#include "irradiance_field.hpp"
namespace bw::scene {
inline constexpr std::size_t kMaximumLocalLights = 4;
struct LocalLight {
  math::Vector3<float> position{}, diffuse{}, specular{}, attenuation{};
};
struct LocalLightSettings {
  float contribution_threshold{}, selection_margin{};
  std::uint32_t maximum_direct{};
  bool overflow_irradiance{};
};
struct LocalLightQuery {
  math::Vector3<float> center{};
  float radius{};
};
// Scene-global static-model selection uses the camera frustum and ranks at the
// eye with the original 0.01 radius, without actor distance admission. The
// source candidate list retains lights passing the contribution-radius cull
// even when the later tighter influence cull removes them from the actor list.
struct LocalLightViewQuery {
  math::Vector3<float> eye{};
  math::PerspectiveFrustum frustum;
};
struct SelectedLocalLights {
  std::array<LocalLight, kMaximumLocalLights> lights{};
  std::size_t count{};
  float contribution_threshold{};
  // Add directly to already sampled/scaled model irradiance before packing.
  // These coefficients include source projection and emitter attenuation,
  // but no SHLT file or owner intensity. Disabled policy and view selection
  // return zero; view preparation discards this source temporary explicitly.
  IrradianceCoefficients residual_irradiance{};
};
class LocalLights {
 public:
  LocalLights(std::span<const LocalLight> lights, LocalLightSettings settings,
              const std::shared_ptr<memory::Domain>& domain);
  SelectedLocalLights select(const LocalLightQuery& query);
  // select() ranks an explicitly supplied full emitter set. Scene consumers
  // use select_active(), whose candidate list requires a successful view
  // publication. select_view() prepares both source lists once; invalid input
  // preserves the previous publication. Replacing the owner resets history.
  SelectedLocalLights select_active(const LocalLightQuery& query);
  SelectedLocalLights select_view(const LocalLightViewQuery& query);
  std::size_t size() const { return lights_.size(); }

 private:
  struct Emitter {
    LocalLight light;
    float radius{}, visibility_radius{}, previous_view_depth{};
  };
  struct Candidate {
    std::size_t index{};
    float luminance{};
    math::Vector3<float> attenuated_diffuse{}, receiver_to_light{};
  };
  memory::Vector<Emitter> lights_;
  memory::Vector<Candidate> candidates_;
  memory::Vector<std::size_t> view_indices_, active_indices_;
  std::size_t active_count_{};
  bool view_prepared_{};
  LocalLightSettings settings_;
  SelectedLocalLights select_model(const LocalLightQuery& query, bool active);
  void order_view_candidates(std::size_t count);
  Candidate rank(std::size_t index, const LocalLightQuery& query) const;
  SelectedLocalLights finish_selection(std::size_t count, std::size_t maximum,
                                       bool accumulate_irradiance);
};
}  // namespace bw::scene
