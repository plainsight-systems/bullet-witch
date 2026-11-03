// Must not compile under BW_RHI_DEBUG=0. device.hpp, DeviceDescription: the
// validation switch, its reporter and the reporter's user exist only with
// BW_RHI_DEBUG 1. The diagnostic must name the field.
#include "bw/rendering/rhi/device.hpp"

static_assert(bw::rhi::DeviceDescription{}.validation);
