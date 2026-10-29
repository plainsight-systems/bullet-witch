#ifndef BW_CORE_MEMORY_ALLOCATOR_CHECKS_HPP
#define BW_CORE_MEMORY_ALLOCATOR_CHECKS_HPP
#include "platform/memory_failure.hpp"
// Unreal f45729c0b6f097038e72b52d366a62baeca3275b: check()/fatal canary sites
// use the accepted native critical-exit route. Shared spelling only; no new
// termination policy. Core CP.2: failure reporting does not mutate allocator state.
#ifndef NDEBUG
#define BW_ALLOCATOR_CHECK(expression)                                                             \
  ((expression) ? static_cast<void>(0)                                                             \
                : bw::platform::memory_check_failed(#expression, __FILE__, __LINE__))
#define BW_ALLOCATOR_VERIFY(expression) BW_ALLOCATOR_CHECK(expression)
#else
#define BW_ALLOCATOR_VERIFY(expression) static_cast<void>(expression)
#define BW_ALLOCATOR_CHECK(expression) static_cast<void>(0)
#endif
// Canary corruption is fatal in production too (MallocBinned3.cpp:65-100,166-201).
#define BW_ALLOCATOR_REQUIRE(expression)                                                           \
  ((expression) ? static_cast<void>(0)                                                             \
                : bw::platform::memory_check_failed(#expression, __FILE__, __LINE__))
#endif
