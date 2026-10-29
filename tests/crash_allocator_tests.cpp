#include "core/memory/allocator.hpp"
#include "core/memory/crash_allocator.hpp"
#include <array>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <exception>
#include <fcntl.h>
#include <spawn.h>
#include <stdexcept>
#include <string_view>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>

extern char** environ;
namespace {
void require(bool condition, const char* message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

void ordinary_contract() {
  auto& previous = bw::memory::general_allocator();
  auto* old = static_cast<unsigned char*>(previous.allocate(80));
  std::memset(old, 0x47, 80);
  auto& crash = bw::memory::CrashAllocator::get();
  require(!bw::memory::CrashAllocator::is_active(), "bootstrap prepares without activating");
  crash.activate();
  require(bw::memory::CrashAllocator::is_active(), "activation is visible");
  require(&bw::memory::general_allocator() == &crash, "facade publishes crash allocator");

  auto* slot = static_cast<unsigned char*>(crash.allocate(17));
  require(reinterpret_cast<std::uintptr_t>(slot) % 16 == 0, "crash slots align to 16");
  for (unsigned index = 0; index < 17; ++index) {
    require(slot[index] == 0xfe, "small allocation receives source fill");
  }
  std::size_t size = 0;
  require(crash.allocation_size(slot, size) && size == 17, "size reports requested bytes");
  // A non-owner's free must leave live crash storage untouched, not suspend.
  std::thread other([&] { crash.release(slot); });
  other.join();
  require(crash.allocation_size(slot, size) && size == 17, "non-owner free skips mutation");
  crash.release(slot);
  auto* reused = static_cast<unsigned char*>(crash.allocate(17));
  require(reused == slot, "small free returns first free slot");
  crash.release(reused);

  // Mac's first pool contains 224 slots; the next request promotes to 96 bytes.
  std::array<void*, 225> slots{};
  for (auto& pointer : slots) {
    pointer = crash.allocate(64);
  }
  const auto first = reinterpret_cast<std::uintptr_t>(slots.front());
  require(reinterpret_cast<std::uintptr_t>(slots[223]) == first + std::uintptr_t{223} * 64,
          "source first pool count and stride");
  require(reinterpret_cast<std::uintptr_t>(slots[224]) == first + std::uintptr_t{224} * 64,
          "exhaustion promotes into next contiguous pool");
  for (void* pointer : slots) {
    crash.release(pointer);
  }

  auto* large = static_cast<unsigned char*>(crash.allocate(40000));
  require(crash.allocation_size(large, size) && size == 40000, "large prefix records request");
  crash.release(large);
  void* next_large = crash.allocate(40000);
  require(next_large != large, "large free deliberately does not reclaim bump space");

  auto* copied = static_cast<unsigned char*>(crash.reallocate(old, 96));
  for (unsigned index = 0; index < 80; ++index) {
    require(copied[index] == 0x47, "realloc preserves previous Binned payload");
  }
  require(previous.allocation_size(old, size) && size >= 80,
          "crash realloc does not release original heap storage");
  std::memset(copied, 0x52, 96);
  auto* grown = static_cast<unsigned char*>(crash.reallocate(copied, 300));
  for (unsigned index = 0; index < 96; ++index) {
    require(grown[index] == 0x52, "realloc preserves crash payload");
  }
  require(crash.reallocate(grown, 0) == nullptr, "zero realloc releases and returns null");
  require(crash.quantize(17, 16) == 17, "inherited quantize is identity");
  crash.release(nullptr);
}

void child_case(std::string_view mode) {
  if (mode == "ordinary") {
    ordinary_contract();
    return;
  }
  static_cast<void>(bw::memory::general_allocator());
  auto& crash = bw::memory::CrashAllocator::get();
  crash.activate();
  if (mode == "exhaustion") {
    static_cast<void>(crash.allocate(std::size_t{2} * 1024 * 1024));
    throw std::runtime_error("oversize large reserve request returned");
  }
  if (mode == "suspend") {
    std::thread other([&] {
      // Parent requires this marker before interpreting a live process as the
      // intended non-owner suspension, rather than stalled initialization.
      static_cast<void>(write(STDOUT_FILENO, "SUSPEND_READY\n", 14));
      static_cast<void>(crash.allocate(64));
    });
    other.join();
    throw std::runtime_error("non-owner allocation returned");
  }
  throw std::runtime_error("unknown child case");
}

void run_case(const char* executable, const char* mode, bool expect_suspend, int expected_exit) {
  pid_t process{};
  std::array<char*, 4> arguments{const_cast<char*>(executable), const_cast<char*>("--child"),
                                 const_cast<char*>(mode), nullptr};
  std::array<int, 2> readiness{-1, -1};
  posix_spawn_file_actions_t actions;
  require(posix_spawn_file_actions_init(&actions) == 0, "initialize spawn actions");
  if (expect_suspend) {
    require(pipe(readiness.data()) == 0, "create suspension readiness pipe");
    require(fcntl(readiness[0], F_SETFL, O_NONBLOCK) == 0, "nonblocking readiness read");
    require(posix_spawn_file_actions_adddup2(&actions, readiness[1], STDOUT_FILENO) == 0,
            "redirect child readiness");
    require(posix_spawn_file_actions_addclose(&actions, readiness[0]) == 0, "close child read end");
    require(posix_spawn_file_actions_addclose(&actions, readiness[1]) == 0,
            "close child original write end");
  }
  require(posix_spawn(&process, executable, &actions, nullptr, arguments.data(), environ) == 0,
          "spawn crash contract process");
  static_cast<void>(posix_spawn_file_actions_destroy(&actions));
  if (expect_suspend) {
    static_cast<void>(close(readiness[1]));
  }
  bool reached_allocation = false;
  const auto deadline =
      std::chrono::steady_clock::now() + std::chrono::seconds(expect_suspend ? 2 : 10);
  int status = 0;
  while (std::chrono::steady_clock::now() < deadline) {
    if (expect_suspend && !reached_allocation) {
      std::array<char, 14> marker{};
      const auto count = read(readiness[0], marker.data(), marker.size());
      reached_allocation =
          count == 14 && std::string_view(marker.data(), marker.size()) == "SUSPEND_READY\n";
    }
    const auto waited = waitpid(process, &status, WNOHANG);
    require(waited >= 0, "wait for crash contract process");
    if (waited == process) {
      require(!expect_suspend, "non-owner allocation must remain suspended");
      require(WIFEXITED(status) && WEXITSTATUS(status) == expected_exit,
              "crash contract child exit code");
      return;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  static_cast<void>(kill(process, SIGKILL));
  static_cast<void>(waitpid(process, &status, 0));
  if (expect_suspend) {
    static_cast<void>(close(readiness[0]));
  }
  require(expect_suspend, "ordinary crash contract timed out");
  require(reached_allocation, "suspension must occur after worker entered allocation");
}
} // namespace

int main(int argc, char** argv) try {
  if (argc == 3 && std::string_view(argv[1]) == "--child") {
    child_case(argv[2]);
    return 0;
  }
  run_case(argv[0], "ordinary", false, 0);
  run_case(argv[0], "exhaustion", false, 3);
  run_case(argv[0], "suspend", true, 0);
  std::puts("Crash allocator contracts passed");
} catch (const std::exception& error) {
  std::fprintf(stderr, "Crash allocator test failed: %s\n", error.what());
  return 1;
}
