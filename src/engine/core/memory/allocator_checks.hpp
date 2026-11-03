#ifndef BW_CORE_MEMORY_ALLOCATOR_CHECKS_HPP
#define BW_CORE_MEMORY_ALLOCATOR_CHECKS_HPP
#include "bw/platform/memory/memory_failure.hpp"
// Check and fatal canary sites use the accepted native critical-exit route. Shared
// spelling only; no new termination policy. Core CP.2: failure reporting does not
// mutate allocator state.
#ifndef NDEBUG
#define BW_ALLOCATOR_CHECK(expression)                                                             \
  ((expression) ? static_cast<void>(0)                                                             \
                : bw::platform::memory_check_failed(#expression, __FILE__, __LINE__))
#define BW_ALLOCATOR_VERIFY(expression) BW_ALLOCATOR_CHECK(expression)
#else
#define BW_ALLOCATOR_VERIFY(expression) static_cast<void>(expression)
#define BW_ALLOCATOR_CHECK(expression) static_cast<void>(0)
#endif
// Canary corruption is fatal in production too.
#define BW_ALLOCATOR_REQUIRE(expression)                                                           \
  ((expression) ? static_cast<void>(0)                                                             \
                : bw::platform::memory_check_failed(#expression, __FILE__, __LINE__))
#endif
