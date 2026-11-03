// Build-time check of the vendored Metal Shader Converter runtime shim after
// Metal.h. The shim requires Metal.h or metal-cpp's Metal.hpp to be included
// before it; this Objective-C++ translation unit checks the Metal.h form, and
// include_check_metal_cpp.cpp checks the metal-cpp form.
#import <Metal/Metal.h>

#include <metal_irconverter_runtime.h>

#include <type_traits>

static_assert(std::is_class_v<IRDescriptorTableEntry>,
              "metal_irconverter_runtime.h declares IRDescriptorTableEntry");
