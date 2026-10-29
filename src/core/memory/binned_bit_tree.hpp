#ifndef BW_CORE_MEMORY_BINNED_BIT_TREE_HPP
#define BW_CORE_MEMORY_BINNED_BIT_TREE_HPP

#include <cstddef>
#include <cstdint>

// Core CP.2: backing words are caller-owned and require external synchronization.
// Performance MEM.2: block availability metadata is separate from client storage.
namespace bw::memory {

// Unreal f45729c0b6f097038e72b52d366a62baeca3275b:
// HAL/MallocBinnedCommon.h:119-163; .cpp:168-459. Non-owning 64-way bit tree.
class BinnedBitTree final {
public:
  BinnedBitTree();
  // .h:134-153: size all summary rows, removing unused trailing leaf words.
  [[nodiscard]] static std::uint32_t memory_requirement(std::uint32_t capacity);
  // .cpp:168-227: bind aligned backing; mark padding occupied even when clear.
  void initialize(std::uint32_t capacity, void* backing, std::uint32_t bytes, bool occupied);
  // .cpp:229-276: lowest available leaf or UINT32_MAX; propagate full rows.
  [[nodiscard]] std::uint32_t allocate();
  // .cpp:289-317: index must be in range and currently free.
  void allocate(std::uint32_t index);
  // .cpp:278-287: query a valid leaf without modifying summaries.
  [[nodiscard]] bool is_allocated(std::uint32_t index) const;
  // .cpp:319-345,347-413: nonmutating next-free search, with optional lower bound.
  [[nodiscard]] std::uint32_t next_available() const;
  [[nodiscard]] std::uint32_t next_available(std::uint32_t start) const;
  // .cpp:415-443: occupied leaf becomes free; clear formerly full parent bits.
  void release(std::uint32_t index);
  // .cpp:445-459: count set leaves in [0, end), excluding padding.
  [[nodiscard]] std::uint32_t count_occupied(std::uint32_t end) const;

private:
  std::uint64_t* bits_{};
  std::uint32_t capacity_{};
  std::uint32_t desired_capacity_{};
  std::uint32_t rows_{};
  std::uint32_t last_row_offset_{};
  std::uint32_t allocation_bytes_{};
};
} // namespace bw::memory

#endif
