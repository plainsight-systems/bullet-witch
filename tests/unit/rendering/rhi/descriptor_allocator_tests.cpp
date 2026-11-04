// Unit test: public contracts of bw/rendering/rhi/descriptor_allocator.hpp,
// one scenario per CTest entry. Each scenario names the header declaration it
// asserts.
#include "bw/rendering/rhi/descriptor_allocator.hpp"

#include "bw/testing/harness.hpp"
#include "unwrap.hpp"

#include <array>
#include <cstdint>
#include <optional>

namespace {
using bw::rhi::DescriptorAllocators;
using bw::rhi::DescriptorCapacities;
using bw::rhi::DescriptorIndexAllocator;
using bw::rhi::DescriptorPool;
using bw::shaders::DescriptorArray;
using bw::testing::require;
using Scenario = bw::testing::Scenario<void>;

// (descriptor_allocator.hpp, DescriptorIndexAllocator): allocate returns the lowest
// free index; a freed index is reused by a later allocate, lowest first;
// allocated() + free_count() == capacity().
void allocate_free_reuse_lowest_first() {
  DescriptorIndexAllocator allocator(8);
  for (std::uint32_t expected = 0; expected < 5; ++expected) {
    require(allocator.allocate() == std::optional<std::uint32_t>{expected},
            "a fresh allocator hands out indices in ascending order");
  }
  require(allocator.allocated() == 5U && allocator.free_count() == 3U, "five of eight in use");
  allocator.free(3);
  allocator.free(1);
  require(allocator.allocated() + allocator.free_count() == allocator.capacity(),
          "allocated plus free is the capacity");
  require(allocator.allocate() == std::optional<std::uint32_t>{1}, "the lowest freed index first");
  require(allocator.allocate() == std::optional<std::uint32_t>{3}, "then the next freed index");
  require(allocator.allocate() == std::optional<std::uint32_t>{5}, "then the untouched range");
  allocator.free(2);
  allocator.free(4);
  allocator.free(3);
  require(allocator.allocate() == std::optional<std::uint32_t>{2},
          "neighbors freed in any order give back the lowest");
  require(allocator.allocate() == std::optional<std::uint32_t>{3}, "and the next");
  require(allocator.allocate() == std::optional<std::uint32_t>{4}, "and the next");
}

// (descriptor_allocator.hpp, DescriptorIndexAllocator::allocate): with no free index, allocate
// returns no value and nothing grows; a free makes one available again.
void exhaustion_is_no_value() {
  DescriptorIndexAllocator allocator(2);
  require(allocator.allocate().has_value() && allocator.allocate().has_value(), "two fit");
  require(!allocator.allocate().has_value(), "a third is no value");
  require(allocator.capacity() == 2U && allocator.free_count() == 0U, "nothing grew");
  allocator.free(0);
  require(allocator.allocate() == std::optional<std::uint32_t>{0}, "a freed index returns");
  require(!DescriptorIndexAllocator(0).allocate().has_value(), "capacity zero has none");
}

// (descriptor_allocator.hpp, pool_of) and
// (descriptor_allocator.hpp, DescriptorAllocators): every read
// array draws from the read pool, the sampler array from the sampler pool,
// both storage arrays from the storage pool; a slot carries its array;
// exhausting one pool leaves the others unchanged.
void pools_isolated_by_array() {
  require(bw::rhi::pool_of(DescriptorArray::Texture2D) == DescriptorPool::Read, "2D reads");
  require(bw::rhi::pool_of(DescriptorArray::TextureCube) == DescriptorPool::Read, "cube reads");
  require(bw::rhi::pool_of(DescriptorArray::Texture2DArray) == DescriptorPool::Read, "arrays");
  require(bw::rhi::pool_of(DescriptorArray::Texture3D) == DescriptorPool::Read, "volumes");
  require(bw::rhi::pool_of(DescriptorArray::Buffer) == DescriptorPool::Read, "buffers");
  require(bw::rhi::pool_of(DescriptorArray::Sampler) == DescriptorPool::Sampler, "samplers");
  require(bw::rhi::pool_of(DescriptorArray::StorageTexture2D) == DescriptorPool::Storage,
          "storage textures");
  require(bw::rhi::pool_of(DescriptorArray::StorageBuffer) == DescriptorPool::Storage,
          "storage buffers");

  DescriptorAllocators allocators(DescriptorCapacities{2, 1, bw::shaders::kStorageDescriptors});
  const bw::rhi::DescriptorSlot texture =
      bw_rhi_test::unwrap(allocators.allocate(DescriptorArray::Texture2D), "a texture slot");
  const bw::rhi::DescriptorSlot buffer =
      bw_rhi_test::unwrap(allocators.allocate(DescriptorArray::Buffer), "a buffer slot");
  require(texture.index == 0U && texture.array == DescriptorArray::Texture2D,
          "a slot carries its index and array");
  require(buffer.index == 1U, "one index space across read arrays");
  require(!allocators.allocate(DescriptorArray::TextureCube).has_value(), "read pool exhausted");
  require(
      bw_rhi_test::unwrap(allocators.allocate(DescriptorArray::Sampler), "a sampler slot").index ==
          0U,
      "the sampler pool is unaffected");
  require(bw_rhi_test::unwrap(allocators.allocate(DescriptorArray::StorageBuffer), "a storage slot")
                  .index == 0U,
          "the storage pool is unaffected");
  require(allocators.pool(DescriptorPool::Storage).allocated() == 1U &&
              allocators.pool(DescriptorPool::Sampler).allocated() == 1U &&
              allocators.pool(DescriptorPool::Read).allocated() == 2U,
          "each pool counts its own slots");
  allocators.free(texture);
  require(allocators.pool(DescriptorPool::Read).allocated() == 1U,
          "free returns the slot to its own pool");
  require(
      bw_rhi_test::unwrap(allocators.allocate(DescriptorArray::Texture3D), "a volume slot").index ==
          0U,
      "the freed read index is reused by another read array");

  require(bw::rhi::descriptor_capacities_valid(DescriptorCapacities{
              1, bw::shaders::kMaxSamplerDescriptors, bw::shaders::kStorageDescriptors}),
          "the limits themselves are valid");
  require(!bw::rhi::descriptor_capacities_valid(
              DescriptorCapacities{0, 1, bw::shaders::kStorageDescriptors}),
          "read capacity at least 1");
  require(!bw::rhi::descriptor_capacities_valid(DescriptorCapacities{
              1, bw::shaders::kMaxSamplerDescriptors + 1U, bw::shaders::kStorageDescriptors}),
          "sampler capacity within the maximum");
  require(!bw::rhi::descriptor_capacities_valid(
              DescriptorCapacities{1, 1, bw::shaders::kStorageDescriptors - 1U}),
          "storage capacity exactly the root signature's");
}

constexpr std::array scenarios{
    Scenario{"allocate_free_reuse_lowest_first", allocate_free_reuse_lowest_first},
    Scenario{"exhaustion_is_no_value", exhaustion_is_no_value},
    Scenario{"pools_isolated_by_array", pools_isolated_by_array},
};
} // namespace

int main(int argc, char** argv) { return bw::testing::run_scenario(argc, argv, scenarios); }
