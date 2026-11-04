// Integration test: public contracts of bw/memory/allocator.hpp through the
// Allocator interface and the facade. Needs threads.
#include "bw/memory/allocator.hpp"
#include "bw/platform/memory/allocator_platform.hpp"

#include "bw/testing/harness.hpp"

#include <array>
#include <barrier>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <string_view>
#include <thread>

namespace {
using bw::testing::require;

// The scenario's name, shared by the table and the worker-thread helper.
constexpr std::string_view kScenario = "allocator";

// A requirement checked on a worker thread, where a throw cannot reach the
// harness: it prints "<scenario>: <message>" and ends the process with 1.
// The line goes out in one write so failing threads do not interleave.
void require_on_thread(bool condition, std::string_view message) {
  if (!condition) {
    std::string line(kScenario);
    line.append(": ").append(message).append("\n");
    std::fwrite(line.data(), 1, line.size(), stderr);
    std::_Exit(1);
  }
}

void allocation_contract(bw::memory::Allocator& allocator) {
  for (const std::size_t bytes :
       std::array<std::size_t, 9>{0, 1, 8, 17, 736, 4097, 32768, 131072, 131073}) {
    void* pointer = allocator.allocate(bytes);
    require(pointer != nullptr, "allocate returns storage for zero, small and large sizes");
    std::size_t capacity = 0;
    require(allocator.allocation_size(pointer, capacity), "an owned allocation reports a size");
    require(capacity == allocator.quantize(bytes), "reported size equals the quantized request");
    allocator.release(pointer);
  }
  allocator.release(nullptr);
  std::size_t ignored = 0;
  require(!allocator.allocation_size(nullptr, ignored), "null is not an allocation");
  for (const std::uint32_t alignment : {64U, 256U}) {
    void* pointer = allocator.allocate(17, alignment);
    require(reinterpret_cast<std::uintptr_t>(pointer) % alignment == 0,
            "requested alignment is honored");
    allocator.release(pointer);
  }
}

void reallocation_contract(bw::memory::Allocator& allocator) {
  void* pointer = allocator.allocate(30);
  std::memset(pointer, 0x5a, 30);
  void* grown = allocator.reallocate(pointer, 200);
  const auto* bytes = static_cast<const unsigned char*>(grown);
  for (std::size_t index = 0; index < 30; ++index) {
    require(bytes[index] == 0x5a, "growth preserves the prior payload");
  }
  require(allocator.reallocate(grown, 0) == nullptr, "zero-size reallocation releases storage");
  const auto page = bw::platform::AllocatorVirtualMemoryBlock::commit_alignment();
  const std::size_t large_size = 131072 + page + 1;
  void* large = allocator.allocate(large_size);
  std::memset(large, 0x2a, large_size);
  void* small = allocator.reallocate(large, 100);
  for (std::size_t index = 0; index < 100; ++index) {
    require(static_cast<unsigned char*>(small)[index] == 0x2a,
            "shrinking preserves the common payload");
  }
  allocator.release(small);
}

void cross_thread_release_contract(bw::memory::Allocator& allocator) {
  std::array<void*, 192> pointers{};
  std::thread producer([&] {
    allocator.setup_current_thread_cache();
    for (std::size_t index = 0; index < pointers.size(); ++index) {
      pointers[index] = allocator.allocate(80);
      std::memset(pointers[index], static_cast<int>(index), 80);
    }
    allocator.clear_current_thread_cache();
  });
  producer.join();
  std::thread consumer([&] {
    allocator.setup_current_thread_cache();
    std::array<unsigned char, 80> expected{};
    for (std::size_t index = 0; index < pointers.size(); ++index) {
      expected.fill(static_cast<unsigned char>(index));
      require_on_thread(
          std::memcmp(pointers[index], expected.data(), expected.size()) == 0,
          "each allocation keeps its own payload after the allocating thread's cache clears");
      allocator.release(pointers[index]);
    }
    allocator.clear_current_thread_cache();
  });
  consumer.join();
}

void many_threads_contract(bw::memory::Allocator& allocator) {
  constexpr std::size_t threads = 32;
  std::barrier ready(static_cast<std::ptrdiff_t>(threads));
  std::array<std::thread, threads> owners;
  for (std::size_t index = 0; index < owners.size(); ++index) {
    owners[index] = std::thread([&allocator, &ready, index] {
      allocator.setup_current_thread_cache();
      void* payload = allocator.allocate(112);
      const auto mark = static_cast<unsigned char>(0x40 + index);
      std::memset(payload, mark, 112);
      ready.arrive_and_wait();
      std::array<unsigned char, 112> expected{};
      expected.fill(mark);
      require_on_thread(std::memcmp(payload, expected.data(), expected.size()) == 0,
                        "concurrent owners hold distinct storage");
      allocator.release(payload);
      allocator.clear_current_thread_cache();
    });
  }
  for (auto& owner : owners) {
    owner.join();
  }
}

void facade_contract() {
  void* pointer = bw::memory::allocate(27);
  require(pointer != nullptr && bw::memory::allocation_size(pointer) >= 27,
          "facade allocation belongs to the general allocator");
  pointer = bw::memory::reallocate(pointer, 300);
  require(bw::memory::allocation_size(pointer) >= 300, "facade reallocation grows");
  bw::memory::release(pointer);
  bw::memory::setup_current_thread_cache();
  bw::memory::mark_current_thread_cache_unused();
  bw::memory::mark_current_thread_cache_used();
  bw::memory::clear_current_thread_cache();
}

// The executable's contracts, one CTest entry.
void contracts() {
  auto& allocator = bw::memory::general_allocator();
  require(&bw::memory::general_allocator() == &allocator, "one general allocator per process");
  require(allocator.internally_thread_safe(), "the general allocator is thread safe");
  allocation_contract(allocator);
  reallocation_contract(allocator);
  cross_thread_release_contract(allocator);
  many_threads_contract(allocator);
  facade_contract();
}

using Scenario = bw::testing::Scenario<void>;
constexpr std::array scenarios{Scenario{kScenario, contracts}};
} // namespace

int main(int argc, char** argv) { return bw::testing::run_scenario(argc, argv, scenarios); }
