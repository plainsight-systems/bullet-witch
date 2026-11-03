#ifndef BW_RENDERING_RHI_BACKENDS_METAL_METAL_BACKEND_HPP
#define BW_RENDERING_RHI_BACKENDS_METAL_METAL_BACKEND_HPP
// The Metal backend's factory, the one declaration backend.cpp includes
// (backend.hpp, create_backend). Nothing here names a Metal type, so the
// backend-neutral module compiles without metal-cpp.
#include "bw/rendering/rhi/backend.hpp"

namespace bw::rhi::metal {

// backend.hpp, create_backend for BackendKind::Metal. Precondition: options
// validated by the device.
[[nodiscard]] BackendResult create_metal_backend(const BackendOptions& options);

} // namespace bw::rhi::metal
#endif
