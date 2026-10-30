#include "bw/memory/allocator.hpp"
#include "bw/memory/global_allocation.hpp"
#include "bw/platform/os/macos/platform_time.hpp"
#include "bw/runtime/engine_loop.hpp"

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {
void require(bool condition, const char* message) {
  if (!condition) {
    std::fprintf(stderr, "%s\n", message);
    std::exit(1);
  }
}
struct AllocationExpectation {
  std::size_t bytes;
  std::size_t alignment;
};
void verify(void* pointer, AllocationExpectation expected) {
  const auto [bytes, alignment] = expected;
  require(pointer != nullptr, "global new must return real storage");
  require(reinterpret_cast<std::uintptr_t>(pointer) % alignment == 0,
          "global new must preserve alignment");
  // This queries the REAL allocator's ownership metadata; a libc++ allocation
  // cannot pass by merely returning writable memory from another heap.
  require(bw::memory::allocation_size(pointer) >= (bytes == 0 ? 1 : bytes),
          "global allocation must belong to selected engine heap");
  std::memset(pointer, 0x37, bytes == 0 ? 1 : bytes);
}
struct StaticAllocation {
  StaticAllocation() : pointer(::operator new(37)) {}
  ~StaticAllocation() {
    verify(pointer, {37, __STDCPP_DEFAULT_NEW_ALIGNMENT__});
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
} // namespace

int main() {
  verify(early.pointer, {37, __STDCPP_DEFAULT_NEW_ALIGNMENT__});
  for (std::size_t bytes : std::array<std::size_t, 5>{0, 1, 8, 17, 131073}) {
    const auto alignment = bytes <= 8 ? 8U : __STDCPP_DEFAULT_NEW_ALIGNMENT__;
    void* scalar = ::operator new(bytes);
    verify(scalar, {bytes, alignment});
    ::operator delete(scalar);
    void* array = ::operator new[](bytes);
    verify(array, {bytes, alignment});
    ::operator delete[](array);
    scalar = ::operator new(bytes, std::nothrow);
    verify(scalar, {bytes, alignment});
    ::operator delete(scalar, std::nothrow);
    array = ::operator new[](bytes, std::nothrow);
    verify(array, {bytes, alignment});
    ::operator delete[](array, std::nothrow);
    constexpr auto aligned = std::align_val_t{256};
    scalar = ::operator new(bytes, aligned);
    verify(scalar, {bytes, 256});
    ::operator delete(scalar, aligned);
    array = ::operator new[](bytes, aligned);
    verify(array, {bytes, 256});
    ::operator delete[](array, aligned);
    scalar = ::operator new(bytes, aligned, std::nothrow);
    verify(scalar, {bytes, 256});
    ::operator delete(scalar, aligned, std::nothrow);
    array = ::operator new[](bytes, aligned, std::nothrow);
    verify(array, {bytes, 256});
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
  std::puts("global operator families, native lifecycle and early routing pass");
}
