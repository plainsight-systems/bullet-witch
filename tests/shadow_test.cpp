// Check reconstructed shadow matrices against original initialized machine-code
// witnesses, including camera translation/rotation and the near-parallel
// branch. Guard tests verify homogeneous inset without changing depth/W. Jitter
// retains independent byte expectations. Both phases: I.1 (#ri-explicit) tests
// numeric contracts; MEM.9
// (guidelines/memory/MEM.9-embedded-allocate-at-init.md) uses fixed fixtures.
// Evidence: shadow-projection-baseline.md and ignored pass66.
#include "bw/render/shadow.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>
#include <numbers>
#include <stdexcept>

namespace {
void require(bool value, const char* message) {
  if (!value) throw std::runtime_error(message);
}
constexpr float kRadians = std::numbers::pi_v<float> / 180;
constexpr bw::render::PerspectiveView kView{44 * kRadians, 16.0F / 9, .01F,
                                            1000};
constexpr bw::render::ShadowWarpSettings kSettings{
    1000, -1, 100, .85F, 100, 60 * kRadians, 2, .058F / 1024};
struct Witness {
  float yaw{}, pitch{}, shift{};
  std::array<float, 16> matrix{};
  std::array<float, 3> eye{-61.3953F, 1.35F, -100.568F};
};
// Original matrices are row-vector memory; fixtures below transpose them into
// the portable dot-product-row convention. No reference binaries are shipped.
constexpr std::array<Witness, 6> kWitnesses{{
    {-0.28F,
     0.F,
     0.F,
     {
         1.392137289F,
         -0.008651229553F,
         0.01024003141F,
         86.51559448F,
         -0.004989427514F,
         -0.0101106232F,
         1.008161783F,
         100.0861053F,
         -0.003280474572F,
         -0.003359485883F,
         0.6670159698F,
         67.54188538F,
         -0.004948724993F,
         -0.01002814341F,
         0.9999375343F,
         101.2614746F,
     }},
    {-0.28F,
     0.F,
     0.1F,
     {
         1.392137408F,
         -0.008637248538F,
         0.01023434661F,
         86.37578583F,
         -0.004989326F,
         -0.0101106232F,
         1.008161783F,
         100.0861282F,
         -0.003280441277F,
         -0.003359486349F,
         0.6670160294F,
         67.54190063F,
         -0.00494862441F,
         -0.01002814341F,
         0.9999375343F,
         101.2614975F,
     }},
    {4.72F,
     0.F,
     0.F,
     {
         1.387403131F,
         -0.007396487519F,
         -0.1116064191F,
         73.96889496F,
         0.08290273696F,
         -0.01061605569F,
         1.004756331F,
         105.1404648F,
         0.0548716709F,
         -0.00369493477F,
         0.6648139358F,
         70.89109802F,
         0.08222629875F,
         -0.01052943431F,
         0.9965580702F,
         106.2744217F,
     }},
    {-0.28F,
     80.F,
     0.F,
     {
         -101.5254745F,
         0.180980593F,
         61.97615814F,
         -1810.423828F,
         -70.53586578F,
         1.337618589F,
         -115.5521088F,
         -13375.0332F,
         0.005899451207F,
         0.9607665539F,
         0.009664501995F,
         9607.712891F,
         -0.006139931735F,
         -0.9999304414F,
         -0.01005845796F,
         9999.294922F,
     }},
    {-0.28F,
     0,
     0,
     {
         1.400926232F,
         -0.001078925561F,
         0.007283366285F,
         10.78961563F,
         -0.004922504071F,
         0.005528562702F,
         1.008146286F,
         -56.30575943F,
         -0.003251607763F,
         0.006989843212F,
         0.6664680243F,
         -35.89851379F,
         -0.004882596433F,
         0.005483741872F,
         0.9999730587F,
         -53.85738754F,
     },
     {-7.98534F, 1.35F, 54.8025F}},
    {-61.25F,
     0,
     0,
     {
         0.676340282F,
         0.01389066782F,
         1.221232653F,
         -138.924408F,
         -0.8838573098F,
         -0.00330330641F,
         0.4849381447F,
         32.01813507F,
         -0.5844356418F,
         0.001149683259F,
         0.3206193447F,
         22.4986248F,
         -0.8767063022F,
         -0.003276580479F,
         0.4810146689F,
         33.75099564F,
     },
     {76.5207F, 0.8F, 71.3654F}},
}};
bw::render::ViewPose pose(const Witness& witness) {
  const float yaw = witness.yaw * kRadians, pitch = witness.pitch * kRadians;
  bw::render::ViewPose result{
      witness.eye,
      {std::cos(yaw), 0, -std::sin(yaw)},
      {-std::sin(yaw) * std::sin(pitch), std::cos(pitch),
       -std::cos(yaw) * std::sin(pitch)},
      {std::sin(yaw) * std::cos(pitch), std::sin(pitch),
       std::cos(yaw) * std::cos(pitch)}};
  for (std::size_t axis = 0; axis < 3; ++axis) {
    result.eye.at(axis) += witness.shift * result.right.at(axis);
  }
  return result;
}
void reject(const bw::render::ViewPose& camera,
            const bw::render::ShadowWarpSettings& settings,
            const std::array<float, 3>& direction = {0, -1000, -.001F}) {
  bool rejected = false;
  try {
    bw::render::make_directional_shadow_projection(camera, kView, settings,
                                                   direction);
  } catch (const std::invalid_argument&) {
    rejected = true;
  }
  require(rejected, "Invalid shadow projection must reject");
}
// Captured original CPU constant uploads, not expected values recomputed by
// this implementation. Hex floats preserve every published float32 bit.
struct ReceiverWitness {
  std::array<float, 3> eye{};
  bw::render::ShadowReceiverProjection expected{};
};
constexpr bw::render::ShadowProjection kReceiverProjection{
    {{{{0x1.c0ebee0000000p-7F, 0x1.1b71760000000p-6F, 0x1.9652be0000000p-9F,
        0x1.6fdf3c0000000p+0F}},
      {{-0x1.42c3ca0000000p-6F, 0x1.5254600000000p-5F, 0x1.2752540000000p-4F,
        -0x1.9bc6a80000000p+1F}},
      {{0x1.de69ae0000000p-8F, -0x1.ad42c40000000p-7F, 0x1.25460a0000000p-6F,
        0x1.f1a9fc0000000p-3F}},
      {{-0x1.fd8ada0000000p-6F, 0x1.b7e9100000000p-5F, 0x1.75f6fe0000000p-4F,
        0x1.7e24de0000000p+2F}}}}};
constexpr std::array<ReceiverWitness, 3> kReceiverWitnesses{{
    {{0x0.0p+0F, 0x0.0p+0F, 0x0.0p+0F},
     {{{{{0x1.c0ebee0000000p-7F, 0x1.1b71760000000p-6F, 0x1.9652be0000000p-9F,
          0x1.6fdf3c0000000p+0F}},
        {{-0x1.42c3ca0000000p-6F, 0x1.5254600000000p-5F, 0x1.2752540000000p-4F,
          -0x1.9bc6a80000000p+1F}},
        {{0x1.de69ae0000000p-8F, -0x1.ad42c40000000p-7F, 0x1.25460a0000000p-6F,
          0x1.f1a9fc0000000p-3F}},
        {{-0x1.fd8ada0000000p-6F, 0x1.b7e9100000000p-5F, 0x1.75f6fe0000000p-4F,
          0x1.7e24de0000000p+2F}}}},
      {{{{-0x1.1d14e40000000p-7F, 0x1.22d0e60000000p-5F, 0x1.82a9940000000p-5F,
          0x1.da1cac0000000p+1F}},
        {{-0x1.758e200000000p-8F, 0x1.9652c00000000p-8F, 0x1.3a92a80000000p-7F,
          0x1.2604180000000p+2F}},
        {{0x1.de69ae0000000p-8F, -0x1.ad42c40000000p-7F, 0x1.25460a0000000p-6F,
          0x1.f1a9fc0000000p-3F}},
        {{-0x1.fd8ada0000000p-6F, 0x1.b7e9100000000p-5F, 0x1.75f6fe0000000p-4F,
          0x1.7e24de0000000p+2F}}}}}},
    {{0x1.ec80000000000p+6F, -0x1.8000000000000p-1F, -0x1.6e80000000000p+6F},
     {{{{{0x1.c0ebee0000000p-7F, 0x1.1b71760000000p-6F, 0x1.9652be0000000p-9F,
          0x1.69d4960000000p+1F}},
        {{-0x1.42c3ca0000000p-6F, 0x1.5254600000000p-5F, 0x1.2752540000000p-4F,
          -0x1.88f34e0000000p+3F}},
        {{0x1.de69ae0000000p-8F, -0x1.ad42c40000000p-7F, 0x1.25460a0000000p-6F,
          -0x1.f42c3a0000000p-2F}},
        {{-0x1.fd8ada0000000p-6F, 0x1.b7e9100000000p-5F, 0x1.75f6fe0000000p-4F,
          -0x1.90e2820000000p+2F}}}},
      {{{{-0x1.1d14e40000000p-7F, 0x1.22d0e60000000p-5F, 0x1.82a9940000000p-5F,
          -0x1.b7f06e0000000p+0F}},
        {{-0x1.758e200000000p-8F, 0x1.9652c00000000p-8F, 0x1.3a92a80000000p-7F,
          0x1.81041a0000000p+1F}},
        {{0x1.de69ae0000000p-8F, -0x1.ad42c40000000p-7F, 0x1.25460a0000000p-6F,
          -0x1.f42c3a0000000p-2F}},
        {{-0x1.fd8ada0000000p-6F, 0x1.b7e9100000000p-5F, 0x1.75f6fe0000000p-4F,
          -0x1.90e2820000000p+2F}}}}}},
    {{-0x1.eb29930be0dedp+5F, 0x1.599999999999ap+0F, -0x1.9245a1cac0831p+6F},
     {{{{{0x1.c0ebee0000000p-7F, 0x1.1b71760000000p-6F, 0x1.9652be0000000p-9F,
          0x1.3adba80000000p-2F}},
        {{-0x1.42c3ca0000000p-6F, 0x1.5254600000000p-5F, 0x1.2752540000000p-4F,
          -0x1.267c9a0000000p+3F}},
        {{0x1.de69ae0000000p-8F, -0x1.ad42c40000000p-7F, 0x1.25460a0000000p-6F,
          -0x1.02f2e80000000p+1F}},
        {{-0x1.fd8ada0000000p-6F, 0x1.b7e9100000000p-5F, 0x1.75f6fe0000000p-4F,
          -0x1.3a9dc80000000p+0F}}}},
      {{{{-0x1.1d14e40000000p-7F, 0x1.22d0e60000000p-5F, 0x1.82a9940000000p-5F,
          -0x1.d7cdbc0000000p-2F}},
        {{-0x1.758e200000000p-8F, 0x1.9652c00000000p-8F, 0x1.3a92a80000000p-7F,
          0x1.fe51c20000000p+1F}},
        {{0x1.de69ae0000000p-8F, -0x1.ad42c40000000p-7F, 0x1.25460a0000000p-6F,
          -0x1.02f2e80000000p+1F}},
        {{-0x1.fd8ada0000000p-6F, 0x1.b7e9100000000p-5F, 0x1.75f6fe0000000p-4F,
          -0x1.3a9dc80000000p+0F}}}}}},
}};
bool same_receiver_bits(const bw::render::ShadowReceiverProjection& a,
                        const bw::render::ShadowReceiverProjection& b) {
  for (std::size_t row = 0; row < 4; ++row) {
    for (std::size_t column = 0; column < 4; ++column) {
      if (std::bit_cast<std::uint32_t>(a.screen_rows.at(row).at(column)) !=
              std::bit_cast<std::uint32_t>(b.screen_rows.at(row).at(column)) ||
          std::bit_cast<std::uint32_t>(a.texture_rows.at(row).at(column)) !=
              std::bit_cast<std::uint32_t>(b.texture_rows.at(row).at(column))) {
        return false;
      }
    }
  }
  return true;
}
void receiver_tests() {
  using bw::render::make_shadow_receiver_projection;
  for (const auto& witness : kReceiverWitnesses) {
    require(same_receiver_bits(make_shadow_receiver_projection(
                                   kReceiverProjection, witness.eye),
                               witness.expected),
            "Receiver constants must match original CPU float bits");
  }
  auto retained = kReceiverWitnesses[1].expected;
  const auto reject_receiver =
      [&](const bw::render::ShadowProjection& projection,
          const std::array<float, 3>& eye) {
        bool rejected = false;
        try {
          retained = make_shadow_receiver_projection(projection, eye);
        } catch (const std::invalid_argument&) {
          rejected = true;
        }
        require(rejected, "Nonfinite receiver input/output must reject");
        require(same_receiver_bits(retained, kReceiverWitnesses[1].expected),
                "Failed receiver publication must retain previous constants");
      };
  constexpr std::array invalid_values{std::numeric_limits<float>::quiet_NaN(),
                                      std::numeric_limits<float>::infinity(),
                                      -std::numeric_limits<float>::infinity()};
  for (float invalid : invalid_values) {
    for (std::size_t row = 0; row < 4; ++row) {
      for (std::size_t column = 0; column < 4; ++column) {
        auto projection = kReceiverProjection;
        projection.clip_rows.at(row).at(column) = invalid;
        reject_receiver(projection, {});
      }
    }
    for (std::size_t axis = 0; axis < 3; ++axis) {
      std::array<float, 3> eye{};
      eye.at(axis) = invalid;
      reject_receiver(kReceiverProjection, eye);
    }
  }
  auto overflow = kReceiverProjection;
  overflow.clip_rows[0][0] = std::numeric_limits<float>::max();
  reject_receiver(overflow, {2, 0, 0});
  overflow.clip_rows[0][3] = std::numeric_limits<float>::max();
  reject_receiver(overflow, {1, 0, 0});
  // A zero/singular projection is valid here: fitting and basis validation
  // belong to the caller, not this value-only publication helper.
  const auto zero = make_shadow_receiver_projection({}, {});
  require(same_receiver_bits(zero, {}),
          "Receiver publication must not invent a projection fit requirement");
}
}  // namespace

void shadow_tests() {
  receiver_tests();
  using bw::render::make_directional_shadow_projection;
  constexpr float kInterior = 1022.0F / 1024;
  constexpr float kRelativeTolerance = .000002F;
  for (const auto& witness : kWitnesses) {
    const auto result = make_directional_shadow_projection(
        pose(witness), kView, kSettings, {0, -1000, -.001F});
    for (std::size_t row = 0; row < 4; ++row) {
      for (std::size_t column = 0; column < 4; ++column) {
        const float expected =
            witness.matrix.at(row * 4 + column) * (row < 2 ? kInterior : 1);
        require(std::abs(result.clip_rows.at(row).at(column) - expected) <=
                    kRelativeTolerance * std::max(1.0F, std::abs(expected)),
                "Source shadow warp matrix disagrees with initialized witness");
      }
    }
  }
  const auto camera = pose(kWitnesses[0]);
  const auto normal = make_directional_shadow_projection(
      camera, kView, kSettings, {0, -1000, -.001F});
  auto authored_branch = kSettings;
  authored_branch.alignment_threshold = 30.98F;
  const auto unrestricted = make_directional_shadow_projection(
      camera, kView, authored_branch, {0, -1000, -.001F});
  require(
      unrestricted.clip_rows == normal.clip_rows,
      "Finite authored thresholds above one select the original warped branch");
  auto wide_filter = kSettings;
  wide_filter.filter_radius_uv = 2.0F / 1024;
  const auto guarded = make_directional_shadow_projection(
      camera, kView, wide_filter, {0, -1000, -.001F});
  require(
      bw::render::shadow_guard_texels(kSettings.filter_radius_uv) == 1 &&
          bw::render::shadow_guard_texels(wide_filter.filter_radius_uv) == 3,
      "Guard must include filter radius plus nearest-sample footprint");
  require(guarded.clip_rows[2] == normal.clip_rows[2] &&
              guarded.clip_rows[3] == normal.clip_rows[3],
          "Guard inset must preserve source homogeneous W and reverse depth");
  constexpr float kGuardRatio = 1018.0F / 1022;
  for (std::size_t row = 0; row < 2; ++row) {
    for (std::size_t column = 0; column < 4; ++column) {
      require(
          std::abs(guarded.clip_rows.at(row).at(column) -
                   normal.clip_rows.at(row).at(column) * kGuardRatio) < .00002F,
          "Caster/receiver clip boundary must reserve exactly the guard");
    }
  }
  reject(camera, kSettings, {0, 0, 0});
  auto invalid = kSettings;
  invalid.perspective_angle_radians = 0;
  reject(camera, invalid);
  invalid = kSettings;
  invalid.maximum_height = invalid.minimum_height;
  reject(camera, invalid);
  invalid = kSettings;
  invalid.filter_radius_uv = .5F;
  reject(camera, invalid);
  invalid.filter_radius_uv = 511.1F / 1024;
  reject(camera, invalid);
  invalid.filter_radius_uv = std::numeric_limits<float>::quiet_NaN();
  reject(camera, invalid);
  auto bad_camera = camera;
  bad_camera.eye[0] = std::numeric_limits<float>::infinity();
  reject(bad_camera, kSettings);

  std::array<std::uint8_t, bw::render::kShadowJitterBytes> volume{}, repeated{};
  bw::render::make_shadow_jitter(volume, 1);
  bw::render::make_shadow_jitter(repeated, 1);
  require(volume == repeated, "Explicit jitter seed must be reproducible");
  const std::array<std::uint8_t, 4> first{115, 184, 231, 251};
  require(std::equal(first.begin(), first.end(), volume.begin()),
          "Jitter must preserve original random order and BGRA conversion");
  bw::render::make_shadow_jitter(repeated, 2);
  require(volume != repeated, "Jitter seed must select the sample stream");
}
