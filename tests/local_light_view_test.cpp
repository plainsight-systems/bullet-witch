// Public view-light admission contracts, including independent original x86
// frustum results and the global-versus-model candidate distinction. Synthetic
// data only. Both-phase I.1 (#ri-explicit) exercises explicit query semantics;
// MEM.9 verifies prepared storage survives repeated selection without growth.
#include <limits>

#include "bw/scene/lighting/local_lights.hpp"
#include "camera_fixture.hpp"

void local_light_view_tests() {
  namespace math = bw::math;
  namespace scene = bw::scene;
  using camera_fixture::require;
  const math::Affine3<float> identity{
      {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}, {0, 0, 0}}};
  const auto frustum =
      math::make_perspective_frustum(identity, {-1, 1, -1, 1, 1, 10});
  require(math::intersects(frustum, {0, 0, .5F}, 0),
          "Source cull intentionally omits near-plane rejection");
  require(math::intersects(frustum, {0, 0, 11}, 1) &&
              !math::intersects(frustum, {0, 0, 11.01F}, 1),
          "Far-plane sphere contact is admitted without an epsilon expansion");
  require(!math::intersects(frustum, {4, 0, 1}, 1) &&
              math::intersects(frustum, {4, 0, 1}, 3),
          "Side planes admit intersecting spheres, not only their centers");
  const math::Affine3<float> rotated{
      {{0, 0, -1}, {0, 1, 0}, {1, 0, 0}, {4, 2, -3}}};
  const auto moved = math::make_perspective_frustum(
      rotated, {-.5F, 1.5F, -.75F, 1.25F, .5F, 20});
  // Independent 40a860 execution; different asymmetric frustum and world pose.
  const std::array<math::Vector<float, 4>, 5> source{
      {{-.707106769F, 0, .707106769F, 4.949747086F},
       {-.948683262F, 0, -.316227764F, 2.846049786F},
       {-.832050323F, -.554700196F, 0, 4.437601566F},
       {-.928476691F, .371390671F, 0, 2.971125364F},
       {1, 0, 0, -24}}};
  for (std::size_t plane = 0; plane < source.size(); ++plane) {
    for (std::size_t lane = 0; lane < 4; ++lane) {
      require(std::abs(moved.planes.at(plane).at(lane) -
                       source.at(plane).at(lane)) < 1e-6F,
              "Frustum planes match independent original instructions");
    }
  }
  auto domain =
      bw::memory::Domain::create(std::make_shared<camera_fixture::Backing>());
  const auto initial = domain->snapshot().live_bytes;
  {
    const std::array<scene::LocalLight, 6> lights{
        {{{0, 0, 5}, {1, 1, 1}, {}, {2, 1, 0}},
         {{0, 0, 6}, {1, 1, 1}, {}, {2, 1, 0}},
         {{6, 0, 1}, {1, 1, 1}, {}, {2, 1, 0}},
         {{0, 0, 7}, {1, 1, 1}, {}, {2, 1, 0}},
         {{0, 0, 8}, {1, 1, 1}, {}, {2, 1, 0}},
         {{0, 0, 30}, {1, 1, 1}, {}, {2, 1, 0}}}};
    scene::LocalLights prepared(lights, {.05F, .15F, 1, false}, domain);
    const auto bytes = domain->snapshot().live_bytes;
    bool absent_view_rejected = false;
    try {
      (void)prepared.select_active({{6, 0, 1}, .01F});
    } catch (const std::logic_error&) {
      absent_view_rejected = true;
    }
    require(absent_view_rejected,
            "Actor lighting requires source view publication");
    const auto selected = prepared.select_view({{0, 0, 0}, frustum});
    require(selected.count == 4 &&
                selected.lights[0].position == lights[0].position &&
                selected.lights[2].position == lights[2].position,
            "Global list uses frustum contribution-radius admission and fixed "
            "capacity four");
    require(prepared.select({{0, 0, 0}, .01F}).count == 0,
            "View candidates do not incorrectly use actor distance admission");
    require(prepared.select({{6, 0, 1}, .01F}).count == 1 &&
                prepared.select_active({{6, 0, 1}, .01F}).count == 0,
            "Tighter view cull removes actor candidates retained globally");
    require(prepared.select_active({{0, 0, 5}, .01F}).count == 1,
            "Visible actor candidate survives both source admissions");
    auto distant = rotated;
    distant[3] = {200, 2, -3};
    const scene::LocalLightViewQuery empty{
        distant[3], math::make_perspective_frustum(
                        distant, {-.01F, .01F, -.01F, .01F, 1, 2})};
    require(prepared.select_view(empty).count == 0,
            "New view with no candidates clears prior global packet");
    for (int i = 0; i < 20; ++i) {
      (void)prepared.select_view({{0, 0, 0}, frustum});
    }
    require(domain->snapshot().live_bytes == bytes,
            "Global view selection retains prepared scratch");
    bool rejected = false;
    try {
      (void)prepared.select_view({{0, 0, 0}, {}});
    } catch (const std::invalid_argument&) {
      rejected = true;
    }
    require(rejected, "Unprepared frustum cannot silently admit every light");
  }
  {
    const std::array<scene::LocalLight, 2> tied{
        {{{0, 0, 5}, {1, 1, 1}, {}, {2, 1, 1}},
         {{3, 0, 4}, {1, 1, 1}, {}, {2, 1, 1}}}};
    scene::LocalLights prepared(tied, {.05F, .15F, 4, false}, domain);
    const scene::LocalLightViewQuery view{{0, 0, 0}, frustum};
    require(prepared.select_view(view).lights[0].position == tied[0].position,
            "New emitter depth starts at zero, retaining registration ties");
    require(
        prepared.select_view(view).lights[0].position == tied[1].position &&
            prepared.select_active({{0, 0, 0}, 4}).lights[0].position ==
                tied[1].position,
        "Equal luminance preserves prior-view depth ordering in both lists");
    bool rejected = false;
    try {
      (void)prepared.select_view({{0, 0, 0}, {}});
    } catch (const std::invalid_argument&) {
      rejected = true;
    }
    require(rejected && prepared.select_active({{0, 0, 0}, 4}).count == 2,
            "Rejected view preserves prior active-list publication");
  }
  require(domain->snapshot().live_bytes == initial,
          "Local light owner releases prepared view storage");
  bool rejected = false;
  try {
    (void)math::make_perspective_frustum(identity, {-1, 1, -1, 1, 0, 10});
  } catch (const std::invalid_argument&) {
    rejected = true;
  }
  require(rejected, "Degenerate source frustum is rejected before publication");
}
