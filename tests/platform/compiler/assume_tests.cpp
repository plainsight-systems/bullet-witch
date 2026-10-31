#include "bw/platform/compiler/assume.hpp"

#include <cstdio>

// No definition on purpose: an ordinary assumption may type-check this call but
// must not evaluate it or require its symbol at link time.
bool unavailable_runtime_expression();

namespace {
struct ExplicitBoolean {
  explicit operator bool() const { return true; }
};
} // namespace

int main() {
  int evaluations = 0;
  // Exercise the actual macro, with no test-local replacement.
  BW_ANALYSIS_ASSUME(++evaluations > 0);
  BW_ANALYSIS_ASSUME(false);
  BW_ANALYSIS_ASSUME(unavailable_runtime_expression());
  BW_ANALYSIS_ASSUME(ExplicitBoolean{});
  if (evaluations != 0) {
    std::fprintf(stderr, "ordinary compiler assumption evaluated its expression\n");
    return 1;
  }
  return 0;
}
