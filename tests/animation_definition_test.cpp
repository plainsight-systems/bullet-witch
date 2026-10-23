// Authored in-process animation construction cases: real directory offsets,
// shared/absent channels, affine bind inverse and invalid graphs. No game
// files, sampling or device execution. Fixtures own bytes; decoded views borrow
// them. Both-phase I.11/MEM.6 guidance keeps ownership and injected-domain
// teardown observable. The test-only backing provider delegates to the process
// heap.
#include "bw/formats/animation_definition.hpp"

#include <cmath>
#include <memory_resource>
#include <stdexcept>

#include "model_fixture.hpp"

namespace {
class AnimationBacking final : public bw::memory::CpuBacking {
 public:
  void* allocate(std::size_t bytes, std::size_t alignment) noexcept override {
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
};
void require(bool value, const char* message) {
  if (!value) throw std::runtime_error(message);
}
template <class Function>
void rejects(Function function) {
  bool rejected = false;
  try {
    function();
  } catch (const bw::resources::Error&) {
    rejected = true;
  }
  require(rejected, "Malformed animation construction must reject");
}
void scalar(bw::resources::Bytes& bytes, std::size_t offset, float value) {
  model_fixture::word(bytes, offset, std::bit_cast<std::uint32_t>(value));
}
}  // namespace
void animation_definition_tests() {
  using bw::formats::MotionDefinition;
  using bw::formats::ShapeAnimationDefinition;
  using bw::formats::SkeletonDefinition;
  using model_fixture::word;
  auto domain =
      bw::memory::Domain::create(std::make_shared<AnimationBacking>());
  {
    bw::resources::Bytes bytes(140);
    word(bytes, 0, 0x00414237);
    word(bytes, 20, 1);
    word(bytes, 32, 36);
    word(bytes, 36 + 52, 92);
    word(bytes, 92, 0x46000000);
    word(bytes, 100, 1);
    word(bytes, 104, 12);
    word(bytes, 116, 124);
    word(bytes, 120, 128);
    const auto definition = MotionDefinition::parse(bytes, domain);
    require(definition.tracks().size() == 1 &&
                !definition.whole_transform_channels(),
            "Separate motion track directory");
    const auto& track = definition.tracks().front();
    require(
        !track.channels.at(0) && !track.channels.at(1) && track.channels.at(2),
        "Absent motion channels remain absent");
    const auto& expected_channel = track.channels.at(2);
    if (!expected_channel) {
      throw std::runtime_error("Missing expected channel");
    }
    const auto& channel = *expected_channel;
    require(channel.times.data() == std::span(bytes).subspan(124).data() &&
                channel.values.size() == 12,
            "Channel references borrow exact source regions");
    word(bytes, 120, 136);
    rejects([&] { (void)MotionDefinition::parse(bytes, domain); });
  }
  {
    bw::resources::Bytes bytes(108);
    word(bytes, 0, 0x4241425f);
    word(bytes, 20, 1);
    word(bytes, 32, 36);
    word(bytes, 44, 1);
    word(bytes, 56, 64);
    word(bytes, 60, 68);
    const auto definition = MotionDefinition::parse(bytes, domain);
    const auto& whole_channel = definition.tracks().front().channels.at(0);
    require(definition.whole_transform_channels() && whole_channel &&
                whole_channel->values.size() == 40,
            "Whole-transform motion retains complete sample");
  }
  {
    bw::resources::Bytes bytes(96);
    word(bytes, 0, 0x4241535f);
    word(bytes, 20, 1);
    bytes.at(24) = 'x';
    word(bytes, 56, 60);
    word(bytes, 68, 1);
    word(bytes, 80, 88);
    word(bytes, 84, 92);
    const auto shape = ShapeAnimationDefinition::parse(bytes, domain);
    require(shape.channels().front().name == "x" &&
                shape.channels().front().channel &&
                shape.channels().front().channel->values.size() == 4,
            "Named shape channel preserves scalar stride");
    word(bytes, 84, 96);
    rejects([&] { (void)ShapeAnimationDefinition::parse(bytes, domain); });
  }
  {
    bw::resources::Bytes bytes(112);
    word(bytes, 0, 0x424c425f);
    word(bytes, 4, 1);
    word(bytes, 12, 0xffffffff);
    for (auto offset : {32U, 72U}) {
      scalar(bytes, offset + 12, 1);
      scalar(bytes, offset + 16, 4);
      scalar(bytes, offset + 20, 6);
      scalar(bytes, offset + 24, 8);
      scalar(bytes, offset + 28, 2);
      scalar(bytes, offset + 32, 3);
      scalar(bytes, offset + 36, 4);
    }
    scalar(bytes, 40, std::sqrt(.5F));
    scalar(bytes, 44, std::sqrt(.5F));
    const auto skeleton = SkeletonDefinition::parse(bytes, domain);
    require(std::abs(skeleton.bones().front().relative_matrix.at(0).at(1) -
                     2.0F) < .00001F,
            "Relative quaternion applies scale in original row-vector order");
    const auto& inverse = skeleton.bones().front().inverse_bind_matrix;
    require(inverse.at(0).at(0) == .5F &&
                std::abs(inverse.at(1).at(1) - 1.0F / 3.0F) < .00001F &&
                inverse.at(2).at(2) == .25F,
            "Nonuniform scale bind inverse");
    require(inverse.at(3) == std::array<float, 3>{-2, -2, -2},
            "Bind inverse transforms translation in row-vector order");
    word(bytes, 12, 0);
    rejects([&] { (void)SkeletonDefinition::parse(bytes, domain); });
    word(bytes, 12, 0xffffffff);
    scalar(bytes, 100, 0);
    rejects([&] { (void)SkeletonDefinition::parse(bytes, domain); });
  }
  require(domain->snapshot().live_bytes == 0,
          "Animation metadata releases its injected domain charges");
}
