#include "fixture.hpp"

#include <type_traits>

namespace bw::tests::mutex_startup {
// Actual production type, not a model. Must reject its currently non-constexpr
// constructor even if an optimizer could promote that initialization to static.
constinit concurrency::WordMutex probe_mutex;
static_assert(std::is_trivially_destructible_v<concurrency::WordMutex>);

// These observations cannot be reset by a later dynamic constructor in this TU.
constinit bool observed_unlocked = false;
constinit bool acquired_before_main = false;
constinit bool startup_finished = false;
} // namespace bw::tests::mutex_startup
