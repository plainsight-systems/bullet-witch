// Synthetic load-time records exercise bounded immutable views and failure
// cleanup inside the existing resource-tests scenario. No game data is used.
// Native pointer graphs become offsets; tests distinguish absent pointers from
// invalid targets and verify domain lifetime after successful and failed
// parses. Design/implementation SL.con.3 (#rsl-bounds), MEM.6
// (guidelines/memory/MEM.6-inject-allocators-behind-one-interface.md).
#include "bw/formats/stage_definitions.hpp"

#include <limits>
#include <memory_resource>

#include "bw/formats/stage_environment.hpp"
#include "model_fixture.hpp"

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
  bw::resources::Bytes bytes(kPayload + kSettingsBytes);
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
  bytes.at(kPayload + 0x134) = 1;
  scalar(0x138, 0.18f);
  scalar(0x13c, 2.2f);
  scalar(0x158, 0.001f);
  scalar(0x15c, 64.0f);
  word(bytes, kPayload + 0x150, 0xffffffff);
  auto parse = [&] {
    return bw::formats::StageEnvironment::parse(bytes, "test", domain);
  };
  const auto result = parse();
  require(result.sky_yaw_degrees == 15 && result.sky_height == 2 &&
              result.ambient_color == std::array{0.25f, 0.5f, 2.0f} &&
              result.light_diffuse.front() == 0.5f &&
              result.light_vector.at(1) == 1000 &&
              result.tone_mapping.enabled &&
              result.tone_mapping.gamma == 2.2f &&
              result.tone_mapping.history_window == -1,
          "Stage settings resolve authored nested payload fields");
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
}  // namespace
void stage_definition_tests() {
  const auto domain =
      bw::memory::Domain::create(std::make_shared<DefinitionBacking>());
  collision(domain);
  physics(domain);
  animation(domain);
  area(domain);
  environment(domain);
  require(domain->snapshot().live_bytes == 0,
          "Auxiliary parse paths release all metadata");
}
