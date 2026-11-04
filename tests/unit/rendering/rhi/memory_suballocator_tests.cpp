// Unit test: public contracts of bw/rendering/rhi/memory_suballocator.hpp,
// one scenario per CTest entry. Each scenario names the header declaration it
// asserts.
#include "bw/rendering/rhi/memory_suballocator.hpp"

#include "bw/testing/harness.hpp"
#include "unwrap.hpp"

#include <array>
#include <cstdint>

namespace {
using bw::rhi::Allocation;
using bw::rhi::BlockSuballocator;
using bw::testing::require;
using Scenario = bw::testing::Scenario<void>;
using bw_rhi_test::unwrap;

// (memory_suballocator.hpp, BlockSuballocator::allocate) and
// (memory_suballocator.hpp, BlockSuballocator::free): allocate returns the first free
// block in address order that fits; free merges with free neighbors, so
// after every allocation is freed one block of capacity bytes remains.
void first_fit_split_merge() {
  BlockSuballocator allocator(1024);
  const Allocation a = unwrap(allocator.allocate(100, 1), "a");
  const Allocation b = unwrap(allocator.allocate(200, 1), "b");
  const Allocation c = unwrap(allocator.allocate(100, 1), "c");
  require(a == Allocation{0, 100}, "the first allocation at 0");
  require(b == Allocation{100, 200}, "the next follows it");
  require(c == Allocation{300, 100}, "and the next");
  require(allocator.free_bytes() == 624U, "624 bytes remain");

  allocator.free(b.offset);
  require(allocator.largest_free_block() == 624U, "the hole is smaller than the tail");
  const Allocation d = unwrap(allocator.allocate(150, 1), "d");
  require(d.offset == 100U, "first fit takes the hole, not the tail");
  const Allocation e = unwrap(allocator.allocate(60, 1), "e");
  require(e.offset == 400U, "a block too large for the hole's rest goes to the tail");
  const Allocation f = unwrap(allocator.allocate(50, 1), "f");
  require(f.offset == 250U, "the hole's remainder was split off");

  for (const std::uint64_t offset : {a.offset, f.offset, c.offset, d.offset, e.offset}) {
    allocator.free(offset);
  }
  require(allocator.empty() && allocator.free_bytes() == 1024U, "everything freed");
  require(allocator.block_count() == 1U && allocator.largest_free_block() == 1024U,
          "one block of capacity bytes again");
}

// (memory_suballocator.hpp, BlockSuballocator::allocate): the offset is a
// multiple of the alignment; the padding before it belongs to the allocation
// and returns with it.
void alignment_padding_belongs_to_allocation() {
  BlockSuballocator allocator(512);
  static_cast<void>(unwrap(allocator.allocate(10, 1), "ten bytes"));
  const Allocation aligned = unwrap(allocator.allocate(64, 256), "aligned");
  require(aligned.offset == 256U && aligned.size == 64U, "aligned to 256");
  require(allocator.free_bytes() == 512U - 320U, "the padding from 10 to 256 is used");
  const Allocation between = unwrap(allocator.allocate(16, 1), "between");
  require(between.offset == 320U, "the padding is not available to another allocation");
  allocator.free(aligned.offset);
  require(allocator.free_bytes() == 512U - 10U - 16U, "the padding returns with the allocation");
  const Allocation again = unwrap(allocator.allocate(246, 1), "again");
  require(again.offset == 10U, "the padded range is free again");
}

// (memory_suballocator.hpp, BlockSuballocator::allocate): when no free block
// fits, allocate returns no value and nothing grows.
void exhaustion_is_no_value() {
  BlockSuballocator allocator(256);
  require(allocator.allocate(256, 1).has_value(), "the whole range fits");
  require(!allocator.allocate(1, 1).has_value(), "nothing fits a full allocator");
  require(allocator.capacity() == 256U && allocator.free_bytes() == 0U, "nothing grew");
  BlockSuballocator fragmented(300);
  const Allocation low = unwrap(fragmented.allocate(100, 1), "low");
  static_cast<void>(unwrap(fragmented.allocate(100, 1), "mid"));
  fragmented.free(low.offset);
  require(!fragmented.allocate(150, 1).has_value(), "200 free bytes in two blocks do not hold 150");
  BlockSuballocator offset(300);
  static_cast<void>(unwrap(offset.allocate(10, 1), "ten bytes"));
  require(!offset.allocate(64, 256).has_value(),
          "256 + 64 runs past the end: no block holds the aligned allocation");
}

constexpr std::array scenarios{
    Scenario{"first_fit_split_merge", first_fit_split_merge},
    Scenario{"alignment_padding_belongs_to_allocation", alignment_padding_belongs_to_allocation},
    Scenario{"exhaustion_is_no_value", exhaustion_is_no_value},
};
} // namespace

int main(int argc, char** argv) { return bw::testing::run_scenario(argc, argv, scenarios); }
