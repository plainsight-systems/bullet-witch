// Synthetic public contracts for immutable scalar sampling and area queries.
// Original algorithms have separate ignored x86 witnesses on real stage data.
// Both phases: CP.3 checks owned immutable data; MEM.9 checks no query growth.
#include <limits>
#include <numbers>

#include "bw/animation/scalar_animation.hpp"
#include "bw/scene/environment.hpp"
#include "bw/scene/spatial/area_regions.hpp"
#include "camera_fixture.hpp"
namespace {
bw::resources::Bytes scalar_track() {
  using camera_fixture::scalar;
  using camera_fixture::word;
  bw::resources::Bytes bytes(384);
  word(bytes, 0, 0x4e414c46);
  word(bytes, 4, 1);
  word(bytes, 8, 3);
  word(bytes, 12, 24);
  word(bytes, 16, 36);
  word(bytes, 20, 64);
  scalar(bytes, 24, 0);
  scalar(bytes, 28, 1);
  scalar(bytes, 32, 1);
  bytes.at(37) = 1;
  for (std::uint32_t frame = 0; frame < 3; ++frame) {
    word(bytes, 64 + frame * 4, 80 + frame * 100);
    for (std::uint32_t channel = 0; channel < 25; ++channel) {
      scalar(bytes, 80 + frame * 100 + channel * 4,
             static_cast<float>(frame * 10 + channel));
    }
  }
  return bytes;
}
bw::resources::Bytes region_data() {
  using camera_fixture::scalar;
  using camera_fixture::word;
  bw::resources::Bytes bytes(224);
  word(bytes, 0, 0x41455241);
  word(bytes, 4, 1);
  word(bytes, 8, 2);
  word(bytes, 12, 16);
  word(bytes, 16, 32);
  word(bytes, 20, 128);
  for (std::size_t record = 0; record < 2; ++record) {
    const auto at = 32 + record * 96;
    scalar(bytes, at, 10);
    scalar(bytes, at + 12, 10);
    scalar(bytes, at + 16, 1);
    scalar(bytes, at + 20, 2);
    scalar(bytes, at + 24, 3);
    scalar(bytes, at + 40, std::numbers::pi_v<float> / 2);
    scalar(bytes, at + 48, 10);
    word(bytes, at + 64, static_cast<std::uint32_t>(record + 7));
  }
  return bytes;
}
}  // namespace
void scalar_area_tests() {
  using camera_fixture::require;
  auto domain =
      bw::memory::Domain::create(std::make_shared<camera_fixture::Backing>());
  const auto initial = domain->snapshot().live_bytes;
  {
    auto bytes = scalar_track();
    bw::animation::ScalarAnimation track(bytes, domain);
    bytes.clear();
    require(
        track.version() == 0x100 && track.channel_count() == 25,
        "Prepared track retains canonical version and source channel count");
    require(track.sample(.5F)[0] == 0 && track.sample(.5F, true)[0] == 5 &&
                track.sample(.5F, true)[1] == 1,
            "Interpolation policy distinguishes linear and held channels");
    require(
        track.sample(1)[0] == 20 && track.sample(100)[0] == 20 &&
            track.sample(-1, true)[0] == 0 && track.sample(1)[25] == 0,
        "Duplicate keys select last, endpoints clamp and absent lanes zero");
    const auto region_bytes = region_data();
    const auto definition =
        bw::formats::AreaDefinition::parse(region_bytes, domain);
    bw::scene::AreaRegions regions(definition, domain);
    require(regions.containing_id({8.5F, 0, 0}) == 7 &&
                !regions.containing_id({10, 1.5F, 0}),
            "Inverse rotated region uses first matching authored identifier");
    require(!regions.containing_id({10, 0, 3}) &&
                !regions.containing_id({30, 0, 0}),
            "Box face and points outside bounding sphere are excluded");
    const auto bytes_before = domain->snapshot().live_bytes;
    for (int i = 0; i < 10; ++i) {
      (void)track.sample(static_cast<float>(i) / 10, true);
      (void)regions.containing_id({10, 0, 0});
    }
    require(domain->snapshot().live_bytes == bytes_before,
            "Prepared region and animation queries allocate no storage");
    const std::array bindings{bw::scene::EnvironmentRegionBinding{0, 0},
                              bw::scene::EnvironmentRegionBinding{7, 1}};
    bw::scene::EnvironmentChannels channels;
    channels.values[0] = 4;
    channels.smooth[0] = true;
    bw::scene::Environment environment(std::move(regions), std::move(track),
                                       bindings, channels,
                                       {1.0F / 32, 31.0F / 32, .0001F}, domain);
    const auto prepared_bytes = domain->snapshot().live_bytes;
    environment.update({10, 0, 0});
    require(environment.values()[0] == 4.5F && environment.values()[1] == 21,
            "Selected region transitions continuous channels but switches "
            "discrete channels immediately");
    environment.update({30, 0, 0});
    require(
        environment.values()[0] == 4.359375F && environment.values()[1] == 1,
        "Leaving regions selects fallback and blends from retained current "
        "state");
    environment.update({10, 0, 0}, true);
    require(
        environment.values()[0] == 20 &&
            domain->snapshot().live_bytes == prepared_bytes,
        "Explicit snap publishes selected target without runtime allocation");
    bool rejected = false;
    try {
      environment.update({std::numeric_limits<float>::infinity(), 0, 0});
    } catch (const std::invalid_argument&) {
      rejected = true;
    }
    require(rejected, "Nonfinite scalar query rejects");
  }
  require(domain->snapshot().live_bytes == initial,
          "Prepared sampling owners release storage");
  auto invalid = scalar_track();
  camera_fixture::word(invalid, 4, 2);
  bool rejected = false;
  try {
    (void)bw::animation::ScalarAnimation(invalid, domain);
  } catch (const std::invalid_argument&) {
    rejected = true;
  }
  require(rejected, "Unknown scalar version is not silently interpreted");
}
