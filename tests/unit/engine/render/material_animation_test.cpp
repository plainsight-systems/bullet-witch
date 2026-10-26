// Both-phase P.9 (#rp-waste) and MEM.9 also cover validated unaligned loads
// without repeated decoding or steady-state allocation.
// Independent float2-curve fixtures exercise nonuniform key timing, wrapping,
// endpoint clamping, malformed input and borrowed sampling without allocation.
// Existing resource-tests only; no game files, clocks or device processes.
// Both-phase I.1 (#ri-explicit) checks seconds/frame units and input rejection;
// MEM.9 (guidelines/memory/MEM.9-embedded-allocate-at-init.md) checks that
// steady-state evaluation does not request backing storage. The synthetic
// fixture/provider use the existing test-only process heap exception.
#include "bw/render/materials/material_animation.hpp"

#include <cmath>
#include <limits>
#include <memory_resource>
#include <stdexcept>

#include "../../../fixtures/model_fixture.hpp"

namespace {
class CurveBacking final : public bw::memory::CpuBacking {
 public:
  void* allocate(std::size_t bytes, std::size_t alignment) noexcept override {
    ++requests;
    try {
      return std::pmr::new_delete_resource()->allocate(bytes, alignment);
    } catch (const std::bad_alloc&) {
      return nullptr;
    }
  }
  void deallocate(void* value, std::size_t bytes,
                  std::size_t alignment) noexcept override {
    std::pmr::new_delete_resource()->deallocate(value, bytes, alignment);
  }
  std::size_t requests{};
};
constexpr std::size_t kTrack = 32, kTimes = 64, kValues = 76;
constexpr std::size_t kDurationField = 8, kRateField = 12;
constexpr std::size_t kTrackCount = 8, kStride = 12, kValuesField = 28;
void scalar(bw::resources::Bytes& bytes, std::size_t offset, float value) {
  model_fixture::word(bytes, offset, std::bit_cast<std::uint32_t>(value));
}
bw::resources::Bytes fixture() {
  using model_fixture::word;
  bw::resources::Bytes bytes(kValues + 6 * sizeof(float));
  bytes[0] = 'M';
  bytes[1] = 'T';
  bytes[2] = 'A';
  bytes[3] = 'B';
  scalar(bytes, kDurationField, 10);
  scalar(bytes, kRateField, 2);
  word(bytes, 16, 1);
  word(bytes, 20, 1);
  word(bytes, 24, kTrack);
  word(bytes, kTrack, 0x46000000);
  word(bytes, kTrack + kTrackCount, 3);
  word(bytes, kTrack + kStride, 8);
  word(bytes, kTrack + 24, kTimes);
  word(bytes, kTrack + kValuesField, kValues);
  // Frames 1,3,9 leave both endpoint clamps observable inside a ten-frame loop.
  const std::array<float, 3> times{1, 3, 9};
  const std::array<float, 6> values{2, 4, 6, 0, -6, 12};
  for (std::size_t i = 0; i < times.size(); ++i) {
    scalar(bytes, kTimes + i * sizeof(float), times.at(i));
  }
  for (std::size_t i = 0; i < values.size(); ++i) {
    scalar(bytes, kValues + i * sizeof(float), values.at(i));
  }
  return bytes;
}
void require(bool value, const char* message) {
  if (!value) throw std::runtime_error(message);
}
void near(std::array<float, 2> actual, std::array<float, 2> expected) {
  require(std::abs(actual[0] - expected[0]) < 0.0001F &&
              std::abs(actual[1] - expected[1]) < 0.0001F,
          "Material curve differs from independent key interpolation");
}
template <class Function>
void rejects(Function function) {
  bool rejected = false;
  try {
    function();
  } catch (const bw::resources::Error&) {
    rejected = true;
  }
  require(rejected, "Malformed material curve must reject before sampling");
}
}  // namespace

void material_animation_tests() {
  auto backing = std::make_shared<CurveBacking>();
  auto domain = bw::memory::Domain::create(backing);
  auto bytes = fixture();
  {
    const auto clip =
        bw::formats::MaterialAnimationDefinition::parse(bytes, domain);
    const auto curve = bw::render::MaterialFloat2Curve::bind(clip, 0);
    const auto requests = backing->requests;
    near(curve.sample(0), {2, 4});
    near(curve.sample(.5), {2, 4});
    near(curve.sample(1), {4, 2});
    near(curve.sample(1.5), {6, 0});
    near(curve.sample(3), {0, 6});
    near(curve.sample(4.75), {-6, 12});
    near(curve.sample(5), {2, 4});
    near(curve.sample(6), {4, 2});
    require(backing->requests == requests,
            "Borrowed curve evaluation requests no new backing storage");
    for (double time : {-1.0, std::numeric_limits<double>::infinity(),
                        std::numeric_limits<double>::quiet_NaN()}) {
      bool rejected = false;
      try {
        (void)curve.sample(time);
      } catch (const std::invalid_argument&) {
        rejected = true;
      }
      require(rejected, "Invalid elapsed time must reject");
    }
  }
  {
    auto clamped_bytes = bytes;
    model_fixture::word(clamped_bytes, 16, 0);
    const auto clip =
        bw::formats::MaterialAnimationDefinition::parse(clamped_bytes, domain);
    const auto curve = bw::render::MaterialFloat2Curve::bind(clip, 0);
    near(curve.sample(5), {-6, 12});
    near(curve.sample(6), {-6, 12});
  }
  {
    // File offsets need not satisfy native float alignment. Prepared loads must
    // preserve interpolation without aliasing or alignment-dependent access.
    auto unaligned = bytes;
    unaligned.insert(unaligned.begin() + kTimes, 0);
    model_fixture::word(unaligned, kTrack + 24, kTimes + 1);
    model_fixture::word(unaligned, kTrack + kValuesField, kValues + 1);
    const auto clip =
        bw::formats::MaterialAnimationDefinition::parse(unaligned, domain);
    const auto curve = bw::render::MaterialFloat2Curve::bind(clip, 0);
    near(curve.sample(1), {4, 2});
    near(curve.sample(3), {0, 6});
  }
  const auto reject_bytes = [&](const bw::resources::Bytes& invalid) {
    rejects([&] {
      const auto clip =
          bw::formats::MaterialAnimationDefinition::parse(invalid, domain);
      (void)bw::render::MaterialFloat2Curve::bind(clip, 0);
    });
  };
  auto invalid = bytes;
  scalar(invalid, kTimes + sizeof(float), 1);
  reject_bytes(invalid);
  invalid = bytes;
  scalar(invalid, kTimes, -1);
  reject_bytes(invalid);
  invalid = bytes;
  scalar(invalid, kValues, std::numeric_limits<float>::quiet_NaN());
  reject_bytes(invalid);
  invalid = bytes;
  scalar(invalid, kDurationField, 0);
  reject_bytes(invalid);
  invalid = bytes;
  scalar(invalid, kRateField, std::numeric_limits<float>::infinity());
  reject_bytes(invalid);
  invalid = bytes;
  model_fixture::word(invalid, kTrack + kStride, 4);
  reject_bytes(invalid);
  invalid = bytes;
  model_fixture::word(invalid, kTrack + kTrackCount, 0);
  reject_bytes(invalid);
  invalid = bytes;
  invalid.resize(invalid.size() - 1);
  reject_bytes(invalid);
  require(domain->snapshot().live_bytes == 0,
          "Curve construction failures release all owned track metadata");
}
