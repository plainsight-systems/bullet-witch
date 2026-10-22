// A1: shared decoded ranges, owned bytes and explicit format/resource errors.
// No I/O or mutable globals. Errors carry a stable category; standard
// allocation failures propagate. Algorithm: callers construct checked
// file-relative ranges. Cost: value-sized ranges, owned byte/error strings.
// Optimization: no hidden copy. Design: cpp-guidelines I.1 (#ri-explicit),
// C.122 (#rh-separation): explicit ownership and injected file capability.
// cpp-performance COPY.2
// (guidelines/copy-move/COPY.2-mark-move-constructors-noexcept.md): container
// owners retain implicit moves. Implementation: SL.con.3 (#rsl-bounds) checks
// slices before access; COPY.4 (guidelines/copy-move/
// COPY.4-rule-of-five-rule-of-zero.md) keeps owning values rule-of-zero.

#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
namespace bw::resources {
inline constexpr std::size_t kCprRecordWordCount = 11;
inline constexpr std::size_t kCprHeaderWordCount = 23;
inline constexpr std::size_t kSectionCount = 6;
inline constexpr std::size_t kRatResourceWordCount = 5;
using Bytes = std::vector<std::uint8_t>;
enum class ErrorCode : std::uint8_t {
  io,
  malformed,
  unsupported,
  not_found,
  ambiguous
};
class Error : public std::runtime_error {
 public:
  Error(ErrorCode code, const std::string& message)
      : std::runtime_error(message), code(code) {}
  ErrorCode code;
};
std::string_view error_name(ErrorCode code);

struct Range {
  std::uint64_t offset{}, size{};
};
}  // namespace bw::resources
