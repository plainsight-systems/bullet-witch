// Exercise shared tree bounds, authored preset selection and atomic rejection
// with synthetic inputs inside resource-tests. Both MCP phases: R.2
// (#rr-use-ptr), MEM.6
// (guidelines/memory/MEM.6-inject-allocators-behind-one-interface.md) apply to
// bounded fixtures and the injected metadata owner.
#include <cmath>

#include "bw/formats/player_camera.hpp"
#include "camera_fixture.hpp"
namespace {
template <class Operation>
void rejects(Operation operation) {
  bool failed = false;
  try {
    operation();
  } catch (const bw::resources::Error&) {
    failed = true;
  }
  camera_fixture::require(failed, "Malformed customization must reject");
}
}  // namespace
void customization_tests() {
  using camera_fixture::Backing;
  using camera_fixture::customization;
  using camera_fixture::require;
  using camera_fixture::scalar;
  using camera_fixture::word;
  const auto domain = bw::memory::Domain::create(std::make_shared<Backing>());
  auto bytes = customization();
  {
    const auto tree =
        bw::formats::CustomizationDefinition::parse(bytes, domain);
    constexpr std::array path{
        std::string_view("witch"), std::string_view("actor"),
        std::string_view("pw0"), std::string_view("behind_camera")};
    require(tree.lookup(path).data() ==
                std::span<const std::uint8_t>(bytes).subspan(192).data(),
            "Tree view retains exact payload location");
    const auto settings =
        bw::formats::PlayerCameraSettings::parse_initial(tree);
    require(settings.height == .7F && settings.offset[0] == .5F &&
                settings.offset[2] == -3 && settings.near_plane == .01F &&
                settings.far_plane == 1000 && settings.transition_speed == 5 &&
                std::abs(settings.vertical_fov_radians - .76794487F) < .000001F,
            "Initial preset selects authored fields");
    require(settings.contexts[1].height == .7F &&
                settings.contexts[3].height == .25F &&
                std::abs(settings.contexts[1].vertical_fov_radians -
                         .34906585F) < .000001F &&
                std::abs(settings.contexts[1].look_radians_per_second[0] -
                         .78539816F) < .000001F &&
                !settings.automatic_targeting_camera,
            "Targeting and crouch settings are decoded independently of "
            "ordinary preset");
    constexpr std::array missing{std::string_view("missing")};
    rejects([&] { (void)tree.lookup(missing); });
  }
  word(bytes, 128, 0);
  rejects([&] {
    (void)bw::formats::CustomizationDefinition::parse(bytes, domain);
  });
  bytes = customization();
  scalar(bytes, 192 + 80, 0);
  rejects([&] {
    (void)bw::formats::PlayerCameraSettings::parse_initial(
        bw::formats::CustomizationDefinition::parse(bytes, domain));
  });
  bytes = customization();
  bytes.resize(128 + 580);
  rejects([&] {
    (void)bw::formats::PlayerCameraSettings::parse_initial(
        bw::formats::CustomizationDefinition::parse(bytes, domain));
  });
  require(domain->snapshot().live_bytes == 0,
          "Customization failures release metadata");
}
