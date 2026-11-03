#ifndef BW_CORE_MEMORY_MEMORY_FAILURE_HPP
#define BW_CORE_MEMORY_MEMORY_FAILURE_HPP
#include <cstdint>

// Core E.26/E.28: fatal failures do not invent recovery; capture errno at the
// failing boundary. Performance MEM.7: OS mappings remain the platform boundary.
namespace bw::memory {
// Only the winning fatal thread owns this record and passes it by const reference
// to native reporting. There is no racing public mutable/global record accessor.
struct OutOfMemoryRecord {
  std::uint64_t allocation_size{};
  std::uint32_t allocation_alignment{};
  int system_error{};
};

// Report through function-local static initialization, then terminate.
// Concurrent callers wait for that initializer. An infinite sleep follows if
// it returns. Recursive static initialization has no added recovery guarantee.
[[noreturn]] void out_of_memory(std::uint64_t size, std::uint32_t alignment);
} // namespace bw::memory
#endif
