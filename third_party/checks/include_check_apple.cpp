// Build-time check of the Metal Shader Converter's C API header from C++, as
// the shader compiler tool includes it. Compiled on Apple platforms only.
#include <metal_irconverter/ir_version.h>
#include <metal_irconverter/metal_irconverter.h>

#include <type_traits>

static_assert(std::is_class_v<IRCompiler>, "metal_irconverter.h declares IRCompiler");
// third_party/metal_irconverter_runtime/ is vendored from converter 4.0.3; the
// shim and the installed converter must be the same release.
static_assert(IR_VERSION_MAJOR == 4 && IR_VERSION_MINOR == 0 && IR_VERSION_PATCH == 3,
              "installed Metal Shader Converter is not 4.0.3, the release the "
              "vendored runtime shim comes from");
