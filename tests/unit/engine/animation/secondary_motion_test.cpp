// Public secondary-pose contracts: weighted authored frames, malformed source
// rejection, constrained chain motion/reset, finite state and fixed update
// storage. In-memory inputs; native costume acceptance is separate research.
// Both-phase I.1/SL.con.3/MEM.9 follow the production component contracts.
#include "bw/animation/secondary_motion.hpp"

#include <bit>
#include <cmath>
#include <memory_resource>
#include <stdexcept>

#include "../../../fixtures/model_fixture.hpp"
namespace {
void require(bool value, const char* message) {
  if (!value) throw std::runtime_error(message);
}
template <class Function>
void rejects(Function function) {
  bool rejected = false;
  try {
    function();
  } catch (const std::exception&) {
    rejected = true;
  }
  require(rejected, "Invalid secondary input must reject");
}
class Backing final : public bw::memory::CpuBacking {
 public:
  std::size_t allocations{}, releases{};
  void* allocate(std::size_t bytes, std::size_t alignment) noexcept override {
    try {
      ++allocations;
      return std::pmr::new_delete_resource()->allocate(bytes, alignment);
    } catch (const std::bad_alloc&) {
      return nullptr;
    }
  }
  void deallocate(void* pointer, std::size_t bytes,
                  std::size_t alignment) noexcept override {
    ++releases;
    std::pmr::new_delete_resource()->deallocate(pointer, bytes, alignment);
  }
};
}  // namespace
void secondary_motion_tests() {
  using bw::animation::evaluate_control_points;
  using bw::animation::kIdentityMatrix;
  using bw::animation::Matrix;
  using bw::animation::SecondaryCapsuleDefinition;
  using bw::animation::SecondaryMotion;
  using bw::animation::SecondaryNodeDefinition;
  using bw::animation::SecondaryNodeKind;
  const auto backing = std::make_shared<Backing>();
  auto domain = bw::memory::Domain::create(backing);
  {
    bw::resources::Bytes bytes(68);
    using model_fixture::word;
    word(bytes, 0, 0x4250435f);
    word(bytes, 4, 1);
    word(bytes, 8, 1);
    word(bytes, 12, 16);
    word(bytes, 16, 3);
    word(bytes, 20, 1);
    word(bytes, 24 + 5 * 4, std::bit_cast<std::uint32_t>(1.0F));
    word(bytes, 24 + 7 * 4, std::bit_cast<std::uint32_t>(1.0F));
    word(bytes, 60, 0);
    word(bytes, 64, std::bit_cast<std::uint32_t>(1.0F));
    auto definition =
        bw::formats::ControlPointDefinition::parse(bytes, 2, domain);
    auto translated = kIdentityMatrix;
    translated[3] = {4, 5, 6, 1};
    std::array<Matrix, 1> frame{};
    evaluate_control_points(definition, std::array<Matrix, 1>{translated},
                            frame);
    require(frame[0] == translated,
            "Weighted control frame uses authored points and pose transform");
    auto broken = bytes;
    word(broken, 60, 2);
    rejects([&] {
      (void)bw::formats::ControlPointDefinition::parse(broken, 2, domain);
    });
    broken = bytes;
    word(broken, 12, 4);
    rejects([&] {
      (void)bw::formats::ControlPointDefinition::parse(broken, 2, domain);
    });
    const std::array<SecondaryNodeDefinition, 2> nodes{
        {{SecondaryNodeKind::frame, 0, 0, -1, 1, -1, -1, 1, 0},
         {SecondaryNodeKind::frame, -1, 1, 0, -1, -1, -1, 1, 0}}};
    const std::array<SecondaryCapsuleDefinition, 1> capsule{
        {{0, {.1F, -1, 0}, {0, 1, 0}, .2F, .25F}}};
    std::array<Matrix, 2> bones{kIdentityMatrix, kIdentityMatrix};
    bones[1][3][1] = -1;
    std::array<Matrix, 1> anchors{kIdentityMatrix};
    SecondaryMotion motion(nodes, capsule, 2, 1, domain);
    motion.reset(anchors, bones);
    const auto before = domain->snapshot();
    const auto allocations = backing->allocations, releases = backing->releases;
    motion.advance(1.0F / 60, 0, -2, anchors, bones, true);
    require(motion.overrides().size() == 2,
            "Only bound secondary bones are published");
    require(std::abs(bw::math::length(bw::math::subtract(
                         motion.positions()[1], motion.positions()[0])) -
                     1) < .00001F,
            "Contact correction retains authored segment length");
    require(motion.positions()[1][0] < -.1F,
            "Animated body capsule displaces intersecting chain");
    const auto retained = motion.overrides()[1].world;
    rejects([&] { motion.advance(-1, 0, -2, anchors, bones); });
    require(motion.overrides()[1].world == retained,
            "Rejected clock preserves published pose");
    for (int i = 0; i < 120; ++i) {
      motion.advance(1.0F / 60, static_cast<float>(i) / 60, -2, anchors, bones);
    }
    const auto after = domain->snapshot();
    require(before.live_bytes == after.live_bytes &&
                allocations == backing->allocations &&
                releases == backing->releases,
            "Secondary updates have fixed domain storage");
    motion.reset(anchors, bones);
    require(motion.overrides()[1].world == bones[1],
            "Reset restores authored initial bone frame");
  }
  require(domain->snapshot().live_bytes == 0,
          "Secondary owners release domain storage");
}
