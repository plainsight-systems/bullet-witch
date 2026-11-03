// Unit test: public contracts of bw/rendering/rhi/queries.hpp, one scenario
// per CTest entry. Each scenario names the header declaration it asserts.
#include "bw/rendering/rhi/queries.hpp"

#include "test_main.hpp"

#include <array>

namespace {
using bw::rhi::QueryError;
using bw::rhi::QueryKind;
using bw::rhi::QueryPoolDescription;
using bw_rhi_test::require;

// (queries.hpp, validate_query_pool): ZeroCount, then MissingDebugName; the
// kind is the device's check (it needs the capability report).
void query_pool_validation_named() {
  require(bw::rhi::validate_query_pool(QueryPoolDescription{QueryKind::Occlusion, 4, "Pass"}) ==
              QueryError::None,
          "a valid pool");
  require(bw::rhi::validate_query_pool(QueryPoolDescription{QueryKind::Occlusion, 0, ""}) ==
              QueryError::ZeroCount,
          "a count of zero, before the name");
  require(bw::rhi::validate_query_pool(QueryPoolDescription{QueryKind::Occlusion, 1, ""}) ==
              QueryError::MissingDebugName,
          "an empty debug name");
  require(bw::rhi::validate_query_pool(QueryPoolDescription{QueryKind::AbsoluteTime, 2, "Time"}) ==
              QueryError::None,
          "the kind is not this function's check");
}

constexpr std::array scenarios{
    bw_rhi_test::Scenario{"query_pool_validation_named", query_pool_validation_named},
};
} // namespace

int main(int argc, char** argv) { return bw_rhi_test::run_scenario(argc, argv, scenarios); }
