// Receiver reflection contracts using synthetic AREA/STBL data. Original
// selector execution is independently retained under ignored research evidence.
// Both-phase I.1 exercises explicit black/default outcomes; CDSA.32 verifies
// input bytes can expire and repeated queries reuse the prepared scene data.
#include "bw/lighting/reflection_field.hpp"

#include <bit>
#include <limits>

#include "../../../fixtures/camera_fixture.hpp"
#include "bw/lighting/scene_lighting.hpp"

namespace {
using ReflectionRow = std::array<std::int32_t, 2>;
bw::resources::Bytes reflection_regions(std::int32_t identifier) {
  using camera_fixture::scalar;
  using camera_fixture::word;
  bw::resources::Bytes bytes(128);
  word(bytes, 0, 0x41455241);
  word(bytes, 4, 1);
  word(bytes, 8, 1);
  word(bytes, 12, 16);
  word(bytes, 16, 32);
  scalar(bytes, 44, 4);
  scalar(bytes, 48, 2);
  scalar(bytes, 52, 2);
  scalar(bytes, 56, 2);
  word(bytes, 96, std::bit_cast<std::uint32_t>(identifier));
  return bytes;
}
bw::resources::Bytes reflection_table(std::span<const ReflectionRow> rows,
                                      std::uint32_t columns = 3,
                                      bool missing_slot = false) {
  using camera_fixture::word;
  constexpr std::size_t kCells = 104, kCellBytes = 8;
  bw::resources::Bytes bytes(kCells + rows.size() * columns * kCellBytes);
  word(bytes, 0, 0x4c425453);
  word(bytes, 4, 1);
  word(bytes, 28, 1);
  word(bytes, 32, 40);
  word(bytes, 72, static_cast<std::uint32_t>(rows.size()));
  word(bytes, 76, columns);
  word(bytes, 80, kCells);
  for (std::size_t row = 0; row < rows.size(); ++row) {
    if (columns > 0) {
      const auto offset = kCells + row * columns * kCellBytes;
      word(bytes, offset, 1);
      word(bytes, offset + 4, std::bit_cast<std::uint32_t>(rows[row][0]));
      if (columns > 2 && !missing_slot) {
        word(bytes, offset + 16, 1);
        word(bytes, offset + 20, std::bit_cast<std::uint32_t>(rows[row][1]));
      }
    }
  }
  return bytes;
}
bw::scene::ReflectionField reflection_field(
    std::int32_t region, std::span<const ReflectionRow> rows,
    bw::scene::ReflectionSlotResidency slots,
    const std::shared_ptr<bw::memory::Domain>& domain,
    std::uint32_t columns = 3, bool missing_slot = false) {
  auto region_bytes = reflection_regions(region);
  auto table_bytes = reflection_table(rows, columns, missing_slot);
  const auto definition =
      bw::formats::AreaDefinition::parse(region_bytes, domain);
  const auto table = bw::formats::SectionTable::parse(table_bytes, domain);
  return {bw::scene::AreaRegions(definition, domain), table, slots, domain};
}
void receiver_cache_contract(
    const bw::scene::ReflectionField& field,
    const std::shared_ptr<bw::memory::Domain>& domain) {
  using camera_fixture::require;
  // Empty authored SH field isolates cube selection while retaining the same
  // receiver cache/publication owners as a real spatially lit model.
  bw::resources::Bytes bytes(32);
  camera_fixture::word(bytes, 0, 0x544c4853);
  camera_fixture::word(bytes, 4, 1);
  camera_fixture::scalar(bytes, 24, 1);
  const std::array<bw::scene::SceneLightFieldSource, 1> fields{{{bytes, 1}}};
  bw::scene::SceneLighting scene(
      {fields, {}, {.05F, .15F, 4, false}, {}, &field}, domain);
  const bw::math::Affine3<float> camera{
      {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}, {0, 0, -5}}};
  scene.set_view({camera[3], bw::math::make_perspective_frustum(
                                 camera, {-1, 1, -1, 1, 1, 100})});
  bw::scene::RetainedLightingReceiver placed(scene, {0});
  require(
      placed.evaluate({1.75F, 0, 0}, 0).reflection_slot == 5,
      "Placed cube samples its own root, independently of the camera region");
  require(placed.evaluate({2, 0, 0}, 0).reflection_slot == 5,
          "Sub-threshold root crossing retains source cube with SH");
  const auto actor =
      scene.evaluate({.policy = bw::scene::ReceiverLightingPolicy::spatial,
                      .model_center = {2, 0, 0},
                      .field = {0}});
  require(actor.reflection_slot == 0,
          "Actor cube query does not inherit placed receiver cache");
  require(placed.evaluate({2.25F, 0, 0}, 0).reflection_slot == 0,
          "Exact movement threshold republishes placed cube selection");
}
}  // namespace
void reflection_field_tests() {
  using camera_fixture::require;
  auto domain =
      bw::memory::Domain::create(std::make_shared<camera_fixture::Backing>());
  const auto initial = domain->snapshot().live_bytes;
  {
    bw::scene::ReflectionSlotResidency slots{};
    slots[0] = slots[4] = slots[5] = true;
    const std::array<ReflectionRow, 4> rows{{{9, 4}, {0, 0}, {7, 5}, {7, 4}}};
    const auto field = reflection_field(7, rows, slots, domain);
    receiver_cache_contract(field, domain);
    require(field.slot_at({0, 0, 0}) == 5,
            "First matching table row selects receiver reflection");
    require(field.slot_at({2, 0, 0}) == 0,
            "Strict box face resolves missing region to identifier zero");
    const auto negative = reflection_field(-9, rows, slots, domain);
    require(negative.slot_at({0, 0, 0}) == 0,
            "Negative region is normalized before table selection");
    const auto unmatched = reflection_field(8, rows, slots, domain);
    require(unmatched.slot_at({0, 0, 0}) == 4,
            "Unmatched region uses first row, not a row named zero");
    slots[0] = false;
    const auto sparse = reflection_field(7, rows, slots, domain);
    require(!sparse.slot_at({0, 0, 0}),
            "Present selected map does not bypass mandatory slot zero");
    slots[0] = true;
    slots[5] = false;
    const auto absent = reflection_field(7, rows, slots, domain);
    require(!absent.slot_at({0, 0, 0}),
            "Absent selected slot uses black, not resident slot zero");
    for (const std::int32_t slot : {-1, 16}) {
      const std::array<ReflectionRow, 1> invalid{{{7, slot}}};
      const auto outside = reflection_field(7, invalid, slots, domain);
      require(!outside.slot_at({0, 0, 0}),
              "Signed and upper-bound invalid slots select black");
    }
    const auto missing_cell = reflection_field(7, rows, slots, domain, 3, true);
    const auto missing_column = reflection_field(7, rows, slots, domain, 2);
    require(missing_cell.slot_at({0, 0, 0}) == 0 &&
                missing_column.slot_at({0, 0, 0}) == 0,
            "Missing source integer cell or column reads zero");
    bool rejected = false;
    try {
      (void)reflection_field(7, {}, slots, domain);
    } catch (const std::invalid_argument&) {
      rejected = true;
    }
    require(rejected, "Configured reflection table needs a defined first row");
    rejected = false;
    try {
      (void)field.slot_at({std::numeric_limits<float>::quiet_NaN(), 0, 0});
    } catch (const std::invalid_argument&) {
      rejected = true;
    }
    require(rejected, "Nonfinite receiver input is rejected explicitly");
    const auto bytes = domain->snapshot().live_bytes;
    for (int i = 0; i < 20; ++i) {
      require(field.slot_at({0, 0, 0}) == 5 && field.slot_at({3, 0, 0}) == 0,
              "Independent receiver queries retain no camera-selected state");
    }
    require(domain->snapshot().live_bytes == bytes,
            "Reflection queries retain fixed prepared storage");
  }
  require(domain->snapshot().live_bytes == initial,
          "Reflection owner releases regions and copied bindings together");
}
