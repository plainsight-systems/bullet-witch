#ifndef BW_TESTS_SUPPORT_BW_TESTING_HARNESS_HPP
#define BW_TESTS_SUPPORT_BW_TESTING_HARNESS_HPP
// The one test harness. A test executable is a table of named scenarios and
// a main that hands argc and argv to run_scenario. Contract:
//   argv[1] names the scenario; the arguments after it are the scenario's.
//   Exit 0: the scenario ran without a failed requirement.
//   Exit 1: a requirement failed; stderr holds "<scenario>: <message>".
//   Exit 2: usage (no scenario, unknown scenario, or the scenario's own
//           arguments did not parse); stderr holds the usage line.
// A requirement failure is a thrown std::runtime_error, so a scenario can
// use any helper that throws and needs no return value.

#include <array>
#include <cstddef>
#include <exception>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>

namespace bw::testing {

// Fails the running scenario with `message` when `condition` is false.
inline void require(bool condition, std::string_view message) {
  if (!condition) {
    throw std::runtime_error(std::string(message));
  }
}

// Table form: the message names the failing row.
inline void require(bool condition, std::size_t row, std::string_view note) {
  if (!condition) {
    throw std::runtime_error("row " + std::to_string(row) + ": " + std::string(note));
  }
}

// A scenario taking the executable's parsed arguments, or none (Arguments =
// void). Tables are constexpr arrays of these.
template <typename Arguments> struct Scenario {
  std::string_view name;
  void (*run)(const Arguments&);
};

template <> struct Scenario<void> {
  std::string_view name;
  void (*run)();
};

namespace detail {

template <typename Scenarios>
const typename Scenarios::value_type* find(const Scenarios& scenarios, std::string_view name) {
  for (const auto& scenario : scenarios) {
    if (scenario.name == name) {
      return &scenario;
    }
  }
  return nullptr;
}

inline int usage(const char* executable, std::string_view arguments) {
  std::cerr << "Usage: " << executable << " <scenario>" << (arguments.empty() ? "" : " ")
            << arguments << '\n';
  return 2;
}

template <typename Run> int guarded(std::string_view name, Run run) {
  try {
    run();
    return 0;
  } catch (const std::exception& error) {
    std::cerr << name << ": " << error.what() << '\n';
    return 1;
  }
}

} // namespace detail

// Scenarios without arguments: argv is exactly {executable, scenario}.
template <std::size_t N>
int run_scenario(int argc, char** argv, const std::array<Scenario<void>, N>& scenarios) {
  if (argc != 2) {
    return detail::usage(argv[0], "");
  }
  const Scenario<void>* scenario = detail::find(scenarios, argv[1]);
  if (scenario == nullptr) {
    std::cerr << "Unknown scenario " << argv[1] << '\n';
    return 2;
  }
  return detail::guarded(scenario->name, scenario->run);
}

// Scenarios with arguments: `parse` receives argv[2..argc) and fills
// `Arguments`, returning false when they do not fit; `usage_arguments` is the
// usage text after "<scenario>", for example "<game_host> <artifact dir>".
template <typename Arguments, std::size_t N, typename Parse>
int run_scenario(int argc, char** argv, const std::array<Scenario<Arguments>, N>& scenarios,
                 std::string_view usage_arguments, Parse parse) {
  if (argc < 2) {
    return detail::usage(argv[0], usage_arguments);
  }
  const Scenario<Arguments>* scenario = detail::find(scenarios, argv[1]);
  if (scenario == nullptr) {
    std::cerr << "Unknown scenario " << argv[1] << '\n';
    return 2;
  }
  Arguments arguments{};
  const std::span<char* const> rest(argv + 2, static_cast<std::size_t>(argc - 2));
  // A parse that throws (a path or string it cannot build) is a usage error
  // too, so no exception leaves main.
  try {
    if (!parse(rest, arguments)) {
      return detail::usage(argv[0], usage_arguments);
    }
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return detail::usage(argv[0], usage_arguments);
  }
  return detail::guarded(scenario->name, [&] { scenario->run(arguments); });
}

} // namespace bw::testing

#endif
