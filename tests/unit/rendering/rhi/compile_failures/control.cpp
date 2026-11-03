// Positive control for the RHI shipping compile probes
// (tests/unit/rendering/rhi/CMakeLists.txt): device.hpp and backend.hpp
// compile under BW_RHI_DEBUG=0 with the same flags the failing probes use,
// and a shipping DeviceDescription, which has no debug-only field
// (device.hpp, DeviceDescription), passes validate_device_description. A
// probe's failure is then its own fault and not the setup's.
#include "bw/rendering/rhi/backend.hpp"
#include "bw/rendering/rhi/device.hpp"

static_assert(BW_RHI_DEBUG == 0);
static_assert(bw::rhi::validate_device_description(bw::rhi::DeviceDescription{}) ==
              bw::rhi::DeviceError::None);
