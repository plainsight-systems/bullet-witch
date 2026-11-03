#ifndef BW_TESTS_UNIT_RENDERING_RHI_TEST_MAIN_HPP
#define BW_TESTS_UNIT_RENDERING_RHI_TEST_MAIN_HPP
// The scenario harness every RHI unit test shares: require throws, main runs
// the one scenario named by argv[1], an unknown scenario is a failure.
#include <cstddef>
#include <exception>
#include <iostream>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>

namespace bw_rhi_test {

inline void require(bool condition, std::string_view message) {
  if (!condition) {
    throw std::runtime_error(std::string(message));
  }
}

// The value of an optional the scenario requires to be present.
template <class T> T unwrap(const std::optional<T>& value, std::string_view message) {
  if (!value.has_value()) {
    throw std::runtime_error(std::string(message));
  }
  return *value;
}

using ScenarioFunction = void (*)();
struct Scenario {
  std::string_view name;
  ScenarioFunction run;
};

inline int run_scenario(int argc, char** argv, std::span<const Scenario> scenarios) {
  if (argc != 2) {
    std::cerr << "Provide one scenario name\n";
    return 2;
  }
  const std::string_view requested = argv[1];
  for (const Scenario& scenario : scenarios) {
    if (scenario.name == requested) {
      try {
        scenario.run();
        return 0;
      } catch (const std::exception& error) {
        std::cerr << scenario.name << ": " << error.what() << '\n';
        return 1;
      }
    }
  }
  std::cerr << "Unknown scenario\n";
  return 2;
}

} // namespace bw_rhi_test

#endif
