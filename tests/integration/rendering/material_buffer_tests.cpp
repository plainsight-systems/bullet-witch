// Integration test: bw/rendering/materials/material_buffer.hpp on this Mac's
// Metal device, with a lit material built from a payload of
// material_payload.hpp.
#include "bw/rendering/materials/lit_template.hpp"
#include "bw/rendering/materials/material.hpp"
#include "bw/rendering/materials/material_asset.hpp"
#include "bw/rendering/materials/material_buffer.hpp"
#include "bw/rendering/rhi/device.hpp"
#include "bw/testing/harness.hpp"
#include "bw/testing/material_payload.hpp"

#include <array>
#include <memory>
#include <stdexcept>
#include <string>

namespace {

namespace m = bw::materials;
using bw::testing::require;

// (material_buffer.hpp, contracts 1 to 4): ranges on 16 bytes, one after the
// other and never reused; update writes only when the block differs; the
// reference is the view's index and the range's offset; one row per error
// name the device cannot cause (CapacityInvalid, Full, SlotInvalid).
void material_buffer_ranges() {
  bw::rhi::DeviceDescription description{};
  description.backend = bw::rhi::BackendKind::Metal;
  bw::rhi::DeviceResult created = bw::rhi::Device::create(description);
  if (!created.ok()) {
    throw std::runtime_error("Device::create failed: " + std::string(created.detail()));
  }
  bw::rhi::Device& device = *created.value();

  const bw::testing::materials::Bytes payload =
      bw::testing::materials::build_payload({.switches = {{m::kBaseColorMapSwitch, 0},
                                                          {m::kNormalMapSwitch, 0},
                                                          {m::kOcclusionReflectanceMapSwitch, 0}}});
  m::MaterialAssetView view;
  require(m::decode_material_asset(payload, view).error == m::MaterialAssetError::None,
          "the payload decodes");
  m::Material material;
  require(m::create_material(view, {}, {}, material).ok(), "the material creates");
  m::MaterialInstance instance(material);

  m::MaterialBuffer bad;
  require(m::create_material_buffer(device, 24, "material buffer", bad).error ==
              m::MaterialBufferError::CapacityInvalid,
          "CapacityInvalid: not a multiple of 16");

  m::MaterialBuffer buffer;
  require(m::create_material_buffer(device, 2U * m::kLitMaterialBytes, "material buffer", buffer)
                  .ok() &&
              buffer.capacity_bytes() == 2U * m::kLitMaterialBytes && buffer.used_bytes() == 0U,
          "a buffer of two blocks");
  m::MaterialSlot first{};
  m::MaterialSlot second{};
  require(buffer.add(device, instance, first).ok() && buffer.add(device, instance, second).ok(),
          "two blocks fit");
  require(first.offset == 0U && second.offset == m::kLitMaterialBytes &&
              first.size == m::kLitMaterialBytes && second.offset % 16U == 0U &&
              buffer.used_bytes() == 2U * m::kLitMaterialBytes,
          "ranges on 16 bytes, one after the other");
  m::MaterialSlot third{};
  require(buffer.add(device, instance, third).error == m::MaterialBufferError::Full &&
              buffer.used_bytes() == 2U * m::kLitMaterialBytes,
          "Full: nothing placed");

  bool wrote = true;
  require(buffer.update(device, first, instance, wrote).ok() && !wrote,
          "update of an unchanged block writes nothing");
  require(instance.set_scalar(m::kRoughness, 0.75F) == m::MaterialError::None &&
              buffer.update(device, first, instance, wrote).ok() && wrote,
          "update of a changed block writes");
  require(buffer.update(device, first, instance, wrote).ok() && !wrote,
          "update after the write finds it equal");
  require(buffer.update(device, m::MaterialSlot{16, m::kLitMaterialBytes}, instance, wrote).error ==
                  m::MaterialBufferError::SlotInvalid &&
              !wrote,
          "SlotInvalid: a range add did not return, on 16 bytes inside the placed bytes");

  const m::MaterialReference a = buffer.reference(first);
  const m::MaterialReference b = buffer.reference(second);
  require(a.buffer.value == b.buffer.value && a.offset == first.offset && b.offset == second.offset,
          "one buffer index, each range's offset");
}

using Scenario = bw::testing::Scenario<void>;
constexpr std::array kScenarios{
    Scenario{"material_buffer_ranges", &material_buffer_ranges},
};

} // namespace

int main(int argc, char** argv) { return bw::testing::run_scenario(argc, argv, kScenarios); }
