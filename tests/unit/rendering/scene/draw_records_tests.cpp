// Unit test: public contracts of bw/rendering/scene/draw_records.hpp. Pure
// values; no device.
#include "bw/rendering/scene/draw_records.hpp"

#include "bw/rendering/scene/frame_views.hpp"
#include "bw/rendering/shaders/permutation.hpp"
#include "bw/testing/harness.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace {

namespace s = bw::scene;
using bw::testing::require;

// (draw_records.hpp, contracts 3 and 4): equal keys share an index given in
// first-use order and a full table refuses a new key; every list of a
// three-view layout holds its pipeline's clusters, lies inside the two
// buffers and overlaps no other list's records or count.
void layout_and_table() {
  s::PipelineTable table;
  const auto key = [](std::uint32_t p) { return s::PipelineKey{nullptr, {p}, {}}; };
  require(table.index_of(key(7)) == 0U && table.index_of(key(3)) == 1U &&
              table.index_of(key(7)) == 0U,
          "first-use order, the same index for an equal key");
  for (std::uint32_t p = 100; table.keys().size() < s::kMaxPipelines; ++p) {
    require(table.index_of(key(p)).has_value(), "room until the limit");
  }
  require(!table.index_of(key(99)).has_value() && table.index_of(key(3)) == 1U,
          "a full table refuses a new key and keeps the old");

  const std::vector<std::uint32_t> clusters{5, 0, 7};
  const s::DrawListLayout layout = s::make_draw_list_layout(3, clusters);
  std::vector<s::DrawList> lists;
  for (std::uint32_t v = 0; v < 3U; ++v) {
    for (std::uint32_t p = 0; p < 3U; ++p) {
      lists.push_back(layout.list(v, p, s::DrawPhase::PhaseOne));
      require(lists.back().capacity == clusters[p], p, "a list holds its pipeline's clusters");
      if (v == s::kMainView) {
        lists.push_back(layout.list(v, p, s::DrawPhase::PhaseTwo));
        require(lists.back().capacity == clusters[p], p, "phase two holds them too");
      }
    }
  }
  for (std::size_t i = 0; i < lists.size(); ++i) {
    const s::DrawList& a = lists[i];
    const std::uint64_t a_end = a.record_offset + (std::uint64_t{a.capacity} * s::kDrawRecordBytes);
    require(a_end <= layout.record_bytes && a.count_offset + 4U <= layout.count_bytes, i,
            "inside the buffers");
    for (std::size_t j = i + 1U; j < lists.size(); ++j) {
      const s::DrawList& b = lists[j];
      const std::uint64_t b_end =
          b.record_offset + (std::uint64_t{b.capacity} * s::kDrawRecordBytes);
      require(a.count_offset != b.count_offset &&
                  (a_end <= b.record_offset || b_end <= a.record_offset),
              i, "no two lists overlap");
    }
  }
}

using Scenario = bw::testing::Scenario<void>;
constexpr std::array kScenarios{Scenario{"layout_and_table", &layout_and_table}};

} // namespace

int main(int argc, char** argv) { return bw::testing::run_scenario(argc, argv, kScenarios); }
