#include "bw/memory/allocator.hpp"
#include "bw/memory/global_allocation.hpp"
#include "bw/platform/os/macos/platform_time.hpp"
#include "bw/runtime/engine_loop.hpp"

#include "bw/testing/harness.hpp"

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>

namespace {
using bw::testing::require;
// Ownership lookup proves the executable's definitions, not libc++, served
// the request. The byte and alignment policy is unit tested as new_request.
void verify(void* pointer, bw::memory::NewRequest request) {
  require(pointer != nullptr, "global new must return real storage");
  require(bw::memory::allocation_size(pointer) >= request.bytes,
          "global allocation must belong to selected engine heap");
  std::memset(pointer, 0x37, request.bytes);
}
void verify_aligned(void* pointer, bw::memory::NewRequest request) {
  verify(pointer, request);
  require(reinterpret_cast<std::uintptr_t>(pointer) % request.alignment == 0,
          "explicit alignment is forwarded to the allocator");
}
struct StaticAllocation {
  StaticAllocation() : pointer(::operator new(37)) {}
  // Runs after main, outside any scenario, so a failed requirement cannot
  // reach the harness: it is reported here and ends the process with 1.
  ~StaticAllocation() {
    try {
      verify(pointer, bw::memory::new_request(37));
    } catch (const std::exception& error) {
      std::fprintf(stderr, "%s\n", error.what());
      std::_Exit(1);
    }
    ::operator delete(pointer);
    std::puts("late static delete reached engine heap");
  }
  void* pointer;
};
StaticAllocation early;
// Real native lifecycle with no world service yet. This is a test Engine
// consumer, not a substitute allocator, platform clock or dispatch service.
class NativeEngine final : public bw::engine::Engine {
public:
  NativeEngine(bw::platform::PlatformTime& time, bw::platform::PlatformProcess& process)
      : Engine(time, process) {}
  void init(bw::engine::EngineLoop&) override {}
  void start() override {}
  void tick(float, bool) override {}
  void pre_exit() override {}
};

// The executable's contracts, one CTest entry.
void contracts() {
  verify(early.pointer, bw::memory::new_request(37));
  for (std::size_t bytes : std::array<std::size_t, 3>{0, 17, 131073}) {
    const auto request = bw::memory::new_request(bytes);
    void* scalar = ::operator new(bytes);
    verify(scalar, request);
    ::operator delete(scalar);
    void* array = ::operator new[](bytes);
    verify(array, request);
    ::operator delete[](array);
    scalar = ::operator new(bytes, std::nothrow);
    verify(scalar, request);
    ::operator delete(scalar, std::nothrow);
    array = ::operator new[](bytes, std::nothrow);
    verify(array, request);
    ::operator delete[](array, std::nothrow);
    constexpr auto aligned = std::align_val_t{256};
    const auto aligned_request = bw::memory::new_request(bytes, aligned);
    scalar = ::operator new(bytes, aligned);
    verify_aligned(scalar, aligned_request);
    ::operator delete(scalar, aligned);
    array = ::operator new[](bytes, aligned);
    verify_aligned(array, aligned_request);
    ::operator delete[](array, aligned);
    scalar = ::operator new(bytes, aligned, std::nothrow);
    verify_aligned(scalar, aligned_request);
    ::operator delete(scalar, aligned, std::nothrow);
    array = ::operator new[](bytes, aligned, std::nothrow);
    verify_aligned(array, aligned_request);
    ::operator delete[](array, aligned, std::nothrow);
  }
  // Sized forms complete the source delete families, including constructor-
  // failure nothrow overloads that libc++ need not otherwise supply.
  constexpr std::size_t size = 64;
  constexpr auto alignment = std::align_val_t{64};
  ::operator delete(::operator new(size), size);
  ::operator delete[](::operator new[](size), size);
  ::operator delete(::operator new(size, std::nothrow), size, std::nothrow);
  ::operator delete[](::operator new[](size, std::nothrow), size, std::nothrow);
  ::operator delete(::operator new(size, alignment), size, alignment);
  ::operator delete[](::operator new[](size, alignment), size, alignment);
  ::operator delete(::operator new(size, alignment, std::nothrow), size, alignment, std::nothrow);
  ::operator delete[](::operator new[](size, alignment, std::nothrow), size, alignment,
                      std::nothrow);
  ::operator delete(nullptr);
  ::operator delete[](nullptr);
  bw::platform::macos::PlatformTime time;
  bw::platform::macos::PlatformProcess process;
  NativeEngine engine(time, process);
  bw::engine::EngineLoop loop(engine);
  require(loop.pre_init() == 0, "global allocation executable completes native PreInit");
  require(loop.init() == 0, "global allocation executable completes native Init");
  loop.tick();
  loop.exit();
  // early's destructor runs after this completed EngineLoop Exit and main.
  // Its real heap lookup and global delete establish late-process ownership.
}

using Scenario = bw::testing::Scenario<void>;
constexpr std::array scenarios{Scenario{"global_allocation", contracts}};
} // namespace

int main(int argc, char** argv) { return bw::testing::run_scenario(argc, argv, scenarios); }
