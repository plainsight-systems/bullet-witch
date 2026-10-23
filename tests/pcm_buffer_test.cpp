// Prepared-PCM transport contracts, in process with no file/device access.
// Tests preserve data and timestamp segment boundaries across ring wrap/full
// pressure and drain one producer concurrently with one consumer. Runtime
// deadlines diagnose a stalled test; they do not claim device timing quality.
// Design/implementation MCP: CP.26 scopes/join workers; CP.42 (#rconc-wait)
// requires predicates for waits (this transport itself never waits).
// Performance CONC.5 (guidelines/concurrency/CONC.5-spsc-no-cas-needed.md)
// verifies SPSC ownership; MEM.9 (guidelines/memory/
// MEM.9-embedded-allocate-at-init.md) keeps read/write storage preallocated.
#include "video/pcm_buffer.hpp"

#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <stdexcept>
#include <thread>

#include "media_memory_fixture.hpp"

namespace {
void require(bool condition, const char* message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}
void domain_storage_lifetime_and_callback_gate() {
  auto backing = std::make_shared<bw::test::MediaBacking>();
  auto domain = bw::memory::Domain::create(backing);
  const std::weak_ptr<bw::memory::Domain> owner = domain;
  {
    bw::video::PcmBuffer ring(8, bw::memory::Allocator<float>(domain));
    require(domain->snapshot().live_bytes > 0, "PCM storage is domain charged");
    domain.reset();
    require(!owner.expired(), "PCM ring retains its original domain");
    const auto attempts = backing->attempts.load();
    backing->reject.store(true);
    const std::array<float, 4> input{1, -1, 2, -2};
    std::array<float, 4> output{};
    require(ring.write(input, 4) == 2 && ring.read(output).frames == 2 &&
                output == input,
            "Prepared PCM remains usable with native allocation disabled");
    require(backing->attempts.load() == attempts,
            "PCM read/write never enters the native allocator");
  }
  require(owner.expired() && backing->acquired == backing->released,
          "Retired ring releases all storage and its domain");
  auto bounded = bw::memory::Domain::create(backing, {.max_live_bytes = 1024});
  backing->reject.store(false);
  bool failed = false;
  try {
    bw::video::PcmBuffer ring(8, bw::memory::Allocator<float>(bounded));
  } catch (const std::bad_alloc&) {
    failed = true;
  }
  require(failed && bounded->snapshot().live_allocations == 0,
          "Partial PCM construction rolls back on descriptor budget failure");
}
void segment_wrap_and_pressure() {
  bw::video::PcmBuffer buffer(4);
  const std::array<float, 6> first{1, 2, 3, 4, 5, 6};
  const std::array<float, 4> second{7, 8, 9, 10};
  std::array<float, 8> output{};
  require(buffer.write(first, 1.0) == 3, "First PCM span fits");
  require(buffer.write(second, 2.0) == 1, "Write is limited by frame capacity");
  require(buffer.write(second, 3.0) == 0, "Full ring must not overwrite PCM");
  auto read = buffer.read(output);
  require(read.frames == 3 && read.timestamp_seconds == 1.0 && output[0] == 1 &&
              output[5] == 6,
          "Read must stop at timestamp segment boundary");
  require(buffer.write(std::span(second).subspan(2), 2.0 + 1.0 / 48000) == 1,
          "Released storage is reused across wrap");
  read = buffer.read(output);
  require(read.frames == 1 && read.timestamp_seconds == 2.0 && output[0] == 7,
          "Published segment remains intact after wrap");
  read = buffer.read(output);
  require(read.frames == 1 && output[0] == 9 && buffer.available() == 0,
          "Remaining wrapped samples drain exactly once");
  require(buffer.read(output).frames == 0 && buffer.consumed_frames() == 5,
          "Empty ring is stable and preserves consumed count");
}
void partial_read_and_segment_pressure() {
  bw::video::PcmBuffer buffer(2048);
  const std::array<float, 4> input{1, 2, 3, 4};
  std::array<float, 3> output{};
  require(buffer.write(input, 1) == 2, "PCM publication succeeds");
  auto read = buffer.read(output);
  require(read.frames == 1 && read.timestamp_seconds == 1 && output[2] == 0,
          "Odd trailing output float is untouched");
  read = buffer.read(output);
  require(read.frames == 1 &&
              std::abs(read.timestamp_seconds - (1 + 1.0 / 48000)) < 1e-12,
          "Partial segment timestamp advances by actual frames");
  for (std::size_t i = 0; i < bw::video::PcmBuffer::kSegmentCapacity; ++i) {
    require(buffer.write(std::span(input).first(2), 0) == 1,
            "Fixed segment descriptors admit bounded publications");
  }
  require(!buffer.writable() && buffer.write(input, 0) == 0,
          "Segment capacity applies even with free PCM frames");
  require(buffer.read(output).frames == 1 && buffer.writable(),
          "Consumer retirement frees a descriptor");
}
void concurrent_handoff() {
  bw::video::PcmBuffer buffer(31);
  constexpr std::size_t total = 10000;
  std::atomic<bool> stop{false};
  std::jthread producer([&] {
    for (std::size_t i = 0; i < total && !stop.load(); ++i) {
      const float value = static_cast<float>(i);
      const std::array<float, 2> sample{value, -value};
      while (!stop.load() &&
             buffer.write(sample, static_cast<double>(i) / 48000) == 0) {
        std::this_thread::yield();
      }
    }
  });
  const auto deadline =
      std::chrono::steady_clock::now() + std::chrono::seconds(5);
  bool valid = true;
  std::size_t count = 0;
  std::array<float, 6> output{};
  while (count < total && std::chrono::steady_clock::now() < deadline) {
    const auto read = buffer.read(output);
    if (read.frames == 0) {
      std::this_thread::yield();
      continue;
    }
    valid =
        valid && read.frames == 1 && output[0] == static_cast<float>(count) &&
        output[1] == -static_cast<float>(count) &&
        std::abs(read.timestamp_seconds - static_cast<double>(count) / 48000) <
            1e-12;
    ++count;
  }
  stop.store(true);
  producer.join();
  require(valid && count == total,
          "Concurrent PCM ownership preserves every sample and timestamp");
}
}  // namespace
void pcm_buffer_contracts() {
  domain_storage_lifetime_and_callback_gate();
  segment_wrap_and_pressure();
  partial_read_and_segment_pressure();
  concurrent_handoff();
}
