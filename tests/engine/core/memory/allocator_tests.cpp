#include "binned/binned_allocator.hpp"
#include "binned/binned_thread_cache.hpp"
#include "bw/platform/memory/allocator_platform.hpp"
#include "bw/tasks/named_thread_dispatch.hpp"
#include "crash_allocator.hpp"

#include <array>
#include <atomic>
#include <barrier>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <latch>
#include <thread>

namespace {
void require(bool condition, const char* message) {
  if (!condition) {
    std::fprintf(stderr, "%s\n", message);
    std::exit(1);
  }
}

void allocation_contract(bw::memory::BinnedAllocator& allocator) {
  for (const std::size_t bytes :
       std::array<std::size_t, 9>{0, 1, 8, 17, 736, 4097, 32768, 131072, 131073}) {
    void* pointer = allocator.allocate(bytes);
    require(pointer != nullptr, "selected allocator returns storage for zero/small/large");
    std::size_t capacity = 0;
    require(allocator.allocation_size(pointer, capacity), "owned allocation has capacity");
    require(capacity == allocator.quantize(bytes), "capacity follows selected class/page policy");
    if (bytes != 0) {
      auto* data = static_cast<std::byte*>(pointer);
      data[0] = std::byte{0x31};
      data[bytes - 1] = std::byte{0x7a};
      require(data[bytes - 1] == std::byte{0x7a}, "requested extent is writable");
    }
    allocator.release(pointer);
  }
  allocator.release(nullptr);
  std::size_t untouched = 42;
  require(!allocator.allocation_size(nullptr, untouched), "null is not an allocation");
  for (const std::uint32_t alignment : {64U, 256U}) {
    void* pointer = allocator.allocate(17, alignment);
    require(reinterpret_cast<std::uintptr_t>(pointer) % alignment == 0,
            "promoted small allocation respects alignment");
    allocator.release(pointer);
  }
}

void reallocation_contract(bw::memory::BinnedAllocator& allocator) {
  void* pointer = allocator.allocate(30);
  std::memset(pointer, 0x5a, 30);
  void* same = allocator.reallocate(pointer, 31);
  require(same == pointer, "same size class reallocation remains in place");
  void* grown = allocator.reallocate(same, 200);
  const auto* bytes = static_cast<const unsigned char*>(grown);
  for (std::size_t index = 0; index < 30; ++index) {
    require(bytes[index] == 0x5a, "class-changing realloc preserves prior payload");
  }
  require(allocator.reallocate(grown, 0) == nullptr, "zero reallocation releases storage");

  const auto page = bw::platform::AllocatorVirtualMemoryBlock::commit_alignment();
  const std::size_t large_size = 131072 + page + 1;
  void* large = allocator.allocate(large_size);
  std::memset(large, 0x2a, large_size);
  void* larger = allocator.reallocate(large, large_size + 1);
  require(larger == large, "growth within same committed page remains in place");
  void* small = allocator.reallocate(larger, 100);
  for (std::size_t index = 0; index < 100; ++index) {
    require(static_cast<unsigned char*>(small)[index] == 0x2a,
            "large-to-small realloc retains common payload");
  }
  allocator.release(small);
}

void cache_lifetime_contract(bw::memory::BinnedAllocator& allocator) {
  allocator.setup_current_thread_cache();
  auto* cache = bw::memory::BinnedThreadCache::current_if_owned();
  require(cache != nullptr, "explicit setup creates owned cache");
  void* pointer = allocator.allocate(48);
  allocator.release(pointer);
  require(allocator.allocate(48) == pointer, "local cache reuses released slot");
  allocator.mark_current_thread_cache_unused();
  require(bw::memory::BinnedThreadCache::current_if_owned() == nullptr,
          "unused cache cannot serve recursive wait-related allocation");
  allocator.release(pointer); // Unused owner must use ordinary locked release.
  allocator.mark_current_thread_cache_used();
  require(bw::memory::BinnedThreadCache::current_if_owned() == cache,
          "resume reacquires the same cache");
  allocator.flush_current_thread_cache();
  allocator.clear_current_thread_cache();
  require(bw::memory::BinnedThreadCache::current_if_owned() == nullptr,
          "explicit clear releases TLS ownership");
}

void cross_thread_release_contract(bw::memory::BinnedAllocator& allocator) {
  std::array<void*, 192> pointers{};
  std::thread producer([&] {
    allocator.setup_current_thread_cache();
    for (auto& pointer : pointers) {
      pointer = allocator.allocate(80);
      std::memset(pointer, 0x6b, 80);
    }
    allocator.clear_current_thread_cache();
  });
  producer.join(); // Transfers client storage ownership; producer cache is gone.
  std::thread consumer([&] {
    allocator.setup_current_thread_cache();
    for (auto* pointer : pointers) {
      require(static_cast<unsigned char*>(pointer)[79] == 0x6b,
              "thread-cache teardown must not reclaim outstanding client storage");
      allocator.release(pointer);
    }
    allocator.flush_current_thread_cache();
    allocator.clear_current_thread_cache();
  });
  consumer.join();
}
void global_trim_contract(bw::memory::BinnedAllocator& allocator) {
  auto& dispatch = bw::tasks::NamedThreadDispatch::instance();
  allocator.setup_current_thread_cache();
  const bw::memory::BinnedSizeClasses sizes(bw::platform::allocator_constants().native_page_size);
  const auto pool = sizes.index_for(80);
  auto* local = bw::memory::BinnedThreadCache::current_if_owned();
  allocator.release(allocator.allocate(80));
  allocator.trim(false);
  void* retained = local->allocate(pool);
  require(retained != nullptr, "trim(false) preserves local cache");
  require(local->release(retained, pool, 80), "return observed cache slot");
  allocator.trim(true); // Source callback also works before TaskGraph startup.
  require(local->allocate(pool) == nullptr, "bootstrap trim flushes game-local bins");
  dispatch.startup();
  std::latch attached(2);
  std::latch pump(1);
  std::atomic<unsigned> flushed{0};
  auto owner = [&](bw::tasks::NamedThread role) {
    allocator.setup_current_thread_cache();
    auto* cache = bw::memory::BinnedThreadCache::current_if_owned();
    allocator.release(allocator.allocate(80));
    dispatch.attach_current_thread(role);
    attached.count_down();
    pump.wait();
    require(dispatch.process_until_idle() == 1, "named owner receives one trim callback");
    require(cache->allocate(pool) == nullptr, "named owner callback flushes its own bins");
    flushed.fetch_add(1);
    allocator.clear_current_thread_cache();
    dispatch.detach_current_thread();
  };
  std::thread rendering(owner, bw::tasks::NamedThread::rendering);
  std::thread rhi(owner, bw::tasks::NamedThread::rhi);
  attached.wait();
  allocator.trim(true);
  require(flushed.load() == 0, "desktop trim does not wait for named owners");
  pump.count_down();
  rendering.join();
  rhi.join();
  require(flushed.load() == 2, "both real named owners handled the trim epoch");
  dispatch.shutdown();
  allocator.clear_current_thread_cache();
}

void registry_growth_contract(bw::memory::BinnedAllocator& allocator) {
  constexpr std::size_t threads = 32;
  std::barrier ready(static_cast<std::ptrdiff_t>(threads));
  std::array<std::thread, threads> owners;
  for (auto& owner : owners) {
    owner = std::thread([&] {
      allocator.setup_current_thread_cache();
      void* payload = allocator.allocate(112);
      std::memset(payload, 0x73, 112);
      ready.arrive_and_wait(); // All32 registrations live, crossing several growths.
      require(static_cast<unsigned char*>(payload)[111] == 0x73,
              "registry growth preserves client storage and owners");
      allocator.release(payload);
      allocator.clear_current_thread_cache();
    });
  }
  for (auto& owner : owners) {
    owner.join();
  }
}
} // namespace

int main() try {
  // One process-lifetime instance follows the constructor's source singleton rule.
  // Integration now exercises the real facade/crash bootstrap, not a second
  // allocator beside TaskGraph's facade-owned instance.
  auto* allocator = static_cast<bw::memory::BinnedAllocator*>(&bw::memory::general_allocator());
  require(!bw::memory::CrashAllocator::is_active(),
          "bootstrap reserves crash memory without activating it");
  require(&bw::memory::general_allocator() == allocator, "facade retains one process owner");
  require(allocator->internally_thread_safe(), "selected allocator owns synchronization");
  allocation_contract(*allocator);
  reallocation_contract(*allocator);
  cache_lifetime_contract(*allocator);
  cross_thread_release_contract(*allocator);
  global_trim_contract(*allocator);
  registry_growth_contract(*allocator);
  std::puts(
      "allocator allocation, realloc, cache lifetime, cross-thread free, trim and registry pass");
} catch (const std::exception& error) {
  std::fprintf(stderr, "Test infrastructure failure: %s\n", error.what());
  return 1;
}
