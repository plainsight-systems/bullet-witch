// Synthetic source-grid contracts: cell interpolation, fallback overlap,
// rotation and excluded neighbors. Actual executable comparison stays in
// ignored integration evidence. Both phases SL.con.3/MEM.9 validate admission
// and verify stable prepared storage; no proprietary source bytes are embedded.
// Redesign I.1/CDSA.32/MEM.9 checks distinct strict sampling and per-receiver
// invalidation, including equality and live intensity without new allocations.
#include "bw/lighting/irradiance_field.hpp"

#include <cmath>
#include <limits>
#include <numbers>
#include <thread>
#include <type_traits>

#include "../../../fixtures/camera_fixture.hpp"
#include "bw/lighting/local_lights.hpp"
#include "bw/lighting/scene_lighting.hpp"
namespace {
static_assert(!std::is_move_constructible_v<bw::scene::SceneLighting>);
static_assert(
    !std::is_copy_constructible_v<bw::scene::RetainedLightingReceiver>);
using camera_fixture::require;
using camera_fixture::scalar;
using camera_fixture::word;
constexpr std::size_t kTable = 32, kRecord = 48, kBounds = 64, kGrid = 128,
                      kFallback = 400, kDictionary = 520, kExclusions = 550,
                      kExcludedBox = 560;
bw::resources::Bytes field_bytes() {
  bw::resources::Bytes bytes(640);
  word(bytes, 0, 0x544c4853);
  word(bytes, 4, 1);
  word(bytes, 8, 1);
  word(bytes, 12, kTable);
  word(bytes, 16, kFallback);
  word(bytes, 20, kDictionary);
  scalar(bytes, 24, 1);
  word(bytes, kTable, kRecord);
  word(bytes, kRecord, kGrid);
  word(bytes, kRecord + 4, kBounds);
  scalar(bytes, kBounds + 12, 100);
  for (std::size_t axis = 0; axis < 3; ++axis) {
    scalar(bytes, kBounds + 16 + axis * 4, 2);
    scalar(bytes, kGrid + axis * 4, 2);
  }
  word(bytes, kGrid + 48, 1);  // two X samples, constant Y/Z
  scalar(bytes, kDictionary, 2);
  scalar(bytes, kDictionary + 4, 6);
  scalar(bytes, kDictionary + 8, 10);
  for (std::size_t c = 0; c < 27; ++c) {
    bytes[kGrid + 64 + 54 + c * 2] = 1;
    bytes[kFallback + 64 + c * 2] = 2;
  }
  return bytes;
}
void near(float actual, float expected) {
  require(std::abs(actual - expected) < .00001F,
          "Source irradiance expectation");
}
void strict_grid_contract(const std::shared_ptr<bw::memory::Domain>& domain) {
  auto bytes = field_bytes();
  bytes.resize(1400);
  // Sixteen cells with asymmetric nonlinear X/Z values distinguish the
  // five-point kernel, fixed Y layer and source terminal-neighbor exclusion.
  constexpr std::size_t kStrictDictionary = 1100;
  word(bytes, 16, 0);
  word(bytes, 20, kStrictDictionary);
  word(bytes, kGrid + 48, 3);
  word(bytes, kGrid + 56, 3);
  for (std::size_t x = 0; x < 4; ++x) {
    for (std::size_t z = 0; z < 4; ++z) {
      const auto index = x * 4 + z;
      scalar(bytes, kStrictDictionary + index * 4,
             static_cast<float>(x * 10 + z * z));
      for (std::size_t channel = 0; channel < 27; ++channel) {
        const auto offset = kGrid + 64 + index * 54 + channel * 2;
        bytes.at(offset) = static_cast<std::uint8_t>(index);
        bytes.at(offset + 1) = 0;
      }
    }
  }
  // This exclusion removes every actor-grid corner but is not a strict-path
  // input. It cannot erase the placed receiver's five source samples.
  word(bytes, kRecord + 8, 1);
  word(bytes, kRecord + 12, 1300);
  word(bytes, 1300, 1310);
  for (std::size_t axis = 0; axis < 3; ++axis) {
    scalar(bytes, 1310 + 16 + axis * 4, 100);
  }
  bw::scene::IrradianceField field(bytes, domain);
  near(field.sample({0, 0, 0}, 1,
                    bw::scene::IrradianceSampling::first_containing)[0][0],
       97.F / 5.F);
  near(field.sample({0, 0, 0})[0][0], 0);
}
void residual_publication_contract(
    const std::shared_ptr<bw::memory::Domain>& domain) {
  namespace scene = bw::scene;
  auto bytes = field_bytes();
  const std::array<scene::SceneLightFieldSource, 1> fields{{{bytes, 0}}};
  const std::array<scene::LocalLight, 1> emitters{
      {{{0, 0, 3}, {4, 2, 1}, {}, {1, .4F, .5F}}}};
  scene::SceneLighting lighting({fields, emitters, {.05F, .15F, 0, true}, {}},
                                domain);
  const bw::math::Affine3<float> camera{
      {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}, {0, 0, -5}}};
  lighting.set_view({camera[3], bw::math::make_perspective_frustum(
                                    camera, {-1, 1, -1, 1, 1, 100})});
  const scene::LightingReceiver actor{
      .policy = scene::ReceiverLightingPolicy::spatial,
      .model_radius = .4F,
      .field = {0}};
  const auto packet = lighting.evaluate(actor);
  require(packet.local_lights.count == 0 && packet.irradiance[0][3] != 0,
          "Overflow survives zero spatial owner intensity with no direct light "
          "slots");
  require(packet.irradiance ==
              scene::pack_irradiance(packet.local_lights.residual_irradiance),
          "Overflow coefficients are added after owner scaling before packing");
  scene::RetainedLightingReceiver placed(lighting, {0});
  const auto before = placed.evaluate({0, 0, 0}, .4F);
  lighting.replace_local_lights({});
  require(before.irradiance[0][3] != 0 &&
              placed.evaluate({0, 0, 0}, .4F).irradiance ==
                  scene::pack_irradiance({}),
          "Emitter retirement removes overflow even on a retained spatial "
          "cache hit");
}
void local_light_tests(const std::shared_ptr<bw::memory::Domain>& domain) {
  using bw::scene::LocalLight;
  // Equal colors at different distances establish original attenuation rank;
  // the fifth admitted light must not exceed the original four-slot packet.
  std::array<LocalLight, 5> lights{};
  for (std::size_t i = 0; i < lights.size(); ++i) {
    lights.at(i) = {{static_cast<float>(i + 1), 0, 0},
                    {10, 10, 10},
                    {170, 140, 130},
                    {1, 1, 0}};
  }
  bw::scene::LocalLights selected(lights, {.05F, .15F, 4, false}, domain);
  const auto live = domain->snapshot().live_bytes;
  const auto packet = selected.select({{0, 0, 0}, .4F});
  require(packet.count == 4 && packet.lights[0].position[0] == 1 &&
              packet.lights[3].position[0] == 4,
          "Source rank retains the four greatest contributions");
  near(packet.lights[0].specular[0], 170);
  near(packet.contribution_threshold, .05F);
  const auto moved = selected.select({{6, 0, 0}, .4F});
  require(moved.count == 4 && moved.lights[0].position[0] == 5 &&
              moved.lights[3].position[0] == 2,
          "Model movement changes source light ordering");
  require(selected.select({{1000, 0, 0}, 0}).count == 0 &&
              domain->snapshot().live_bytes == live,
          "Empty influence clears selection without query allocation");
  const auto coincident = selected.select({{3, 0, 0}, .4F});
  require(coincident.count == 4 && coincident.lights[0].position[0] == 4 &&
              coincident.lights[1].position[0] == 2 &&
              coincident.lights[2].position[0] == 3,
          "Original RGBA rank preserves unordered coincident zero-bias light");
  // Radius = pow((maxRGB - bias*threshold)/(scale*threshold),1/exponent).
  // For this light it is exactly four, and source sphere admission is strict.
  const LocalLight boundary{{0, 0, 0}, {1, 1, 1}, {}, {1, 1, 1}};
  bw::scene::LocalLights bounded(std::span(&boundary, 1),
                                 {.05F, .15F, 4, false}, domain);
  require(
      bounded.select({{4, 0, 0}, 0}).count == 0 &&
          bounded.select({{4, 0, 0}, .5F}).count == 1,
      "Model group radius participates in strict source influence admission");
  bw::scene::LocalLights empty_overflow({}, {.05F, .15F, 4, true}, domain);
  require(empty_overflow.select({}).residual_irradiance ==
              bw::scene::IrradianceCoefficients{},
          "Empty overflow selection retains zero irradiance");
}
// Exercise the scene publication boundary with independent field identities,
// model observations and transactional updates, not private sampler structure.
void dynamic_light_contract(const std::shared_ptr<bw::memory::Domain>& domain) {
  namespace scene = bw::scene;
  static_assert(!std::is_copy_constructible_v<scene::LocalLightEmitter>);
  static_assert(std::is_nothrow_move_constructible_v<scene::LocalLightEmitter>);
  const std::array<scene::LocalLight, 1> fixed{
      {{{0, 0, 3}, {1, 1, 1}, {2, 2, 2}, {1, 1, 0}}}};
  const scene::LocalLightViewQuery view{
      {},
      bw::math::make_perspective_frustum(
          {{{1, 0, 0}, {0, 1, 0}, {0, 0, 1}, {0, 0, 0}}},
          {-1, 1, -1, 1, 1, 100})};
  const scene::LightingReceiver receiver{
      .policy = scene::ReceiverLightingPolicy::stage_ambient};
  const auto field = field_bytes();
  const std::array<scene::SceneLightFieldSource, 1> fields{{{field, 1}}};
  const scene::LightingReceiver spatial{
      .policy = scene::ReceiverLightingPolicy::spatial, .field = {0}};
  scene::LocalLightEmitter expired;
  {
    scene::SceneLighting owner({fields, fixed, {.05F, .15F, 4, false}, {}},
                               domain);
    owner.prepare_emitters(1);
    auto emitter = owner.acquire_emitter();
    const auto allocated = domain->snapshot().live_bytes;
    bool full = false;
    try {
      (void)owner.acquire_emitter();
    } catch (const std::length_error&) {
      full = true;
    }
    require(full, "Scene emitter capacity must fail explicitly");
    auto dynamic = fixed[0];
    dynamic.diffuse = {3, 3, 3};
    owner.set_view(view);
    emitter.update(dynamic);
    scene::RetainedLightingReceiver delayed(owner, {0});
    require(
        owner.evaluate(receiver).local_lights.count == 1 &&
            owner.evaluate(spatial).local_lights.count == 1 &&
            delayed.evaluate({}, 0).local_lights.count == 1,
        "Emitter update preserves the completed view for delayed admission");
    auto invalid_view = view;
    invalid_view.frustum.depth_range = 0;
    bool rejected_view = false;
    try {
      owner.set_view(invalid_view);
    } catch (const std::invalid_argument&) {
      rejected_view = true;
    }
    require(rejected_view && owner.evaluate(spatial).local_lights.count == 1,
            "Rejected view cannot partially publish pending emitters");
    owner.set_view(view);
    require(owner.evaluate(receiver).local_lights.count == 2,
            "Dynamic publication preserves static emitter");
    dynamic.diffuse = {4, 4, 4};
    emitter.update(dynamic);
    const auto old_color =
        owner.evaluate(spatial).local_lights.lights[0].diffuse;
    auto invalid = dynamic;
    invalid.position[0] = std::numeric_limits<float>::infinity();
    bool rejected = false;
    try {
      emitter.update(invalid);
    } catch (const std::invalid_argument&) {
      rejected = true;
    }
    require(rejected && owner.evaluate(receiver).local_lights.count == 2,
            "Rejected dynamic publication preserves prepared view");
    owner.set_view(view);
    require(owner.evaluate(spatial).local_lights.lights[0].diffuse != old_color,
            "Invalid update preserves the preceding accepted pending value");
    bool wrong_thread = false;
    {
      std::jthread worker([&] {
        try {
          emitter.update(dynamic);
        } catch (const std::logic_error&) {
          wrong_thread = true;
        }
      });
    }
    require(wrong_thread,
            "Emitter mutation must retain scene thread ownership");
    auto moved = std::move(emitter);
    require(moved.valid(), "Move transfers sole emitter capability");
    moved.clear();
    require(owner.evaluate(spatial).local_lights.count == 2 &&
                delayed.evaluate({}, 0).local_lights.count == 2,
            "Clear stages retirement without revoking the published view");
    owner.set_view(view);
    require(owner.evaluate(receiver).local_lights.count == 1,
            "Clearing dynamic emitter cannot clear static lights");
    moved = {};
    owner.set_view(view);
    auto reused = owner.acquire_emitter();
    reused.update(dynamic);
    owner.set_view(view);
    require(owner.evaluate(receiver).local_lights.count == 2 &&
                domain->snapshot().live_bytes == allocated,
            "Slot reuse is generation checked and allocation free");
    const auto before_reuse =
        owner.evaluate(spatial).local_lights.lights[0].diffuse;
    reused = {};
    require(owner.evaluate(spatial).local_lights.count == 2,
            "Release retains the completed snapshot until publication");
    reused = owner.acquire_emitter();
    dynamic.diffuse = {6, 6, 6};
    reused.update(dynamic);
    require(
        owner.evaluate(spatial).local_lights.lights[0].diffuse == before_reuse,
        "Reused generation cannot overwrite the prior snapshot");
    owner.set_view(view);
    require(owner.evaluate(spatial).local_lights.count == 2 &&
                owner.evaluate(spatial).local_lights.lights[0].diffuse !=
                    before_reuse,
            "Next publication installs only the replacement generation");
    reused = {};
    owner.set_view(view);
    require(owner.evaluate(spatial).local_lights.count == 1,
            "Release before publication leaves no ghost emitter");
    reused = owner.acquire_emitter();
    reused.update(dynamic);
    owner.set_view(view);
    owner.clear_static_emitters();
    require(owner.evaluate(receiver).local_lights.count == 1 &&
                reused.valid() && domain->snapshot().live_bytes == allocated,
            "Static retirement preserves dynamic identity and fixed storage");
    reused.update(dynamic);
    owner.set_view(view);
    require(owner.evaluate(receiver).local_lights.count == 1,
            "Dynamic updates remain valid after static retirement");
    expired = std::move(reused);
  }
  require(!expired.valid(),
          "Scene retirement expires borrowed light capability");
  expired.clear();
  expired = {};
}
void scene_lighting_tests(const std::shared_ptr<bw::memory::Domain>& domain) {
  auto character = field_bytes();
  auto enemy = field_bytes();
  scalar(enemy, kDictionary, 8);
  scalar(enemy, kDictionary + 4, 12);
  const std::array<bw::scene::LocalLight, 1> lights{
      {{{3, 0, 0}, {1, 1, 1}, {2, 3, 4}, {1, 1, 0}}}};
  const bw::scene::LocalLightSettings settings{.2F, .2F, 4, false};
  const bw::scene::SceneLightingParameters parameters{{.2F, .3F, .4F}};
  const std::array<bw::scene::SceneLightFieldSource, 2> fields{
      {{character, .25F}, {enemy, 2}}};
  bw::scene::ReceiverLighting retained;
  const auto initial_bytes = domain->snapshot().live_bytes;
  {
    bw::scene::SceneLighting scene({fields, lights, settings, parameters},
                                   domain);
    auto updated_parameters = parameters;
    updated_parameters.directional = {
        {0, 0, .999F}, {.2F, -.3F, .4F, 2}, {.5F, .6F, .7F, 3}};
    scene.set_parameters(updated_parameters);
    const auto directional = scene.directional_lighting();
    require(directional.direction == bw::math::Vector<float, 4>{0, 0, -1, 0} &&
                directional.diffuse == updated_parameters.directional.diffuse &&
                directional.specular == updated_parameters.directional.specular,
            "Shared directional packet preserves authored color and direction");
    auto invalid_parameters = updated_parameters;
    invalid_parameters.ambient = {7, 8, 9};
    invalid_parameters.directional.vector = {0, 0, -.001F};
    bool rejected_direction = false;
    try {
      scene.set_parameters(invalid_parameters);
    } catch (const std::invalid_argument&) {
      rejected_direction = true;
    }
    require(
        rejected_direction &&
            scene.directional_lighting().direction == directional.direction &&
            scene.directional_lighting().diffuse == directional.diffuse,
        "Invalid directional publication retains prior shared state");
    const bw::scene::LightingReceiver static_receiver{
        bw::scene::ReceiverLightingPolicy::stage_ambient};
    bool missing_view = false;
    try {
      (void)scene.evaluate(static_receiver);
    } catch (const std::logic_error&) {
      missing_view = true;
    }
    require(missing_view, "Static direct lighting requires a published view");
    missing_view = false;
    try {
      (void)scene.evaluate(
          {.policy = bw::scene::ReceiverLightingPolicy::spatial, .field = {0}});
    } catch (const std::logic_error&) {
      missing_view = true;
    }
    require(missing_view, "Spatial direct lighting requires view admission");
    const bw::math::Affine3<float> camera_world{
        {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}, {0, 0, -5}}};
    const bw::scene::LocalLightViewQuery view{
        {0, 0, -5},
        bw::math::make_perspective_frustum(camera_world,
                                           {-1, 1, -1, 1, 1, 100})};
    scene.set_view(view);
    bw::scene::RetainedLightingReceiver placed(scene, {0});
    bw::scene::IrradianceCoefficients placed_expected;
    placed_expected.fill({.5F, .5F, .5F});
    const auto initial_placed = placed.evaluate({1.75F, 0, 0}, .4F);
    require(initial_placed.irradiance ==
                bw::scene::pack_irradiance(placed_expected),
            "Placed receiver initializes independent strict-field state");
    require(
        placed.evaluate({2, 0, 0}, .4F).irradiance == initial_placed.irradiance,
        "Sub-threshold root movement retains source SH");
    placed_expected.fill({2.5F, 2.5F, 2.5F});
    require(placed.evaluate({2.25F, 0, 0}, .4F).irradiance ==
                bw::scene::pack_irradiance(placed_expected),
            "Exact source squared-distance threshold refreshes SH");
    scene.set_field_intensity({0}, .3125F);
    placed_expected.fill({3.125F, 3.125F, 3.125F});
    require(placed.evaluate({2.25F, 0, 0}, .4F).irradiance ==
                bw::scene::pack_irradiance(placed_expected),
            "Live intensity participates in the four-lane cache key");
    scene.set_field_intensity({0}, .25F);
    const auto static_light = scene.evaluate(static_receiver);
    near(static_light.irradiance[0][3], .2F);
    near(static_light.irradiance[2][3], .4F);
    const auto sky =
        scene.evaluate({bw::scene::ReceiverLightingPolicy::sky_white});
    near(sky.irradiance[0][3], 1);
    require(static_light.local_lights.count == 1 && sky.local_lights.count == 0,
            "Static view lights and sky white have distinct source policies");
    // Source bytes and emitter arrays are preparation inputs, not live aliases.
    scalar(character, kDictionary, 100);
    scalar(enemy, kDictionary, 100);
    bw::scene::LightingReceiver receiver{
        bw::scene::ReceiverLightingPolicy::spatial,
        {1, 0, 0},
        .4F,
        {{{2, 0, 0}, {0, 1, 0}, {0, 0, 1}, {-2, 0, 0}}},
        {0}};
    const auto body = scene.evaluate(receiver);
    bw::scene::IrradianceCoefficients expected;
    expected.fill({1, 1, 1});
    require(body.irradiance == bw::scene::pack_irradiance(expected) &&
                body.local_lights.count == 0,
            "Model world center and unscaled radius select source lighting");
    receiver.model_world[3][0] = -1;
    const auto weapon = scene.evaluate(receiver);
    expected.fill({1.25F, 1.25F, 1.25F});
    require(weapon.irradiance == bw::scene::pack_irradiance(expected) &&
                weapon.local_lights.count == 1 && body.local_lights.count == 0,
            "Independent receiver snapshots do not overwrite prior results");
    receiver.field = {1};
    expected.fill({22, 22, 22});
    require(scene.evaluate(receiver).irradiance ==
                bw::scene::pack_irradiance(expected),
            "Registered fields and intensities remain independent");
    const auto live = domain->snapshot().live_bytes;
    for (int index = 0; index < 20; ++index) {
      retained = scene.evaluate(receiver);
    }
    require(domain->snapshot().live_bytes == live,
            "Scene queries retain fixed prepared storage");

    bool rejected = false;
    try {
      scene.set_field_intensity({1}, std::numeric_limits<float>::infinity());
    } catch (const std::invalid_argument&) {
      rejected = true;
    }
    require(
        rejected && scene.evaluate(receiver).irradiance == retained.irradiance,
        "Invalid parameter publication retains complete prior state");
    auto invalid_view = view;
    invalid_view.eye[0] = std::numeric_limits<float>::quiet_NaN();
    rejected = false;
    try {
      scene.set_view(invalid_view);
    } catch (const std::invalid_argument&) {
      rejected = true;
    }
    require(rejected && scene.evaluate(static_receiver).local_lights.count == 1,
            "Invalid view publication preserves the previous static packet");
    auto bad_lights = lights;
    bad_lights[0].attenuation = {};
    rejected = false;
    try {
      scene.replace_local_lights(bad_lights);
    } catch (const std::invalid_argument&) {
      rejected = true;
    }
    require(rejected && scene.evaluate(receiver).local_lights.count == 1,
            "Invalid emitter replacement preserves active lights");
    scene.replace_local_lights({});
    require(scene.evaluate(receiver).local_lights.count == 0 &&
                scene.evaluate(static_receiver).local_lights.count == 0 &&
                retained.local_lights.count == 1 &&
                static_light.local_lights.count == 1,
            "Emitter retirement clears future queries, not owned old packets");
    scene.set_field_intensity({1}, -1);
    expected.fill({-11, -11, -11});
    require(scene.evaluate(receiver).irradiance ==
                bw::scene::pack_irradiance(expected),
            "Signed source intensity is preserved without art normalization");
  }
  require(
      domain->snapshot().live_bytes == initial_bytes &&
          retained.local_lights.count == 1,
      "Scene retirement releases storage while returned packets remain valid");
  bw::scene::SceneLighting empty({{}, {}, settings, parameters}, domain);
  const bw::math::Affine3<float> empty_camera{
      {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}, {0, 0, 0}}};
  empty.set_view({{},
                  bw::math::make_perspective_frustum(empty_camera,
                                                     {-1, 1, -1, 1, 1, 100})});
  bool rejected = false;
  try {
    (void)empty.evaluate({bw::scene::ReceiverLightingPolicy::spatial});
  } catch (const std::logic_error&) {
    rejected = true;
  }
  require(rejected, "Absent selected field does not become stage ambient");
  rejected = false;
  try {
    (void)empty.evaluate({});
  } catch (const std::invalid_argument&) {
    rejected = true;
  }
  require(rejected,
          "Unclassified receiver cannot silently use a static policy");
}
}  // namespace
void irradiance_field_tests() {
  auto domain =
      bw::memory::Domain::create(std::make_shared<camera_fixture::Backing>());
  strict_grid_contract(domain);
  residual_publication_contract(domain);
  local_light_tests(domain);
  scene_lighting_tests(domain);
  dynamic_light_contract(domain);
  auto bytes = field_bytes();
  bw::scene::IrradianceField field(bytes, domain);
  require(field.zone_count() == 1 && field.sample_count() == 2,
          "Prepared source dimensions");
  near(field.sample({0, 0, 0})[0][0], 4);
  near(field.sample({0, 0, 0}, .25F)[0][0], 1);
  near(field.sample({0, 0, 0}, 0)[0][0], 0);
  near(field.sample({0, 0, 0}, -1)[0][0], -4);
  near(field.sample({1, 0, 0})[0][0], 5);
  near(field.sample({2, 0, 0})[0][0],
       8);  // cell value6, half-zone overlap with default10
  near(field.sample({3, 0, 0})[0][0], 10);  // strict expanded-volume edge
  using bw::scene::IrradianceSampling;
  near(field.sample({1.75F, 0, 0}, 1,
                    IrradianceSampling::first_containing)[0][0],
       2);
  near(field.sample({2, 0, 0}, 1, IrradianceSampling::first_containing)[0][0],
       10);
  near(field.sample({-2, 0, 0}, 1, IrradianceSampling::first_containing)[0][0],
       10);
  const auto live = domain->snapshot().live_bytes;
  for (int i = 0; i < 100; ++i) {
    (void)field.sample({1, 0, 0});
  }
  require(domain->snapshot().live_bytes == live,
          "Sampling retains prepared allocation");
  scalar(bytes, kGrid + 16 + 8, std::numbers::pi_v<float> / 2);
  bw::scene::IrradianceField rotated(bytes, domain);
  near(rotated.sample({0, 1, 0})[0][0], 5);
  // Exclude the first local grid sample: surviving positive corner
  // renormalizes.
  bytes = field_bytes();
  word(bytes, kRecord + 8, 1);
  word(bytes, kRecord + 12, kExclusions);
  word(bytes, kExclusions, kExcludedBox);
  scalar(bytes, kExcludedBox + 12, 1);
  for (std::size_t axis = 0; axis < 3; ++axis) {
    scalar(bytes, kExcludedBox + 16 + axis * 4, .5F);
  }
  bw::scene::IrradianceField excluded(bytes, domain);
  near(excluded.sample({1, 0, 0})[0][0], 6);
  bw::scene::IrradianceCoefficients coefficients{};
  coefficients[0] = {1, 2, 3};
  coefficients[8] = {4, 5, 6};
  auto packed = bw::scene::pack_irradiance(coefficients);
  near(packed[0][3], .2820947766F);
  near(packed[2][3], .8462843299F);
  near(packed[6][0], .5462741852F);
  near(packed[6][3], 1);
  bool rejected = false;
  try {
    (void)field.sample({std::numeric_limits<float>::quiet_NaN(), 0, 0});
  } catch (const std::invalid_argument&) {
    rejected = true;
  }
  require(rejected, "Nonfinite query rejected");
  word(bytes, kGrid + 48, std::numeric_limits<std::uint32_t>::max());
  rejected = false;
  try {
    bw::scene::IrradianceField invalid(bytes, domain);
  } catch (const bw::resources::Error&) {
    rejected = true;
  }
  require(rejected, "Malformed grid rejected before allocation");
}
