// Unit test: descriptor contracts of bw/platform/os/macos/platform_memory.hpp
// that hold without a mapping: the default descriptor, extent arithmetic and
// the commit/decommit range precondition.
#include "bw/platform/os/macos/platform_memory.hpp"

#include "bw/testing/harness.hpp"

#include <array>

namespace {
using bw::platform::macos::VirtualMemoryBlock;

using bw::testing::require;

void default_descriptor() {
  VirtualMemoryBlock block;
  require(block.virtual_pointer() == nullptr, "default block address");
  require(block.actual_size_in_pages() == 0, "default block page count");
  require(block.actual_size() == 0, "default block extent");
  block.free_virtual();
  block.free_virtual();
  require(block.virtual_pointer() == nullptr, "releasing an empty block is a no-op");
}

void extent_and_range() {
  const auto page = VirtualMemoryBlock::commit_alignment();
  const VirtualMemoryBlock block(nullptr, 3);
  require(block.actual_size() == 3 * page, "extent is pages times commit alignment");
  require(block.contains_page_range(0, page), "first page");
  require(block.contains_page_range(page, 2 * page), "tail pages");
  require(block.contains_page_range(0, 3 * page), "whole extent");
  require(block.contains_page_range(page, 0), "empty range on a boundary");
  require(!block.contains_page_range(0, 4 * page), "range past the extent");
  require(!block.contains_page_range(3 * page, page), "offset at the extent");
  require(!block.contains_page_range(1, page), "misaligned offset");
  require(!block.contains_page_range(0, page + 1), "misaligned size");
}

// The executable's contracts, one CTest entry.
void contracts() {
  default_descriptor();
  extent_and_range();
}

using Scenario = bw::testing::Scenario<void>;
constexpr std::array scenarios{Scenario{"virtual_memory_block", contracts}};
} // namespace

int main(int argc, char** argv) { return bw::testing::run_scenario(argc, argv, scenarios); }
