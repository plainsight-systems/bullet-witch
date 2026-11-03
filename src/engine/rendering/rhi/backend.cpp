#include "bw/rendering/rhi/backend.hpp"

#if BW_RHI_METAL
#include "backends/metal/metal_backend.hpp"
#endif

namespace bw::rhi {

// A switch over the kinds this build knows. A kind with no backend compiled
// in fails by name and creates nothing (E.4); there is no null backend to
// fall back to.
BackendResult create_backend(BackendKind kind, const BackendOptions& options) {
  switch (kind) {
  case BackendKind::Metal:
#if BW_RHI_METAL
    return metal::create_metal_backend(options);
#else
    // The Metal backend is compiled on Apple hosts only.
    static_cast<void>(options);
    return BackendResult::failure(DeviceError::BackendUnavailable,
                                  "Metal: no backend in this build");
#endif
  case BackendKind::Vulkan:
    // The Vulkan backend is a later slice; unavailable in this one.
    return BackendResult::failure(DeviceError::BackendUnavailable,
                                  "Vulkan: no backend in this build");
  case BackendKind::Direct3D12:
    // The Direct3D 12 backend is a later slice; unavailable in this one.
    return BackendResult::failure(DeviceError::BackendUnavailable,
                                  "Direct3D12: no backend in this build");
  }
  return BackendResult::failure(DeviceError::BackendUnavailable, "unknown backend kind");
}

} // namespace bw::rhi
