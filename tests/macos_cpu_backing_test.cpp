// Native in-process provider contract checks; no files, subprocess or UI.
// Exercise alignment, writable independent allocations and cross-thread release
// using one explicitly selected Mac provider. Rejection is deterministic
// invalid input, not a claim that native memory-pressure recovery has been
// tested. Bounded allocations; native allocator latency is intentionally
// unmeasured. Design MCP R.1 (#rr-raii), implementation E.6 (#re-raii) keep
// test storage guarded through assertions. Performance MEM.6
// (guidelines/memory/ MEM.6-inject-allocators-behind-one-interface.md), read in
// both phases, tests the public injected capability. Parent domain tests cover
// owner prefixes, budgets and zeroing.
#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <stdexcept>
#include <thread>

#include "platform/macos/cpu_backing.hpp"

namespace {
void require(bool condition, const char* message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

std::shared_ptr<void> acquire(
    const std::shared_ptr<bw::memory::CpuBacking>& backing, std::size_t bytes,
    std::size_t alignment) {
  return {backing->allocate(bytes, alignment),
          [backing, bytes, alignment](void* pointer) {
            backing->deallocate(pointer, bytes, alignment);
          }};
}

void aligned_independent_storage() {
  const auto backing = bw::platform::macos::make_cpu_backing();
  constexpr std::size_t bytes = 37;
  constexpr std::array<std::size_t, 3> alignments{alignof(std::max_align_t), 64,
                                                  4096};
  for (const auto alignment : alignments) {
    auto first = acquire(backing, bytes, alignment);
    const auto second = acquire(backing, bytes, alignment);
    require(first && second, "Native CPU allocations succeed");
    require(std::bit_cast<std::uintptr_t>(first.get()) % alignment == 0 &&
                std::bit_cast<std::uintptr_t>(second.get()) % alignment == 0,
            "Native allocations preserve requested alignment");
    require(first.get() != second.get(), "Live allocations do not overlap");
    auto view = std::span(static_cast<std::byte*>(second.get()), bytes);
    std::ranges::fill(view, std::byte{0x5a});
    // Ownership may move to another thread; joining retires the first block.
    std::jthread release(
        [owned = std::move(first)]() mutable { owned.reset(); });
    release.join();
    require(std::ranges::all_of(
                view, [](std::byte value) { return value == std::byte{0x5a}; }),
            "Independent allocation survives another block's retirement");
    auto replacement = acquire(backing, bytes, alignment);
    require(replacement != nullptr, "Provider remains usable after release");
  }
}

void invalid_requests() {
  const auto backing = bw::platform::macos::make_cpu_backing();
  require(backing->allocate(0, alignof(std::max_align_t)) == nullptr,
          "Zero-sized native request is rejected");
  require(backing->allocate(37, 0) == nullptr &&
              backing->allocate(37, 3) == nullptr &&
              backing->allocate(37, alignof(std::max_align_t) / 2) == nullptr,
          "Invalid or below-contract alignment is rejected");
  backing->deallocate(nullptr, 0, alignof(std::max_align_t));
}
}  // namespace

void macos_cpu_backing_tests() {
  aligned_independent_storage();
  invalid_requests();
}
