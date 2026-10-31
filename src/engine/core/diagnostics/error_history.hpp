#ifndef BW_CORE_DIAGNOSTICS_ERROR_HISTORY_HPP
#define BW_CORE_DIAGNOSTICS_ERROR_HISTORY_HPP

#include "bw/concurrency/word_mutex.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

// Core CP.20: writer lifetime owns the lock; I.5: code-unit and suffix contracts.
// Performance EMB.2: source fixed capacity; no heap growth or new overflow policy.
// This header is diagnostics-private, not part of the public include interface.
namespace bw::diagnostics::detail {
// Unreal f45729c0b6f097038e72b52d366a62baeca3275b, CoreGlobals.cpp:70 and
// MacPlatform.h:12-18: selected TCHAR is char16_t, not native Mac wchar_t.
inline constexpr std::size_t error_history_capacity = 16384;
extern std::array<char16_t, error_history_capacity> error_history;
// AssertionMacros.cpp:58-60. Internal access only; not an unlocked read API.
// Accepted WordMutex has dynamic initialization and trivial destruction.
extern concurrency::WordMutex error_history_mutex;

// Source FErrorHistWriter, AssertionMacros.cpp:194-281. Only same-encoding
// plain text and termination are implemented. Conversion/formatting remain absent.
// Inputs: non-null, zero-terminated char16_t, not overlapping error_history;
// length and length+cursor arithmetic fit int32_t. Calls borrow input only.
// Nonrecursive lock: do not nest writers or use one writer from multiple threads.
class ErrorHistoryWriter final {
public:
  // Source:199-202,274. Cursor starts at zero; acquire lock, retain old storage.
  ErrorHistoryWriter();
  ErrorHistoryWriter(const ErrorHistoryWriter&) = delete;
  ErrorHistoryWriter& operator=(const ErrorHistoryWriter&) = delete;
  ErrorHistoryWriter(ErrorHistoryWriter&&) = delete;
  ErrorHistoryWriter& operator=(ErrorHistoryWriter&&) = delete;
  // Source:204-208. Empty suffix termination, then release lock.
  ~ErrorHistoryWriter();

  // Source:221-230. Truncate to remaining space minus terminator. Empty/full
  // appends do nothing. Operates on code units, without Unicode repair.
  void append(const char16_t* text);
  // Source:210-219. Reserve full suffix and terminator by replacing tail if
  // needed; suffix length must be less than capacity. No oversized fallback.
  void terminate_with(const char16_t* suffix);

private:
  std::int32_t index_ = 0;
};
} // namespace bw::diagnostics::detail

#endif
