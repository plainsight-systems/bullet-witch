#include "error_history.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdio>
#include <stdexcept>
#include <string_view>
#include <thread>
#include <type_traits>

namespace {
namespace history = bw::diagnostics::detail;
using history::ErrorHistoryWriter;
constexpr auto capacity = history::error_history_capacity;

static_assert(capacity == 16384);
static_assert(sizeof(decltype(history::error_history)::value_type) == 2);
static_assert(!std::is_copy_constructible_v<ErrorHistoryWriter>);
static_assert(!std::is_copy_assignable_v<ErrorHistoryWriter>);
static_assert(!std::is_move_constructible_v<ErrorHistoryWriter>);
static_assert(!std::is_move_assignable_v<ErrorHistoryWriter>);
static_assert(std::is_trivially_destructible_v<bw::concurrency::WordMutex>);

void require(bool condition, const char* message) {
  if (!condition) {
    throw std::runtime_error{message};
  }
}

// Only inspect while owning the writer, or with all writers already destroyed.
std::u16string_view text() { return history::error_history.data(); }

void initial_and_reset_contracts() {
  require(std::ranges::all_of(history::error_history, [](char16_t unit) { return unit == 0; }),
          "entire process history starts zero initialized");
  {
    ErrorHistoryWriter writer;
    writer.append(u"previous history");
  }
  require(text() == u"previous history", "destructor preserves appended text");
  {
    ErrorHistoryWriter writer;
    require(text() == u"previous history", "construction does not eagerly clear prior history");
    writer.append(u"");
    require(text() == u"previous history", "empty append leaves prior history unchanged");
  }
  require(text().empty(), "empty writer terminates at its fresh zero cursor");
  require(history::error_history[1] == u'r', "termination does not clear trailing storage");
  {
    ErrorHistoryWriter writer;
    writer.append(u"new");
    writer.append(u" text");
    require(text() == u"new text", "appends advance cursor and terminate current text");
    writer.terminate_with(u"\n");
    writer.append(u"after");
    writer.terminate_with(u"\r\n\r\n");
  }
  require(text() == u"new text\nafter\r\n\r\n", "termination keeps cursor usable for later text");
}

void extent_and_suffix_contracts() {
  std::array<char16_t, capacity + 2> too_long{};
  too_long.fill(u'Q');
  too_long.back() = 0;
  {
    ErrorHistoryWriter writer;
    writer.append(too_long.data());
    require(text().size() == capacity - 1, "overlong append reserves terminal unit");
    require(std::ranges::all_of(text(), [](char16_t unit) { return unit == u'Q'; }),
            "overlong append copies prefix exactly");
    writer.append(u"discarded");
    require(text().size() == capacity - 1 && text().back() == u'Q',
            "full buffer append is unchanged");
    writer.terminate_with(u"\r\n\r\n");
    require(text().size() == capacity - 1 && text().ends_with(u"\r\n\r\n"),
            "full history replaces tail with complete suffix");
    require(history::error_history[capacity - 6] == u'Q' &&
                history::error_history[capacity - 1] == 0,
            "suffix replacement preserves preceding unit and final terminator");
  }
  std::array<char16_t, capacity - 1> almost_full{};
  almost_full.fill(u'X');
  almost_full.back() = 0; // Exactly capacity-2 text units, leaving one plus NUL.
  {
    ErrorHistoryWriter writer;
    writer.append(almost_full.data());
    writer.append(u"YZ");
    require(text().size() == capacity - 1 && text().back() == u'Y',
            "last available text unit is copied, remaining input truncated");
    writer.terminate_with(u"");
    require(text().back() == u'Y', "empty termination does not discard a full-buffer unit");
  }
  {
    ErrorHistoryWriter writer;
    writer.append(u"a");
    writer.terminate_with(almost_full.data());
    require(text().size() == capacity - 1 && text().front() == u'a',
            "suffix that exactly fits retains existing prefix");
  }
  std::array<char16_t, capacity> largest_suffix{};
  largest_suffix.fill(u'S');
  largest_suffix.back() = 0;
  {
    ErrorHistoryWriter writer;
    writer.append(u"prefix");
    writer.terminate_with(largest_suffix.data());
    require(text().size() == capacity - 1 && text().front() == u'S',
            "largest valid suffix replaces all prior text");
  }
}

void code_unit_contracts() {
  const std::array<char16_t, 5> units{u'\u03A9', char16_t{0xD83D}, char16_t{0xDE00},
                                      char16_t{0xD800}, char16_t{0}};
  {
    ErrorHistoryWriter writer;
    writer.append(units.data());
  }
  require(text() == std::u16string_view{units.data(), 4},
          "non-ASCII, surrogate pair and unpaired surrogate remain exact units");
  std::array<char16_t, capacity - 1> prefix{};
  prefix.fill(u'P');
  prefix.back() = 0;
  const std::array<char16_t, 3> pair{char16_t{0xD83D}, char16_t{0xDE00}, char16_t{0}};
  {
    ErrorHistoryWriter writer;
    writer.append(prefix.data());
    writer.append(pair.data());
  }
  require(text().size() == capacity - 1 && text().back() == char16_t{0xD83D},
          "source truncates code units without repairing a split surrogate");
}

void lock_lifetime_contracts() {
  bool acquired_during_writer = true;
  {
    ErrorHistoryWriter writer;
    std::thread observer([&] {
      acquired_during_writer = history::error_history_mutex.try_lock();
      if (acquired_during_writer) {
        history::error_history_mutex.unlock();
      }
    });
    observer.join();
    require(!acquired_during_writer, "writer owns actual shared mutex throughout lifetime");
    writer.append(u"locked");
  }
  const bool acquired_after_writer = history::error_history_mutex.try_lock();
  if (acquired_after_writer) {
    history::error_history_mutex.unlock();
  }
  require(acquired_after_writer, "destructor releases actual shared mutex");
  try {
    ErrorHistoryWriter writer;
    writer.append(u"unwound");
    throw std::runtime_error{"fixture unwind"};
  } catch (const std::runtime_error& failure) {
    // Exercise the C++ RAII lifetime; this is not production assertion throwing.
    require(std::string_view{failure.what()} == "fixture unwind", "expected fixture exception");
  }
  const bool acquired_after_unwind = history::error_history_mutex.try_lock();
  if (acquired_after_unwind) {
    history::error_history_mutex.unlock();
  }
  require(acquired_after_unwind && text() == u"unwound", "unwinding terminates and releases");
}

void concurrent_writers_contract() {
  constexpr std::array<char16_t, 4> tokens{u'A', u'B', u'C', u'D'};
  std::atomic<bool> intact{true};
  std::array<std::thread, tokens.size()> workers;
  for (std::size_t i = 0; i < workers.size(); ++i) {
    workers[i] = std::thread([&, i] {
      const std::array<char16_t, 2> token{tokens[i], char16_t{0}};
      for (int iteration = 0; iteration < 128; ++iteration) {
        ErrorHistoryWriter writer;
        for (int part = 0; part < 8; ++part) {
          writer.append(token.data());
          std::this_thread::yield();
        }
        writer.terminate_with(u"\n");
        const auto observed = text();
        if (observed.size() != 9 || observed.back() != u'\n' ||
            !std::ranges::all_of(observed.substr(0, 8),
                                 [&](char16_t unit) { return unit == tokens[i]; })) {
          intact.store(false, std::memory_order_relaxed);
        }
      }
    });
  }
  for (auto& worker : workers) {
    worker.join();
  }
  require(intact.load(std::memory_order_relaxed), "writer transactions do not interleave");
}
} // namespace

int main() {
  try {
    initial_and_reset_contracts();
    extent_and_suffix_contracts();
    code_unit_contracts();
    lock_lifetime_contracts();
    concurrent_writers_contract();
    std::puts("error history contracts passed");
    return 0;
  } catch (const std::exception& failure) {
    std::fprintf(stderr, "%s\n", failure.what());
    return 1;
  }
}
