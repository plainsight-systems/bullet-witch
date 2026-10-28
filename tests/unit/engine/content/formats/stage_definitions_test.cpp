// Background tests also apply both-phase C.32 (#rc-dtor-ptr) to borrowed
// names and MEM.9 (guidelines/memory/MEM.9-embedded-allocate-at-init.md) to
// one-time metadata lifetime checks.
// Synthetic load-time records exercise bounded immutable views and failure
// cleanup inside the existing resource-tests scenario. No game data is used.
// Native pointer graphs become offsets; tests distinguish absent pointers from
// invalid targets and verify domain lifetime after successful and failed
// parses. Design/implementation SL.con.3 (#rsl-bounds), MEM.6
// (guidelines/memory/MEM.6-inject-allocators-behind-one-interface.md).
// Fog decoder extension reread R.2 (#rr-use-ptr) and MEM.6 in both phases:
// fixtures preserve bounded authored bytes without native state or allocation.
#include "bw/content/formats/stage_definitions.hpp"

#include <limits>
#include <memory_resource>

#include "../../../../fixtures/model_fixture.hpp"
#include "bw/content/formats/background_placement.hpp"
#include "game/content/original/stage_environment_settings.hpp"

namespace {
class DefinitionBacking final : public bw::memory::CpuBacking {
 public:
  void* allocate(std::size_t bytes, std::size_t alignment) noexcept override {
    try {
      return std::pmr::new_delete_resource()->allocate(bytes, alignment);
    } catch (const std::bad_alloc&) {
      return nullptr;
    }
  }
  void deallocate(void* pointer, std::size_t bytes,
                  std::size_t alignment) noexcept override {
    std::pmr::new_delete_resource()->deallocate(pointer, bytes, alignment);
  }
};
void require(bool condition, const char* message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}
template <typename Parse>
void rejects(Parse parse) {
  bool failed = false;
  try {
    parse();
  } catch (const bw::resources::Error&) {
    failed = true;
  }
  require(failed, "Malformed auxiliary definition must fail");
}
void signature(bw::resources::Bytes& bytes, std::string_view value) {
  std::copy(value.begin(), value.end(), bytes.begin());
}
void collision(const std::shared_ptr<bw::memory::Domain>& domain) {
  using model_fixture::word;
  constexpr std::size_t kHeader = 48, kPart = 64, kPartSize = 96;
  constexpr std::size_t kVertexBytes = 36, kFaceBytes = 8;
  constexpr std::size_t kCountField = 36, kTableField = 40;
  constexpr std::size_t kFaceCountField = 44, kFaceTableField = 48;
  constexpr std::size_t kNameField = 64;
  bw::resources::Bytes bytes(kPart + kPartSize + kVertexBytes + kFaceBytes);
  word(bytes, kCountField, 1);
  word(bytes, kTableField, kHeader);
  word(bytes, kHeader, kPart);
  word(bytes, kPart + kCountField, 3);
  word(bytes, kPart + kTableField, kPartSize);
  word(bytes, kPart + kFaceCountField, 1);
  word(bytes, kPart + kFaceTableField, kPartSize + kVertexBytes);
  bytes.at(kPart + kNameField) = 'a';
  const auto face = kPart + kPartSize + kVertexBytes;
  bytes.at(face + 2) = 1;
  bytes.at(face + 4) = 2;
  {
    auto result = bw::formats::CollisionDefinition::parse(bytes, domain);
    require(result.parts().size() == 1 && result.parts().front().name == "a" &&
                result.parts().front().triangles.data() ==
                    std::span<const std::uint8_t>(bytes).subspan(face).data(),
            "Collision immutable view retains geometry identity");
  }
  bytes.at(face + 4) = 3;
  rejects(
      [&] { (void)bw::formats::CollisionDefinition::parse(bytes, domain); });
}
void physics(const std::shared_ptr<bw::memory::Domain>& domain) {
  using model_fixture::word;
  constexpr std::size_t kBytes = 128, kRoot = 64, kChild = 80, kData = 112;
  constexpr std::size_t kVersion = 8, kChildDataField = 8;
  bw::resources::Bytes bytes(kBytes);
  word(bytes, kVersion, 0x20060411);
  word(bytes, 0, kRoot);
  word(bytes, kRoot, 1);
  word(bytes, kRoot + 4, kChild - kRoot);
  word(bytes, kChild + kChildDataField, kData - kChild);
  {
    const auto result =
        bw::formats::PhysicsCollisionDefinition::parse(bytes, domain);
    require(result.references().size() == 3 &&
                result.references().back().target_offset == kData,
            "Physics graph preserves nested relative pointer bases");
  }
  // Body directory preserves native order and signed joint identifiers.
  word(bytes, 16, 1);
  word(bytes, 20, 56);
  word(bytes, 56, 88);
  word(bytes, 96, 3);
  {
    const auto result =
        bw::formats::PhysicsCollisionDefinition::parse(bytes, domain);
    require(
        result.body_joints().size() == 1 && result.body_joints().front() == 3,
        "Physics body directory retains joint association");
  }
  word(bytes, 56, 0);
  {
    const auto result =
        bw::formats::PhysicsCollisionDefinition::parse(bytes, domain);
    require(result.body_joints().front() == -1,
            "Null physics body retains an unresolved joint slot");
  }
  word(bytes, kChild + kChildDataField, kBytes);
  rejects([&] {
    (void)bw::formats::PhysicsCollisionDefinition::parse(bytes, domain);
  });
}
void animation(const std::shared_ptr<bw::memory::Domain>& domain) {
  using model_fixture::word;
  constexpr std::size_t kTrack = 32, kCountField = 20, kDirectory = 24;
  constexpr std::size_t kFirstData = 24, kSecondData = 28, kData = 64;
  bw::resources::Bytes bytes(kData + 8);
  signature(bytes, "MTAB");
  word(bytes, kCountField, 1);
  word(bytes, kDirectory, kTrack);
  word(bytes, kTrack + kFirstData, kData);
  word(bytes, kTrack + kSecondData, kData + 4);
  {
    const auto result =
        bw::formats::MaterialAnimationDefinition::parse(bytes, domain);
    require(result.tracks().size() == 1 &&
                result.tracks().front().first_data_offset == kData,
            "Animation track keeps original data offsets");
  }
  word(bytes, kTrack + kSecondData, static_cast<std::uint32_t>(bytes.size()));
  rejects([&] {
    (void)bw::formats::MaterialAnimationDefinition::parse(bytes, domain);
  });
  constexpr std::size_t kScalarHeader = 24, kChannels = 28, kValues = 32;
  bytes.assign(kValues + 4, 0);
  signature(bytes, "FLAN");
  word(bytes, 4, 0x00080001);
  word(bytes, 8, 1);
  word(bytes, 12, kScalarHeader);
  word(bytes, 16, kChannels);
  word(bytes, kCountField, kValues);
  require(
      bw::formats::ScalarAnimationDefinition::parse(bytes).times.size() == 4,
      "Scalar animation binds independent source sections");
  word(bytes, 16, kScalarHeader);
  rejects([&] { (void)bw::formats::ScalarAnimationDefinition::parse(bytes); });
}
void area(const std::shared_ptr<bw::memory::Domain>& domain) {
  using model_fixture::word;
  constexpr std::size_t kDirectory = 32, kRecord = 64, kRecordBytes = 96;
  bw::resources::Bytes bytes(kRecord + kRecordBytes);
  signature(bytes, "AREA");
  word(bytes, 4, 1);
  word(bytes, 8, 1);
  word(bytes, 12, kDirectory);
  word(bytes, kDirectory, kRecord);
  require(bw::formats::AreaDefinition::parse(bytes, domain)
                  .records()
                  .front()
                  .size() == kRecordBytes,
          "Area record directory bounds its original records");
  word(bytes, kDirectory, kRecord + 1);
  rejects([&] { (void)bw::formats::AreaDefinition::parse(bytes, domain); });
}
void environment(const std::shared_ptr<bw::memory::Domain>& domain) {
  using model_fixture::word;
  constexpr std::size_t kNames = 32, kNodes = 64, kNodeBytes = 16;
  constexpr std::size_t kPayload = kNodes + 3 * kNodeBytes;
  constexpr std::size_t kSettingsBytes = 0x160;
  constexpr std::uint32_t kAbsent = 0xffffffff;
  constexpr std::size_t kLightNode = kPayload + kSettingsBytes;
  bw::resources::Bytes bytes(kLightNode + 2 * kNodeBytes + 12);
  signature(bytes, "CSTM");
  word(bytes, 4, 0x04030201);
  word(bytes, 12, kNames);
  word(bytes, 16, kNodes);
  constexpr std::string_view kPath("witch\0stage\0test\0", 17);
  std::copy(kPath.begin(), kPath.end(), bytes.begin() + kNames);
  for (std::size_t node = 0; node < 3; ++node) {
    word(bytes, kNodes + node * kNodeBytes, kAbsent);
    word(bytes, kNodes + node * kNodeBytes + 4,
         node == 2 ? kAbsent
                   : static_cast<std::uint32_t>((node + 1) * kNodeBytes));
    word(bytes, kNodes + node * kNodeBytes + 8,
         static_cast<std::uint32_t>(node * 6));
  }
  constexpr std::size_t kLightName = 18;
  constexpr std::string_view kLightPath("light\0", 6);
  std::copy(kLightPath.begin(), kLightPath.end(),
            bytes.begin() + kNames + kLightName);
  word(bytes, kNodes + kNodeBytes,
       static_cast<std::uint32_t>(kLightNode - kNodes));
  word(bytes, kLightNode, kAbsent);
  word(bytes, kLightNode + 4,
       static_cast<std::uint32_t>(kLightNode + kNodeBytes - kNodes));
  word(bytes, kLightNode + 8, kLightName);
  word(bytes, kLightNode + kNodeBytes, kAbsent);
  word(bytes, kLightNode + kNodeBytes + 4, kAbsent);
  word(bytes, kLightNode + kNodeBytes + 8, 12);
  for (std::size_t lane = 0; lane < 3; ++lane) {
    word(bytes, kLightNode + 2 * kNodeBytes + lane * 4,
         std::bit_cast<std::uint32_t>(.5F));
  }
  const auto scalar = [&](std::size_t field, float value) {
    word(bytes, kPayload + field, std::bit_cast<std::uint32_t>(value));
  };
  scalar(0, 0.25f);
  scalar(4, 0.5f);
  scalar(8, 2.0f);
  scalar(0xc0, 15.0f);
  scalar(0xc4, 2.0f);
  scalar(0xc8, 0.5f);
  scalar(0xf8, 1000.0f);
  bytes.at(kPayload + 0x111) = 1;
  bytes.at(kPayload + 0x113) = 1;
  scalar(0xec, .058f);
  scalar(0x100, 1000);
  scalar(0x108, -1);
  scalar(0x10c, 100);
  scalar(0x124, .85f);
  scalar(0x128, 100);
  // Area override and surface fog distance enable are adjacent distinct bytes.
  bytes.at(kPayload + 0x24) = 1;
  bytes.at(kPayload + 0x34) = 1;
  bytes.at(kPayload + 0x40) = 1;
  scalar(0x28, 1000);
  scalar(0x2c, 0);
  scalar(0x30, .125f);
  scalar(0x38, 10);
  scalar(0x3c, 20);
  scalar(0x44, -10);
  scalar(0x48, 30);
  scalar(0x4c, .25f);
  bytes.at(kPayload + 0x50) = 1;
  scalar(0x54, 2000);
  scalar(0x58, 100);
  scalar(0x5c, .5f);
  scalar(0x7c, .25f);
  scalar(0x80, .5f);
  scalar(0x84, .75f);
  bytes.at(kPayload + 0x134) = 1;
  scalar(0x138, 0.18f);
  scalar(0x13c, 2.2f);
  scalar(0x158, 0.001f);
  scalar(0x15c, 64.0f);
  word(bytes, kPayload + 0x150, 0xffffffff);
  auto parse = [&] {
    return bw::formats::StageEnvironment::parse(bytes, "test", domain);
  };
  scalar(0x1c, .75F);
  scalar(0x20, 1.25F);
  const auto result = parse();
  require(result.local_light_color == std::array{.5F, .5F, .5F} &&
              result.sky_yaw_degrees == 15 && result.sky_height == 2 &&
              result.ambient_color == std::array{0.25f, 0.5f, 2.0f} &&
              result.character_light_intensity == .75F &&
              result.enemy_light_intensity == 1.25F &&
              result.light_diffuse.front() == 0.5f &&
              result.light_vector.at(1) == 1000 &&
              result.tone_mapping.enabled &&
              result.tone_mapping.gamma == 2.2f &&
              result.tone_mapping.history_window == -1,
          "Stage settings resolve authored nested payload fields");
  require(result.shadow.setup_enabled && result.shadow.casters_enabled &&
              result.shadow.cull_selector == 0 &&
              result.shadow.receiver_radius == .058f &&
              result.shadow.raster_depth_bias == 0 &&
              result.shadow.raster_slope_bias == 0 &&
              result.shadow.far_distance == 1000 &&
              result.shadow.minimum_height == -1 &&
              result.shadow.maximum_height == 100 &&
              result.shadow.alignment_threshold == .85f &&
              result.shadow.parallel_near_extension == 100,
          "Shadow controls preserve independent gates and receiver radius");
  require(!result.surface_fog.distance_enabled &&
              result.surface_fog.height_gate_enabled &&
              result.surface_fog.height_enabled &&
              result.surface_fog.distance_start == 1000 &&
              result.surface_fog.distance_end == 0 &&
              result.surface_fog.density == .125f &&
              result.surface_fog.height_gate == std::array{10.f, 20.f} &&
              result.surface_fog.height_range == std::array{-10.f, 30.f} &&
              result.surface_fog.height_opacity == .25f &&
              result.sky_fog.distance_enabled &&
              !result.sky_fog.height_gate_enabled &&
              result.sky_fog.distance_start == 2000 &&
              result.sky_fog.distance_end == 100 &&
              result.sky_fog.density == .5f &&
              result.fog_color == std::array{.25f, .5f, .75f},
          "Fog blocks preserve independent controls, signed ranges and shared "
          "color");
  bytes.at(kPayload + 0x25) = 1;
  require(parse().surface_fog.distance_enabled,
          "Surface fog distance enable is independent of the area override");
  scalar(0x5c, std::numeric_limits<float>::infinity());
  rejects([&] { (void)parse(); });
  scalar(0x5c, .5f);
  rejects([&] {
    (void)bw::formats::StageEnvironment::parse(bytes, "missing", domain);
  });
  word(bytes, kNodes, 0);
  rejects([&] { (void)parse(); });
  word(bytes, kNodes, kAbsent);
  word(bytes, kNodes + 8, 2000);
  rejects([&] { (void)parse(); });
  word(bytes, kNodes + 8, 0);
  scalar(0x13c, 0.0f);
  rejects([&] { (void)parse(); });
  scalar(0x13c, 2.2f);
  scalar(4, std::numeric_limits<float>::infinity());
  rejects([&] { (void)parse(); });
  scalar(4, 0.5f);
  bytes.pop_back();
  rejects([&] { (void)parse(); });
}
void background(const std::shared_ptr<bw::memory::Domain>& domain) {
  using model_fixture::word;
  bw::resources::Bytes bytes(128);
  signature(bytes, "BGOB");
  word(bytes, 4, 0x00010001);
  word(bytes, 8, 1);
  word(bytes, 12, 24);
  word(bytes, 16, 1);
  word(bytes, 20, 28);
  word(bytes, 24, 32);
  word(bytes, 28, 40);
  bytes.at(32) = 'b';
  word(bytes, 56, 0xffff0000);
  word(bytes, 60, 1);
  word(bytes, 64, std::bit_cast<std::uint32_t>(1.0F));
  word(bytes, 80, std::bit_cast<std::uint32_t>(1.0F));
  word(bytes, 96, std::bit_cast<std::uint32_t>(1.0F));
  word(bytes, 100, std::bit_cast<std::uint32_t>(12.0F));
  const auto parse = [&] {
    return bw::formats::BackgroundPlacementDefinition::parse(bytes, domain);
  };
  {
    const auto placement = parse();
    require(
        placement.names().at(0) == "b" &&
            placement.placements().at(0).group == -1 &&
            placement.matrices().at(0).at(3).at(0) == 12 &&
            placement.matrices().at(0).at(3).at(3) == 1,
        "Background source transforms retain translation and homogeneous row");
  }
  word(bytes, 56, 0xffff0001);
  rejects([&] { (void)parse(); });
  word(bytes, 56, 0xffff0000);
  word(bytes, 100,
       std::bit_cast<std::uint32_t>(std::numeric_limits<float>::infinity()));
  rejects([&] { (void)parse(); });
  word(bytes, 100, 0);
  word(bytes, 60, 2);
  rejects([&] { (void)parse(); });
  word(bytes, 60, 1);
  word(bytes, 4, 0x00030001);
  rejects([&] { (void)parse(); });
  // Version 1.2 carries an additional parameter vector before the name index.
  word(bytes, 4, 0x00020001);
  word(bytes, 56, 0xffffffff);
  word(bytes, 60, 0);
  word(bytes, 64, 0);
  word(bytes, 68, 0);
  word(bytes, 72, 0xffff0000);
  word(bytes, 76, 1);
  word(bytes, 80, std::bit_cast<std::uint32_t>(1.0F));
  word(bytes, 96, std::bit_cast<std::uint32_t>(1.0F));
  word(bytes, 112, std::bit_cast<std::uint32_t>(1.0F));
  word(bytes, 116, std::bit_cast<std::uint32_t>(9.0F));
  const auto placement = parse();
  require(
      placement.placements().at(0).secondary_parameters.at(0) == 0xffffffff &&
          placement.matrices().at(0).at(3).at(0) == 9,
      "Background version selects the correct prefix and matrix layout");
}
}  // namespace
void stage_definition_tests() {
  const auto domain =
      bw::memory::Domain::create(std::make_shared<DefinitionBacking>());
  collision(domain);
  physics(domain);
  animation(domain);
  area(domain);
  environment(domain);
  background(domain);
  require(domain->snapshot().live_bytes == 0,
          "Auxiliary parse paths release all metadata");
}
