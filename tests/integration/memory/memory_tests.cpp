// Integration test: the Mac virtual-memory block (bw/platform/os/macos/
// platform_memory.hpp) against real mappings, and the out-of-memory path
// (bw/memory/memory_failure.hpp) observed from child processes. Descriptor
// arithmetic and the range precondition are unit tested separately.
#include "bw/memory/memory_failure.hpp"
#include "bw/platform/os/macos/platform_memory.hpp"

#include <sys/wait.h>
#include <unistd.h>

#include <array>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>

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

// The mapping is as long as the rounded extent the descriptor reports.
void mapping_covers_rounded_extent() {
  const auto page = VirtualMemoryBlock::virtual_size_alignment();
  MappingOwner owner(page + 1);
  require(owner.block.actual_size_in_pages() == 2, "a partial page rounds up");
  auto* bytes = static_cast<std::byte*>(owner.block.virtual_pointer());
  bytes[owner.block.actual_size() - 1] = std::byte{0x7a};
  require(bytes[owner.block.actual_size() - 1] == std::byte{0x7a}, "last rounded byte is mapped");
}

void decommit_keeps_the_address() {
  MappingOwner owner(VirtualMemoryBlock::virtual_size_alignment());
  void* address = owner.block.virtual_pointer();
  owner.block.decommit();
  owner.block.commit();
  require(owner.block.virtual_pointer() == address, "the address range remains mapped");
}

void release_clears_only_owner_descriptor() {
  MappingOwner owner(VirtualMemoryBlock::virtual_size_alignment());
  const VirtualMemoryBlock alias = owner.block;
  void* address = owner.block.virtual_pointer();
  owner.block.free_virtual();
  require(owner.block.virtual_pointer() == nullptr && owner.block.actual_size() == 0,
          "release clears the owner descriptor");
  require(alias.virtual_pointer() == address && alias.actual_size_in_pages() == 1,
          "a copied descriptor is an alias, not shared ownership");
}

enum class FailureCase : std::uint8_t {
  zero_mapping,
  decommit_unmapped,
  unmap_misaligned,
  commit_unmapped,
  bad_alignment,
  concurrent_oom
};

void run_child_case(FailureCase which) {
  const auto page = VirtualMemoryBlock::virtual_size_alignment();
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
    alias.commit(0, page);
    break;
  }
  case FailureCase::bad_alignment:
    (void)VirtualMemoryBlock::allocate_virtual(page, 2 * page);
    break;
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

// Contract: a failed mapping operation or an out-of-memory call never returns;
// the process reports once and exits with status 3. The report's wording is
// not part of the contract.
void failure_semantics() {
  for (const auto which : {FailureCase::zero_mapping, FailureCase::decommit_unmapped,
                           FailureCase::unmap_misaligned, FailureCase::concurrent_oom}) {
    const auto result = child_result(which);
    require(WIFEXITED(result.status) && WEXITSTATUS(result.status) == 3,
            "memory failure terminates with exit status 3");
    const auto first = result.output.find("Out of memory");
    require(first != std::string::npos, "memory failure emits a report");
    require(result.output.find("Out of memory", first + 1) == std::string::npos,
            "memory failure reports exactly once");
  }
  const auto ignored_commit = child_result(FailureCase::commit_unmapped);
  require(WIFEXITED(ignored_commit.status) && WEXITSTATUS(ignored_commit.status) == 0,
          "recommit advice failure is not fatal");
#ifndef NDEBUG
  const auto precondition = child_result(FailureCase::bad_alignment);
  require(WIFEXITED(precondition.status) && WEXITSTATUS(precondition.status) == 3,
          "debug precondition failure exits with status 3");
  require(precondition.output.find("Out of memory") == std::string::npos,
          "a precondition failure is not reported as out of memory");
#endif
}
} // namespace

int main() {
  try {
    mapping_covers_rounded_extent();
    decommit_keeps_the_address();
    release_clears_only_owner_descriptor();
    failure_semantics();
    std::cout << "virtual memory contracts passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
