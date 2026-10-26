// Independent in-memory shape fixtures exercise original layout/arithmetic,
// scalar binding, transactional failure and allocation-free retained outputs.
// Core SL.con.3 and Performance MEM.9/CDSA.32 apply as in shape_pose.hpp.
#include "bw/animation/shape_pose.hpp"

#include <algorithm>
#include <limits>
#include <memory_resource>

#include "../../../fixtures/shape_fixture.hpp"
#include "bw/content/formats/model_shapes.hpp"
#include "bw/math/half.hpp"
namespace {
void require(bool value, const char* message) {
  if (!value) throw std::runtime_error(message);
}
class Backing final : public bw::memory::CpuBacking {
 public:
  std::size_t calls{};
  void* allocate(std::size_t bytes, std::size_t alignment) noexcept override {
    try {
      ++calls;
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
template <class Function>
void rejects(Function function) {
  bool caught = false;
  try {
    function();
  } catch (const std::exception&) {
    caught = true;
  }
  require(caught, "Malformed shape request must reject");
}
}  // namespace
void shape_pose_tests() {
  const auto backing = std::make_shared<Backing>();
  const auto domain = bw::memory::Domain::create(backing);
  {
    const auto bytes = shape_fixture::offsets();
    const auto decoded = bw::formats::ModelShapeOffsets::parse(bytes, domain);
    require(decoded.vertex_count() == 3 && decoded.names().front() == "smile" &&
                decoded.deltas().size() == 24,
            "Shape offset layout");
    auto malformed = bytes;
    model_fixture::word(malformed, 76, 149);
    rejects([&] { bw::formats::ModelShapeOffsets::parse(malformed, domain); });
    const auto model = bw::formats::ModelDefinition::parse(
        bw::resources::ResourceArchive::parse(
            model_fixture::archive({.shape_offsets = bytes, .shape = true})),
        domain);
    const bw::animation::PreparedModelShapes prepared(model, domain);
    bw::animation::ShapePose pose(prepared, domain);
    const auto clip_bytes = shape_fixture::shape_clip();
    const auto definition =
        bw::formats::ShapeAnimationDefinition::parse(clip_bytes, domain);
    const bw::animation::PreparedShapeClip clip(definition, prepared, domain);
    const auto allocations = backing->calls;
    std::array<float, 1> weights{};
    clip.accumulate(.5F, 2, weights);
    require(weights[0] == 1 && clip.duration_frames() == 1 &&
                clip.frames_per_second() == 30 && clip.repeating(),
            "Shape clock and source interpolation");
    pose.evaluate(weights);
    require(pose.positions(0)[0] == bw::math::Vector3<float>{0, -.5F, .25F},
            "Rest plus weighted XYZ, fourth half ignored");
    const auto previous = pose.positions(0)[0];
    weights[0] = std::numeric_limits<float>::infinity();
    rejects([&] { pose.evaluate(weights); });
    require(pose.positions(0)[0] == previous,
            "Bad weights preserve published positions");
    weights[0] = 0;
    pose.evaluate(weights);
    require(pose.positions(0)[0] == bw::math::Vector3<float>{-1, -1, .5F},
            "Zero weights restore original rest positions");
    require(backing->calls == allocations,
            "Shape sampling/deformation do not allocate");
    require(bw::math::finite_half_to_float(0x7c00) == 65536 &&
                std::signbit(bw::math::finite_half_to_float(0x8000)),
            "Finite half policy and signed zero");
  }
  require(domain->snapshot().live_bytes == 0, "Shape resources retire");
}
