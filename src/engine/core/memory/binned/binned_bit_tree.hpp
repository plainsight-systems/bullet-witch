#ifndef BW_CORE_MEMORY_BINNED_BIT_TREE_HPP
#define BW_CORE_MEMORY_BINNED_BIT_TREE_HPP

#include <cstddef>
#include <cstdint>

// Core CP.2: backing words are caller-owned and require external synchronization.
// Performance MEM.2: block availability metadata is separate from client storage.
namespace bw::memory {

// Non-owning 64-way bit tree.
class BinnedBitTree final {
public:
  BinnedBitTree();
  // Size all summary rows, removing unused trailing leaf words.
  [[nodiscard]] static std::uint32_t memory_requirement(std::uint32_t capacity);
  // Bind aligned backing; mark padding occupied even when clear.
  void initialize(std::uint32_t capacity, void* backing, std::uint32_t bytes, bool occupied);
  // Lowest available leaf or UINT32_MAX; propagate full rows.
  [[nodiscard]] std::uint32_t allocate();
  // Index must be in range and currently free.
  void allocate(std::uint32_t index);
  // Query a valid leaf without modifying summaries.
  [[nodiscard]] bool is_allocated(std::uint32_t index) const;
  // Nonmutating next-free search, with optional lower bound.
  [[nodiscard]] std::uint32_t next_available() const;
  [[nodiscard]] std::uint32_t next_available(std::uint32_t start) const;
  // Occupied leaf becomes free; clear formerly full parent bits.
  void release(std::uint32_t index);
  // Count set leaves in [0, end), excluding padding.
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
