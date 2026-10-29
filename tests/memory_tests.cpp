// Native tests prepared before implementation, now exercising production calls.
// Core R.1: this test owner pairs mapping allocation/release even on exceptions.
// Production descriptor semantics remain explicit-release and copyable.
#include "core/memory/memory_failure.hpp"
#include "platform/macos/platform_memory.hpp"

#include <array>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>

#include <mach/vm_page_size.h>
#include <sys/wait.h>
#include <unistd.h>

namespace {
using bw::platform::macos::VirtualMemoryBlock;

void require(bool condition, std::string_view message) {
  if (!condition) {
    throw std::runtime_error(std::string(message));
  }
}

class MappingOwner {
public:
  explicit MappingOwner(std::size_t size) : block(VirtualMemoryBlock::allocate_virtual(size)) {}
  ~MappingOwner() { block.free_virtual(); }
  MappingOwner(const MappingOwner&) = delete;
  MappingOwner& operator=(const MappingOwner&) = delete;
  VirtualMemoryBlock block;
};

void empty_descriptor() {
  VirtualMemoryBlock block;
  require(block.virtual_pointer() == nullptr, "default block address");
  require(block.actual_size_in_pages() == 0, "default block page count");
  require(block.actual_size() == 0, "default block extent");
  block.free_virtual();
  block.free_virtual();
}

void native_alignment_and_rounding() {
  const auto page = static_cast<std::size_t>(vm_page_size);
  require(VirtualMemoryBlock::virtual_size_alignment() == page, "native virtual alignment");
  require(VirtualMemoryBlock::commit_alignment() == page, "native commit alignment");
  for (const std::size_t size : {std::size_t{1}, page, page + 1, 3 * page}) {
    MappingOwner owner(size);
    const auto expected_pages = (size + page - 1) / page;
    require(owner.block.actual_size_in_pages() == expected_pages, "rounded page count");
    require(owner.block.actual_size() == expected_pages * page, "rounded mapping extent");
    require(reinterpret_cast<std::uintptr_t>(owner.block.virtual_pointer()) % page == 0,
            "native aligned address");
  }
}

void mapping_is_read_write() {
  MappingOwner owner(2 * static_cast<std::size_t>(vm_page_size) + 1);
  auto* bytes = static_cast<std::byte*>(owner.block.virtual_pointer());
  // Mac maps read/write immediately; commit is not an access-permission gate.
  bytes[0] = std::byte{0x35};
  bytes[owner.block.actual_size() - 1] = std::byte{0x7a};
  require(bytes[0] == std::byte{0x35}, "first byte readable/writable");
  require(bytes[owner.block.actual_size() - 1] == std::byte{0x7a}, "last rounded byte accessible");
}

void subrange_reuse() {
  const auto page = static_cast<std::size_t>(vm_page_size);
  MappingOwner owner(3 * page);
  auto* bytes = static_cast<std::byte*>(owner.block.virtual_pointer());
  bytes[0] = std::byte{0x11};
  bytes[2 * page] = std::byte{0x22};
  owner.block.decommit(page, page);
  owner.block.commit(page, page);
  // Do not assert old contents or zeroing: MADV_FREE_REUSABLE discards ownership
  // of the middle page's contents, without guaranteeing when reclamation occurs.
  bytes[page] = std::byte{0x33};
  bytes[2 * page - 1] = std::byte{0x44};
  require(bytes[page] == std::byte{0x33}, "reused page writable");
  require(bytes[2 * page - 1] == std::byte{0x44}, "reused page extent writable");
  require(bytes[0] == std::byte{0x11} && bytes[2 * page] == std::byte{0x22},
          "neighbor pages preserved");
  owner.block.decommit_by_pointer(bytes + page, page);
  owner.block.commit_by_pointer(bytes + page, page);
  bytes[page] = std::byte{0x55};
  require(bytes[page] == std::byte{0x55}, "pointer-based reuse writable");
}

void whole_mapping_reuse() {
  MappingOwner owner(static_cast<std::size_t>(vm_page_size));
  void* address = owner.block.virtual_pointer();
  owner.block.decommit();
  owner.block.commit();
  require(owner.block.virtual_pointer() == address, "reuse retains virtual address");
  auto* bytes = static_cast<std::byte*>(address);
  bytes[0] = std::byte{0x66};
  require(bytes[0] == std::byte{0x66}, "whole mapping reused");
}

void release_clears_only_owner_descriptor() {
  MappingOwner owner(static_cast<std::size_t>(vm_page_size));
  const VirtualMemoryBlock alias = owner.block;
  void* address = owner.block.virtual_pointer();
  owner.block.free_virtual();
  require(owner.block.virtual_pointer() == nullptr && owner.block.actual_size() == 0,
          "release clears owner descriptor");
  require(alias.virtual_pointer() == address && alias.actual_size_in_pages() == 1,
          "copied descriptor remains an invalidated alias, not shared ownership");
  // Never dereference or free alias: the caller's mapping lifetime has ended.
  owner.block.free_virtual();
}

// Faults run in disposable children. No host memory exhaustion or production
// fault-injection hooks: these are real invalid native mapping operations.
enum class FailureCase : std::uint8_t {
  zero_mapping,
  decommit_unmapped,
  unmap_misaligned,
  commit_unmapped,
  bad_alignment,
  bad_range,
  concurrent_oom
};

void run_child_case(FailureCase which) {
  const auto page = static_cast<std::size_t>(vm_page_size);
  switch (which) {
  case FailureCase::zero_mapping:
    (void)VirtualMemoryBlock::allocate_virtual(0);
    break;
  case FailureCase::decommit_unmapped: {
    MappingOwner owner(page);
    VirtualMemoryBlock alias = owner.block;
    owner.block.free_virtual();
    alias.decommit(0, page);
    break;
  }
  case FailureCase::unmap_misaligned: {
    MappingOwner owner(page);
    auto* bytes = static_cast<std::byte*>(owner.block.virtual_pointer());
    VirtualMemoryBlock invalid(bytes + 1, 1);
    invalid.free_virtual();
    break;
  }
  case FailureCase::commit_unmapped: {
    MappingOwner owner(page);
    VirtualMemoryBlock alias = owner.block;
    owner.block.free_virtual();
    alias.commit(0, page); // Source intentionally ignores madvise reuse failure.
    break;
  }
  case FailureCase::bad_alignment:
    (void)VirtualMemoryBlock::allocate_virtual(page, 2 * page);
    break;
  case FailureCase::bad_range: {
    MappingOwner owner(page);
    owner.block.decommit(page, page);
    break;
  }
  case FailureCase::concurrent_oom: {
    std::atomic<bool> start{};
    std::array<std::thread, 4> contenders;
    for (auto& thread : contenders) {
      thread = std::thread([&start] {
        while (!start.load(std::memory_order_acquire)) {
          std::this_thread::yield();
        }
        errno = ENOMEM;
        bw::memory::out_of_memory(4096, 16);
      });
    }
    start.store(true, std::memory_order_release);
    for (auto& thread : contenders) {
      thread.join();
    }
    break;
  }
  }
  std::_Exit(0);
}

struct ChildResult {
  int status{};
  std::string output;
};

ChildResult child_result(FailureCase which) {
  std::array<int, 2> pipe_fds{};
  require(pipe(pipe_fds.data()) == 0, "create child diagnostic pipe");
  const pid_t pid = fork();
  if (pid == 0) {
    (void)close(pipe_fds[0]);
    if (dup2(pipe_fds[1], STDOUT_FILENO) < 0) {
      std::_Exit(90);
    }
    (void)close(pipe_fds[1]);
    (void)alarm(5);
    run_child_case(which);
  }
  (void)close(pipe_fds[1]);
  require(pid > 0, "fork child");
  ChildResult result;
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(6);
  while (waitpid(pid, &result.status, WNOHANG) == 0) {
    if (std::chrono::steady_clock::now() > deadline) {
      (void)kill(pid, SIGKILL);
      (void)waitpid(pid, &result.status, 0);
      (void)close(pipe_fds[0]);
      throw std::runtime_error("fatal child exceeded watchdog");
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  std::array<char, 4096> buffer{};
  for (;;) {
    const auto count = read(pipe_fds[0], buffer.data(), buffer.size());
    if (count <= 0) {
      break;
    }
    result.output.append(buffer.data(), static_cast<std::size_t>(count));
  }
  (void)close(pipe_fds[0]);
  return result;
}

void native_failure_semantics() {
  for (const auto which : {FailureCase::zero_mapping, FailureCase::decommit_unmapped,
                           FailureCase::unmap_misaligned, FailureCase::concurrent_oom}) {
    const auto result = child_result(which);
    require(WIFEXITED(result.status) && WEXITSTATUS(result.status) == 3,
            "native OOM must terminate with critical exit3");
    require(result.output.find("Out of memory:") != std::string::npos,
            "fatal allocation diagnosis emitted");
    require(result.output.find("MemoryStats:") != std::string::npos,
            "native memory statistics emitted");
    require(result.output.find("system_error=") != std::string::npos,
            "native failure error retained");
    require(result.output.find("Out of memory:", result.output.find("Out of memory:") + 1) ==
                std::string::npos,
            "fatal report emitted exactly once");
    if (which == FailureCase::zero_mapping) {
      require(result.output.find("size=0") != std::string::npos, "real failed mapping size");
      require(result.output.find("system_error=" + std::to_string(EINVAL)) != std::string::npos,
              "mmap length0 reports actual EINVAL");
      require(result.output.find("alignment=" + std::to_string(vm_page_size)) != std::string::npos,
              "mmap length0 records requested native alignment");
    }
    if (which == FailureCase::unmap_misaligned) {
      require(result.output.find("system_error=" + std::to_string(EINVAL)) != std::string::npos,
              "misaligned munmap reports actual EINVAL");
      require(result.output.find("size=" + std::to_string(vm_page_size) + " alignment=0") !=
                  std::string::npos,
              "munmap failure records source extent and zero alignment");
    }
    if (which == FailureCase::decommit_unmapped) {
      // macOS madvise(2): unallocated regions are EINVAL; ENOMEM means outside
      // the allowed address space, which this saved mapping address is not.
      require(result.output.find("system_error=" + std::to_string(EINVAL)) != std::string::npos,
              "unmapped madvise reports actual EINVAL");
      require(result.output.find("size=" + std::to_string(vm_page_size) + " alignment=0") !=
                  std::string::npos,
              "decommit failure records source extent and zero alignment");
    }
    if (which == FailureCase::concurrent_oom) {
      require(result.output.find("size=4096 alignment=16") != std::string::npos,
              "winning fatal record size and alignment");
    }
  }
  const auto ignored_commit = child_result(FailureCase::commit_unmapped);
  require(WIFEXITED(ignored_commit.status) && WEXITSTATUS(ignored_commit.status) == 0,
          "source reuse advice error does not invent fatal policy");
#ifndef NDEBUG
  for (const auto which : {FailureCase::bad_alignment, FailureCase::bad_range}) {
    const auto result = child_result(which);
    require(WIFEXITED(result.status) && WEXITSTATUS(result.status) == 3,
            "native debug check follows critical exit3");
    require(result.output.find("Memory assertion failed:") != std::string::npos,
            "debug precondition identified");
    require(result.output.find("Out of memory:") == std::string::npos,
            "precondition is not an OOM diagnosis");
  }
#endif
}
} // namespace

int main() {
  try {
    empty_descriptor();
    native_alignment_and_rounding();
    mapping_is_read_write();
    subrange_reuse();
    whole_mapping_reuse();
    release_clears_only_owner_descriptor();
    native_failure_semantics();
    std::cout << "Native virtual-memory behavior passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
