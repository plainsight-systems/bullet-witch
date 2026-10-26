// Overflow irradiance witnesses from independently executed original x86 code
// with synthetic lights. No original asset bytes. Design/implementation I.1
// checks explicit unscaled residual semantics; MEM.9 checks steady-state
// storage.
#include "bw/lighting/local_lights.hpp"

#include "../../../fixtures/camera_fixture.hpp"

namespace {
using bw::scene::IrradianceCoefficients;
void require_coefficients(const IrradianceCoefficients& actual,
                          const IrradianceCoefficients& expected) {
  for (std::size_t i = 0; i < actual.size(); ++i) {
    for (std::size_t channel = 0; channel < actual.at(i).size(); ++channel) {
      camera_fixture::require(
          std::abs(actual.at(i).at(channel) - expected.at(i).at(channel)) <
              2e-6F,
          "Residual matches original projection and accumulation");
    }
  }
}
}  // namespace
void local_lights_tests() {
  namespace scene = bw::scene;
  namespace math = bw::math;
  using camera_fixture::require;
  auto domain =
      bw::memory::Domain::create(std::make_shared<camera_fixture::Backing>());
  const std::array<scene::LocalLight, 6> lights{
      {{{1, 0, 2}, {4, 2, 1}, {}, {1, .4F, .5F}},
       {{-2, 3, -4}, {2, 5, 1}, {}, {2, .2F, 1}},
       {{4, -1, 3}, {3, 1, 6}, {}, {1.25F, .3F, .5F}},
       {{0, 0, 0}, {1, 1, 1}, {}, {1, 1, 1}},
       {{3, 2, 1}, {2, 4, 3}, {}, {1, .4F, .5F}},
       {{5, 4, -2}, {7, 3, 5}, {}, {.5F, .2F, 1}}}};
  scene::LocalLights enabled(lights, {.05F, .15F, 1, true}, domain);
  scene::LocalLights disabled(lights, {.05F, .15F, 1, false}, domain);
  const scene::LocalLightQuery query{{0, 0, 0}, .5F};
  const auto selected = enabled.select(query);
  require(
      selected.count == 1 && selected.lights[0].position == lights[5].position,
      "Overflow retains the strongest original direct selection");
  // 5461f0 including full projection and ordered accumulation, independently
  // executed with these six emitters. Coincident unselected emitter is skipped.
  const IrradianceCoefficients expected{
      {{5.003534317F, 4.306976318F, 4.221116066F},
       {-.801209688F, -2.303884745F, -.754374802F},
       {5.397086620F, 2.431111336F, 3.564060926F},
       {-4.616146564F, -3.645628452F, -5.079975605F},
       {.718065858F, 2.306263924F, 1.012876749F},
       {.393038809F, .278197467F, .348286629F},
       {3.810909033F, 1.098216772F, .060803197F},
       {-7.181293488F, -5.068212986F, -5.941339970F},
       {2.736653328F, 1.953735828F, 3.403736591F}}};
  require_coefficients(selected.residual_irradiance, expected);
  const auto unprojected = disabled.select(query);
  require(unprojected.count == selected.count &&
              unprojected.lights[0].position == selected.lights[0].position,
          "Overflow policy changes residual only, not direct-light ranking");
  require_coefficients(unprojected.residual_irradiance, {});
  scene::LocalLights four(lights, {.05F, .15F, 4, true}, domain);
  const auto moved = four.select({{1, -2, 2}, .25F});
  constexpr std::array<std::size_t, 4> kOrder{5, 0, 4, 2};
  require(moved.count == kOrder.size(), "Source direct capacity stays four");
  for (std::size_t i = 0; i < kOrder.size(); ++i) {
    require(
        moved.lights.at(i).position == lights.at(kOrder.at(i)).position,
        "Moving receiver changes direct/residual partition by source ranking");
  }
  const IrradianceCoefficients moved_expected{
      {{.340103924F, .516621590F, .281264693F},
       {-.378643572F, -.561356723F, -.317739189F},
       {-.403005332F, -.622261167F, -.329920053F},
       {.201502666F, .311130583F, .164960027F},
       {-.289097637F, -.435594082F, -.240265489F},
       {.578195274F, .871188164F, .480530977F},
       {.154315978F, .261450350F, .118604533F},
       {-.308630496F, -.484426200F, -.250031918F},
       {-.195662647F, -.273794085F, -.169618845F}}};
  require_coefficients(moved.residual_irradiance, moved_expected);
  require_coefficients(
      enabled.select({{1000, 1000, 1000}, 0}).residual_irradiance, {});
  const math::Affine3<float> identity{
      {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}, {0, 0, 0}}};
  const scene::LocalLightViewQuery view{
      {0, 0, 0},
      math::make_perspective_frustum(identity, {-1, 1, -1, 1, 1, 10})};
  require_coefficients(enabled.select_view(view).residual_irradiance, {});
  require_coefficients(enabled.select_active(query).residual_irradiance,
                       expected);
  const auto bytes = domain->snapshot().live_bytes;
  for (int i = 0; i < 20; ++i) {
    (void)enabled.select_view(view);
    (void)enabled.select_active(query);
  }
  require(domain->snapshot().live_bytes == bytes,
          "Overflow projection reuses prepared candidate storage");
}
