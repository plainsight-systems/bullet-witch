// A1: stable error labels. Unknown enum values return "unknown". Algorithm:
// select a literal; O(1), no allocation. Optimization: return a static view.
// Design: cpp-guidelines I.1 (#ri-explicit), C.122 (#rh-separation):
// explicit ownership and injected file capability. cpp-performance COPY.2
// (guidelines/copy-move/COPY.2-mark-move-constructors-noexcept.md): container
// owners retain implicit moves. Implementation: SL.con.3 (#rsl-bounds) checks
// slices before access; COPY.4 (guidelines/copy-move/
// COPY.4-rule-of-five-rule-of-zero.md) keeps owning values rule-of-zero.

#include "bw/formats/common.hpp"
namespace bw::resources {
std::string_view error_name(ErrorCode code) {
  switch (code) {
    case ErrorCode::io:
      return "io";
    case ErrorCode::malformed:
      return "malformed";
    case ErrorCode::unsupported:
      return "unsupported";
    case ErrorCode::not_found:
      return "not_found";
    case ErrorCode::ambiguous:
      return "ambiguous";
  }
  return "unknown";
}
}  // namespace bw::resources
