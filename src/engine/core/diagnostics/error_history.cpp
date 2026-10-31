#include "error_history.hpp"

#include <algorithm>

// Core CP.20: writer RAII owns lock/release; I.5: source input preconditions.
// Performance EMB.2: fixed source capacity, without heap growth or new policy.
// Unreal reference: f45729c0b6f097038e72b52d366a62baeca3275b, Runtime/Core.
namespace bw::diagnostics::detail {
// Private/Misc/CoreGlobals.cpp:70 and AssertionMacros.cpp:58-60: process storage
// and mutex. Both are initialized before dynamic startup; no function-local
// guard or allocation is introduced.
std::array<char16_t, error_history_capacity> error_history{};
// constinit is our compile-time enforcement of the source static-initialization
// guarantee at AssertionMacros.cpp:58-60, not an extra runtime locking policy.
constinit concurrency::WordMutex error_history_mutex;

namespace {
constexpr auto capacity = static_cast<std::int32_t>(error_history_capacity);

// Public/Misc/CString.h:1089-1099 -> GenericWidePlatformString.h:77-88, Strlen.
// Extract the selected char16_t scan: count each nonzero unit, excluding NUL.
// No decoding, replacement, bounded-prefix scan or native wchar_t conversion.
std::int32_t code_unit_length(const char16_t* text) {
  std::int32_t length = -1;
  do {
    ++length;
  } while (*text++ != 0);
  return length;
}
} // namespace

// AssertionMacros.cpp:199-202,274, FErrorHistWriter constructor. The header's
// cursor initializer starts at zero; acquiring the mutex does not clear storage.
ErrorHistoryWriter::ErrorHistoryWriter() { error_history_mutex.lock(); }

// AssertionMacros.cpp:204-208, FErrorHistWriter destructor. Terminate at the
// current cursor while owned, then publish the completed write by unlocking.
ErrorHistoryWriter::~ErrorHistoryWriter() {
  terminate_with(u"");
  error_history_mutex.unlock();
}

// AssertionMacros.cpp:221-230, FErrorHistWriter::Append. Include one terminator
// in the bounded copy count; empty/full appends leave old storage unchanged.
void ErrorHistoryWriter::append(const char16_t* text) {
  const auto length = code_unit_length(text);
  const auto copy_count = std::min(length + 1, capacity - index_);
  if (copy_count > 1) {
    // CString.h:1033-1036 -> GenericWidePlatformString.cpp:33-66, Strncpy.
    // Extract its used extent: copy_count<=length+1, so every copied unit is
    // before input NUL; the source zero-padding branch cannot be reached here.
    auto* destination = error_history.data() + index_;
    for (std::int32_t unit = 0; unit < copy_count - 1; ++unit) {
      destination[unit] = text[unit];
    }
    destination[copy_count - 1] = 0;
    index_ += copy_count - 1;
  }
}

// AssertionMacros.cpp:210-219, FErrorHistWriter::TerminateWith. Rewind only when
// the complete suffix plus NUL would overflow, then append and explicitly end.
void ErrorHistoryWriter::terminate_with(const char16_t* suffix) {
  const auto length = code_unit_length(suffix);
  if (index_ + length >= capacity) {
    index_ = capacity - length - 1;
  }
  append(suffix);
  error_history[static_cast<std::size_t>(index_)] = 0;
}
} // namespace bw::diagnostics::detail
