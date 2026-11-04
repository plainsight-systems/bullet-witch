#ifndef BW_TESTS_UNIT_RENDERING_RHI_UNWRAP_HPP
#define BW_TESTS_UNIT_RENDERING_RHI_UNWRAP_HPP
// The one helper the RHI unit tests share beyond the test harness
// (bw/testing/harness.hpp): unwrap fails the scenario, as a harness
// requirement does, when a required optional is empty.
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

namespace bw_rhi_test {

// The value of an optional the scenario requires to be present.
template <class T> T unwrap(const std::optional<T>& value, std::string_view message) {
  if (!value.has_value()) {
    throw std::runtime_error(std::string(message));
  }
  return *value;
}

} // namespace bw_rhi_test

#endif
