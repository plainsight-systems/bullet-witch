// Build-time check of the vendored Metal Shader Converter runtime shim. The
// shim requires Metal.h (or metal-cpp's Metal.hpp, which is not pinned) to be
// included before it, so this is an Objective-C++ translation unit.
#import <Metal/Metal.h>

#include <metal_irconverter_runtime.h>

#include <type_traits>

static_assert(std::is_class_v<IRDescriptorTableEntry>,
              "metal_irconverter_runtime.h declares IRDescriptorTableEntry");
