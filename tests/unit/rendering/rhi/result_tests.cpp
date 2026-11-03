// Unit test: public contracts of bw/rendering/rhi/result.hpp, one scenario
// per CTest entry. Each scenario names the header declaration it asserts.
#include "bw/rendering/rhi/result.hpp"

#include "test_main.hpp"

#include <array>
#include <cstdint>
#include <string_view>

namespace {
using bw::rhi::Result;
using bw::rhi::Status;
using bw_rhi_test::require;

enum class TestError : std::uint8_t { None, First, Second };

// (result.hpp, Result): ok() exactly when the error is None; a success
// holds the value and no error; a failure holds the error and its detail and
// no value. (result.hpp, Status): Status defaults to None and carries an
// error and detail.
void success_failure_and_detail() {
  const auto success = Result<int, TestError>::success(42);
  require(success.ok(), "success is ok");
  require(success.value() == 42, "success holds the value");
  require(success.detail().empty(), "success has no detail");

  const auto failure = Result<int, TestError>::failure(TestError::Second, "probe");
  require(!failure.ok(), "failure is not ok");
  require(failure.error() == TestError::Second, "failure holds the error");
  require(failure.detail() == "probe", "failure holds the detail");

  const auto unnamed = Result<int, TestError>::failure(TestError::First);
  require(!unnamed.ok() && unnamed.detail().empty(), "a failure without a name has no detail");

  constexpr Status<TestError> none{};
  static_assert(none.ok());
  require(none.ok() && none.error() == TestError::None, "default status is None");
  const Status<TestError> failed{TestError::First, "named"};
  require(!failed.ok(), "a status with an error is not ok");
  require(failed.error() == TestError::First && failed.detail() == "named",
          "a status carries the error and detail");
}

constexpr std::array scenarios{
    bw_rhi_test::Scenario{"success_failure_and_detail", success_failure_and_detail},
};
} // namespace

int main(int argc, char** argv) { return bw_rhi_test::run_scenario(argc, argv, scenarios); }
