// Original named vertex-offset view, independent of deformation or devices.
// Parse validates header/version, bounded names, and vertex-major half4
// storage; failures throw resources::Error before publication. Names/deltas
// borrow the immutable input bytes, which outlive this object and prepared
// consumers. Metadata uses the injected domain. O(channels), no bulk vertex
// copy; no clocks or synchronization. Core I.11 (#ri-raw) separates borrowing
// from ownership; SL.con.3 (#rsl-bounds) validates extents. Performance CDSA.32
// (guidelines/cpu-dsa/
// CDSA.32-transform-static-data-into-the-consumer-layout.md) reserves
// conversion for preparation. Both phases consulted. Source:
// facial-animation.md.
#pragma once
#include "bw/formats/common.hpp"
#include "bw/memory/allocator.hpp"
namespace bw::formats {
class ModelShapeOffsets {
 public:
  static ModelShapeOffsets parse(std::span<const std::uint8_t> bytes,
                                 const std::shared_ptr<memory::Domain>& domain);
  std::uint32_t vertex_count() const { return vertex_count_; }
  const auto& names() const { return names_; }
  std::span<const std::uint8_t> deltas() const { return deltas_; }

 private:
  explicit ModelShapeOffsets(const std::shared_ptr<memory::Domain>& domain)
      : names_(memory::Allocator<std::string_view>(domain)) {}
  std::uint32_t vertex_count_{};
  memory::Vector<std::string_view> names_;
  std::span<const std::uint8_t> deltas_;
};
}  // namespace bw::formats
