#ifndef BW_TESTS_CONCURRENCY_STARTUP_FIXTURE_HPP
#define BW_TESTS_CONCURRENCY_STARTUP_FIXTURE_HPP

#include "bw/concurrency/word_mutex.hpp"

namespace bw::tests::mutex_startup {
extern concurrency::WordMutex probe_mutex;
extern bool observed_unlocked;
extern bool acquired_before_main;
extern bool startup_finished;
} // namespace bw::tests::mutex_startup

#endif
