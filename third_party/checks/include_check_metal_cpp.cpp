// Build-time check of metal-cpp and the vendored Metal Shader Converter runtime
// shim in its metal-cpp form, compiled through bw_metal_cpp and
// bw_metal_irconverter_runtime under the project's warning flags. Nothing here
// runs; each static_assert proves a header declared what the Metal RHI uses.
//
// No *_PRIVATE_IMPLEMENTATION macro is defined here: metal-cpp's and the shim's
// implementations are emitted by the one translation unit of the Metal RHI that
// defines them.

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>
#include <QuartzCore/QuartzCore.hpp>

// The shim selects its metal-cpp typedefs and calls with IR_RUNTIME_METALCPP;
// it must be defined before the shim is included.
#define IR_RUNTIME_METALCPP 1
#include <metal_irconverter_runtime.h>

#include <type_traits>

static_assert(std::is_class_v<NS::String>, "Foundation.hpp declares NS::String");
static_assert(std::is_class_v<MTL::Device>, "Metal.hpp declares MTL::Device");
static_assert(std::is_class_v<CA::MetalLayer>, "QuartzCore.hpp declares CA::MetalLayer");
static_assert(std::is_class_v<IRDescriptorTableEntry>,
              "metal_irconverter_runtime.h declares IRDescriptorTableEntry");
static_assert(std::is_same_v<buffer_t, MTL::Buffer*>,
              "IR_RUNTIME_METALCPP selects the shim's metal-cpp typedefs");

// The bind points (descriptor heap 0, sampler heap 1, top-level argument
// buffer 2) are not asserted: metal_irconverter_runtime.h declares
// kIRDescriptorHeapBindPoint, kIRSamplerHeapBindPoint and
// kIRArgumentBufferBindPoint as `extern const uint64_t` and gives their values
// only under IR_PRIVATE_IMPLEMENTATION, so they are not constant expressions in
// this translation unit.
