// Build-time check of the shader toolchain's third-party headers, compiled
// through the targets cmake/ThirdParty.cmake declares and under the project's
// warning flags. Nothing here runs; each static_assert proves the header
// declared what the shader compiler tool uses rather than compiling to nothing.

// dxcapi.h includes WinAdapter.h itself on macOS and Linux.
#include <dxc/dxcapi.h>

// d3d12shader.h is not self-contained off Windows (SF.11 does not hold for it):
// it needs WinAdapter.h first, which sets COM_NO_WINDOWS_H, and `interface`
// defined as `struct`. This is the order DXC's own
// include/dxc/Support/D3DReflection.h uses.
#include <dxc/WinAdapter.h>
#define interface struct
#include <directx/d3d12shader.h>
#undef interface

#include <blake3.h>
#include <spirv-tools/optimizer.hpp>
#include <spirv_reflect.h>

#include <type_traits>

static_assert(std::is_class_v<DxcBuffer>, "dxcapi.h declares DxcBuffer");
static_assert(std::is_class_v<D3D12_SHADER_DESC>, "d3d12shader.h declares D3D12_SHADER_DESC");
static_assert(std::is_class_v<ID3D12ShaderReflection>,
              "d3d12shader.h declares ID3D12ShaderReflection");
static_assert(std::is_class_v<spvtools::Optimizer>, "optimizer.hpp declares spvtools::Optimizer");
static_assert(std::is_class_v<SpvReflectShaderModule>,
              "spirv_reflect.h declares SpvReflectShaderModule");
static_assert(BLAKE3_OUT_LEN == 32, "blake3.h declares a 32-byte digest");
