// The one translation unit that emits metal-cpp's and the converter runtime
// shim's implementations (metal_api.hpp, contract). metal-cpp's three
// implementation macros come first, then the shim's metal-cpp selection and
// its implementation macro, then the headers; no other file of the backend
// defines any of them.
//
// Unreal f45729c0b6f097038e72b52d366a62baeca3275b
// Engine/Source/ThirdParty/MetalCPP/Private/MetalCPP.cpp:8-14: the pin's
// single metal-cpp implementation unit, the three macros then the three
// headers. Followed, with the shim's implementation added to the same unit.
#define NS_PRIVATE_IMPLEMENTATION
#define MTL_PRIVATE_IMPLEMENTATION
#define CA_PRIVATE_IMPLEMENTATION
#define IR_RUNTIME_METALCPP 1
#define IR_PRIVATE_IMPLEMENTATION
#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>
#include <QuartzCore/QuartzCore.hpp>
#include <metal_irconverter_runtime.h>

#include "metal_api.hpp"

#include <string_view>

namespace bw::rhi::metal {

// The shim declares the bind points `extern const` and defines them under
// IR_PRIVATE_IMPLEMENTATION, which is this unit only; the check therefore
// runs here, once, at device creation (metal_device.cpp).
std::string_view irconverter_bind_point_mismatch() noexcept {
  if (kIRDescriptorHeapBindPoint != kDescriptorHeapBindPoint) {
    return "the converter runtime's kIRDescriptorHeapBindPoint is not 0, where the "
           "descriptor heap is bound";
  }
  if (kIRSamplerHeapBindPoint != kSamplerHeapBindPoint) {
    return "the converter runtime's kIRSamplerHeapBindPoint is not 1, where the "
           "sampler heap is bound";
  }
  if (kIRArgumentBufferBindPoint != kArgumentBufferBindPoint) {
    return "the converter runtime's kIRArgumentBufferBindPoint is not 2, where the "
           "top-level argument buffer is set";
  }
  return {};
}

} // namespace bw::rhi::metal
