// Prepared CPU shape deformation and named scalar clips, before GPU skinning.
// Model names and clip names borrow immutable definitions; those definitions
// outlive prepared objects, which outlive bound clips and instance output.
// Preparation validates extents, finite rest/key values and unique channel
// identities, converting half deltas once in source vertex/channel order.
// Instance evaluate validates all weights, computes pending positions, rejects
// nonfinite results before atomically publishing all buffers. No
// synchronization: one exclusive owner; caller serializes output upload and
// device completion. Setup O(vertices*channels + keys), owned typed arrays;
// evaluate same arithmetic cost, no allocations. Scalar clips sample explicit
// frame/weight into supplied additive weights with no hidden clocks or game
// policy. Core I.11 (#ri-raw), SL.con.3 (#rsl-bounds) preserve
// lifetimes/bounds; Performance CDSA.32
// (guidelines/cpu-dsa/CDSA.32-transform-static-data-into-the-consumer-layout.md)
// and MEM.9 (guidelines/memory/MEM.9-embedded-allocate-at-init.md) keep
// preparation separate from allocation-free updates. Both phases consulted. See
// facial-animation.md for original algorithm and pinned Unreal comparison.
#pragma once
#include "bw/content/formats/model.hpp"
#include "bw/math/vector.hpp"
namespace bw::animation {
class PreparedModelShapes {
 public:
  PreparedModelShapes(const formats::ModelDefinition& model,
                      const std::shared_ptr<memory::Domain>& domain);
  struct Buffer {
    explicit Buffer(const std::shared_ptr<memory::Domain>& domain)
        : rest(memory::Allocator<math::Vector3<float>>(domain)),
          deltas(memory::Allocator<math::Vector3<float>>(domain)),
          channels(memory::Allocator<std::size_t>(domain)) {}
    memory::Vector<math::Vector3<float>> rest, deltas;
    memory::Vector<std::size_t> channels;
  };
  const auto& buffers() const { return buffers_; }
  const auto& names() const { return names_; }

 private:
  memory::Vector<Buffer> buffers_;
  memory::Vector<std::string_view> names_;
};
class ShapePose {
 public:
  ShapePose(const PreparedModelShapes& prepared,
            const std::shared_ptr<memory::Domain>& domain);
  void evaluate(std::span<const float> weights);
  std::span<const math::Vector3<float>> positions(std::size_t buffer) const;

 private:
  const PreparedModelShapes* prepared_;
  memory::Vector<math::Vector3<float>> positions_, pending_;
  memory::Vector<std::size_t> offsets_;
};
class PreparedShapeClip {
 public:
  PreparedShapeClip(const formats::ShapeAnimationDefinition& definition,
                    const PreparedModelShapes& model,
                    const std::shared_ptr<memory::Domain>& domain);
  float duration_frames() const { return duration_; }
  float frames_per_second() const { return rate_; }
  bool repeating() const { return repeating_; }
  // Inputs finite, frame>=0, output matches model channels. Adds samples in
  // authored channel order. Caller supplies fresh weights or deliberate sums.
  void accumulate(float frame, float weight, std::span<float> output) const;

 private:
  struct Channel {
    std::size_t target{}, first{}, count{};
    float end{};
  };
  memory::Vector<Channel> channels_;
  memory::Vector<float> times_, values_;
  std::size_t output_count_{};
  float duration_{}, rate_{};
  bool repeating_{};
};
}  // namespace bw::animation
