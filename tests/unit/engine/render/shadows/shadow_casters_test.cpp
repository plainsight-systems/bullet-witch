// In-process source owner contracts and independently executed original volume
// witnesses, inside the existing resources scenario. Both MCP phases: I.1
// (#ri-explicit) keeps each state/coordinate explicit; MEM.9
// (guidelines/memory/MEM.9-embedded-allocate-at-init.md) bounds fixture
// storage.
#include "bw/render/shadows/shadow_casters.hpp"

#include <cmath>
#include <limits>
#include <stdexcept>

namespace {
using bw::scene::visibility::make_shadow_selection_volume;
using bw::scene::visibility::query_model_shadow_caster;
using bw::scene::visibility::query_placed_shadow_caster;
using bw::scene::visibility::query_static_shadow_caster;
using bw::scene::visibility::ShadowCasterAdmission;
using bw::scene::visibility::ShadowModelCaster;
using bw::scene::visibility::ShadowPlacedCaster;
using bw::scene::visibility::ShadowSelectionCamera;
void require(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}
ShadowSelectionCamera camera() {
  return {{{{1, 0, 0}, {0, 1, 0}, {0, 0, 1}, {0, 0, 0}}},
          {-1, 1, -1, 1, 1, 10}};
}
void model_contract() {
  const auto view = camera();
  const auto volume =
      bw::math::make_perspective_frustum(view.world, view.extents);
  ShadowModelCaster owner{{{0, 0, 4}, 1}, {{0, 0, 5}, 1}, true, false, false};
  const auto query = [&](float threshold) {
    return query_model_shadow_caster(view, volume, threshold, owner);
  };
  require(query(.25F) == ShadowCasterAdmission::accepted,
          "Projected threshold equality admits a model");
  require(query(std::nextafter(.25F, 1.F)) == ShadowCasterAdmission::too_small,
          "Projected threshold rejects just above equality");
  owner.projected.center[2] = -1;
  require(query(100) == ShadowCasterAdmission::accepted,
          "Behind-camera projected sphere uses max-finite size");
  owner.intersection.center = {0, 0, .5F};
  owner.intersection.radius = 0;
  require(query(100) == ShadowCasterAdmission::accepted,
          "Negative accepted volume score is not an outside test");
  owner.intersection = {{0, 0, 11}, 1};
  require(query(100) == ShadowCasterAdmission::accepted,
          "Far-plane sphere contact is admitted");
  owner.intersection.center[2] = 11.01F;
  require(query(100) == ShadowCasterAdmission::outside_volume,
          "Independent group bound controls volume rejection");
  owner.force_visible = true;
  owner.disabled = true;
  require(query(100) == ShadowCasterAdmission::forced,
          "Forced model state precedes disable and volume");
  owner.pose_ready = false;
  require(query(100) == ShadowCasterAdmission::inactive,
          "Force visibility cannot prepare a missing pose");
  owner.pose_ready = true;
  owner.force_visible = false;
  require(query(100) == ShadowCasterAdmission::inactive,
          "Disable flag rejects an otherwise projected model");
}
void placed_contract() {
  auto view = camera();
  const auto volume =
      bw::math::make_perspective_frustum(view.world, view.extents);
  ShadowPlacedCaster owner{{{0, 0, 500}, 12}, 12, 0};
  const auto query = [&](const ShadowSelectionCamera* active) {
    return query_placed_shadow_caster(view, volume, 0, owner, active);
  };
  require(query(&view) == ShadowCasterAdmission::inactive,
          "Registration alone does not enable placed shadow casting");
  owner.flags = 0x1000;
  require(query(&view) == ShadowCasterAdmission::outside_volume,
          "Ordinary placed owner uses light volume");
  owner.flags = 0x1007;
  require(query(&view) == ShadowCasterAdmission::accepted,
          "Large mode7 owner uses expanded active camera");
  require(query(nullptr) == ShadowCasterAdmission::outside_volume,
          "Missing active camera retains ordinary light volume");
  owner.sphere.center[2] = 1012;
  require(query(&view) == ShadowCasterAdmission::accepted,
          "Large owner radial threshold includes contact");
  owner.sphere.center[2] = 1012.25F;
  require(query(&view) == ShadowCasterAdmission::outside_volume,
          "Large owner radial threshold rejects beyond contact");
  owner.sphere.center[2] = 500;
  owner.original_radius = 10;
  require(query(&view) == ShadowCasterAdmission::outside_volume,
          "Large mode requires original radius strictly greater than ten");
}
void verify_volume(const ShadowSelectionCamera& view,
                   const std::array<float, 21>& original) {
  const auto actual =
      make_shadow_selection_volume(view, 100, {.3F, .8F, -.399F});
  for (std::size_t plane = 0; plane < actual.planes.size(); ++plane) {
    for (std::size_t lane = 0; lane < 4; ++lane) {
      const float tolerance = lane == 3 ? .02F : .00001F;
      require(std::abs(actual.planes.at(plane).at(lane) -
                       original.at(plane * 4 + lane)) < tolerance,
              "Selection planes match independent original execution");
    }
  }
  require(std::abs(actual.depth_range - original.back()) < .01F,
          "Selection far bound matches original execution");
}
void selection_volume_contract() {
  auto view = camera();
  view.extents = {-1, 1, -.5F, .5F, 1, 1000};
  // Original457620 executed until registry selection. Final raster matrix was
  // stubbed identity; that output is separate and unused by this volume fit.
  constexpr std::array<float, 21> kOriginal{
      -.798646688F, .001676839F,  -.601797879F, -19.7756348F, .803667545F,
      .011916773F,  .594959199F,  -140.459473F, .510149956F,  -.528574049F,
      -.678495884F, -9.60327148F, -.505182743F, .541617095F,  .671893895F,
      -144.788574F, -.318142235F, -.848379254F, .423129171F,  -116.545792F,
      5116.545898F};
  verify_volume(view, kOriginal);
  view.world[3] = {17, -3, 41};
  constexpr std::array<float, 21> kTranslated{
      -.798647642F, .001677483F,  -.601796627F, 18.4829102F, .803668439F,
      .011916101F,  .594957948F,  -178.482178F, .510149717F, -.528574407F,
      -.678495526F, 7.96166992F,  -.505182505F, .541617513F, .671893597F,
      -162.129150F, -.318142235F, -.848379254F, .423129171F, -131.031921F,
      5116.546875F};
  verify_volume(view, kTranslated);
  bool rejected = false;
  try {
    static_cast<void>(make_shadow_selection_volume(view, 100, {0, 0, 0}));
  } catch (const std::invalid_argument&) {
    rejected = true;
  }
  require(rejected, "Undefined light direction cannot publish a volume");
}
}  // namespace
void shadow_casters_tests() {
  require(query_static_shadow_caster(1) == ShadowCasterAdmission::accepted,
          "Static owner size1 threshold equality is retained");
  require(query_static_shadow_caster(std::nextafter(1.F, 2.F)) ==
              ShadowCasterAdmission::too_small,
          "Static owner size is not forced to infinite");
  model_contract();
  placed_contract();
  selection_volume_contract();
}
