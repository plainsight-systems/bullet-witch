#include "metal_formats.hpp"

#include "bw/rendering/rhi/format.hpp"

#include <cstdint>

namespace bw::rhi::metal {

// The Metal pixel format of each format format.hpp names. R32G32B32Float has
// no Metal pixel format; it is a vertex format only here.
MTL::PixelFormat pixel_format(Format format) noexcept {
  switch (format) {
  case Format::R8Unorm:
    return MTL::PixelFormatR8Unorm;
  case Format::R8G8Unorm:
    return MTL::PixelFormatRG8Unorm;
  case Format::R8G8B8A8Unorm:
    return MTL::PixelFormatRGBA8Unorm;
  case Format::R8G8B8A8UnormSrgb:
    return MTL::PixelFormatRGBA8Unorm_sRGB;
  case Format::B8G8R8A8Unorm:
    return MTL::PixelFormatBGRA8Unorm;
  case Format::B8G8R8A8UnormSrgb:
    return MTL::PixelFormatBGRA8Unorm_sRGB;
  case Format::R10G10B10A2Unorm:
    return MTL::PixelFormatRGB10A2Unorm;
  case Format::R11G11B10Float:
    return MTL::PixelFormatRG11B10Float;
  case Format::R16Float:
    return MTL::PixelFormatR16Float;
  case Format::R16G16Float:
    return MTL::PixelFormatRG16Float;
  case Format::R16G16B16A16Float:
    return MTL::PixelFormatRGBA16Float;
  case Format::R16G16B16A16Unorm:
    return MTL::PixelFormatRGBA16Unorm;
  case Format::R32Float:
    return MTL::PixelFormatR32Float;
  case Format::R32Uint:
    return MTL::PixelFormatR32Uint;
  case Format::R32G32Float:
    return MTL::PixelFormatRG32Float;
  case Format::R32G32B32A32Float:
    return MTL::PixelFormatRGBA32Float;
  case Format::R16G16Snorm:
    return MTL::PixelFormatRG16Snorm;
  case Format::R16G16B16A16Snorm:
    return MTL::PixelFormatRGBA16Snorm;
  case Format::R8G8B8A8Snorm:
    return MTL::PixelFormatRGBA8Snorm;
  case Format::R8G8B8A8Uint:
    return MTL::PixelFormatRGBA8Uint;
  case Format::BC1Unorm:
    return MTL::PixelFormatBC1_RGBA;
  case Format::BC1UnormSrgb:
    return MTL::PixelFormatBC1_RGBA_sRGB;
  case Format::BC2Unorm:
    return MTL::PixelFormatBC2_RGBA;
  case Format::BC2UnormSrgb:
    return MTL::PixelFormatBC2_RGBA_sRGB;
  case Format::BC3Unorm:
    return MTL::PixelFormatBC3_RGBA;
  case Format::BC3UnormSrgb:
    return MTL::PixelFormatBC3_RGBA_sRGB;
  case Format::BC4Unorm:
    return MTL::PixelFormatBC4_RUnorm;
  case Format::BC5Unorm:
    return MTL::PixelFormatBC5_RGUnorm;
  case Format::BC6HUfloat:
    return MTL::PixelFormatBC6H_RGBUfloat;
  case Format::BC7Unorm:
    return MTL::PixelFormatBC7_RGBAUnorm;
  case Format::BC7UnormSrgb:
    return MTL::PixelFormatBC7_RGBAUnorm_sRGB;
  case Format::D16Unorm:
    return MTL::PixelFormatDepth16Unorm;
  case Format::D32Float:
    return MTL::PixelFormatDepth32Float;
  case Format::D24UnormS8Uint:
    return MTL::PixelFormatDepth24Unorm_Stencil8;
  case Format::D32FloatS8Uint:
    return MTL::PixelFormatDepth32Float_Stencil8;
  case Format::R32G32B32Float:
  case Format::Unknown:
  case Format::Count:
    return MTL::PixelFormatInvalid;
  }
  return MTL::PixelFormatInvalid;
}

namespace {

[[nodiscard]] bool is_integer(Format format) noexcept {
  return format == Format::R32Uint || format == Format::R8G8B8A8Uint;
}

[[nodiscard]] bool is_32bit_float(Format format) noexcept {
  return format == Format::R32Float || format == Format::R32G32Float ||
         format == Format::R32G32B32A32Float;
}

} // namespace

// The capabilities reported per format, so that nothing is reported that this
// backend or Metal does not do:
//   - Block-compressed formats: Texture only.
//   - Depth formats: Texture, DepthStencil, Multisample; not RenderTarget,
//     Blendable or UnorderedAccess (Metal depth formats are not color
//     attachments, not blendable and not shader-writable).
//   - D24UnormS8Uint only where the device reports it
//     (isDepth24Stencil8PixelFormatSupported); block compressed formats only
//     where supportsBCTextureCompression.
//   - Integer formats are not Blendable (Metal blends float and normalized
//     formats only). 32-bit float formats are not reported Blendable and
//     sRGB and R11G11B10Float are not reported UnorderedAccess: they are
//     family-dependent and no pass of this slice proves them.
//   - VertexAttribute is reported for no format: this backend creates no
//     pipeline with a vertex layout (pipeline_states.hpp,
//     VertexLayoutUnsupported), so no format is usable as one here.
FormatCapability format_capability(Format format, FormatSupport support) noexcept {
  if (pixel_format(format) == MTL::PixelFormatInvalid) {
    return FormatCapability::None;
  }
  const FormatInfo info = format_info(format);
  if (info.is_compressed) {
    return support.bc_compression ? FormatCapability::Texture : FormatCapability::None;
  }
  if (info.is_depth) {
    if (format == Format::D24UnormS8Uint && !support.depth24_stencil8) {
      return FormatCapability::None;
    }
    return FormatCapability::Texture | FormatCapability::DepthStencil |
           FormatCapability::Multisample;
  }
  FormatCapability capability =
      FormatCapability::Texture | FormatCapability::RenderTarget | FormatCapability::Multisample;
  if (!is_integer(format) && !is_32bit_float(format)) {
    capability = capability | FormatCapability::Blendable;
  }
  if (!info.is_srgb && format != Format::R11G11B10Float) {
    capability = capability | FormatCapability::UnorderedAccess;
  }
  return capability;
}

MTL::CompareFunction compare_function(CompareFunction function) noexcept {
  switch (function) {
  case CompareFunction::Never:
    return MTL::CompareFunctionNever;
  case CompareFunction::Less:
    return MTL::CompareFunctionLess;
  case CompareFunction::LessEqual:
    return MTL::CompareFunctionLessEqual;
  case CompareFunction::Greater:
    return MTL::CompareFunctionGreater;
  case CompareFunction::GreaterEqual:
    return MTL::CompareFunctionGreaterEqual;
  case CompareFunction::Equal:
    return MTL::CompareFunctionEqual;
  case CompareFunction::NotEqual:
    return MTL::CompareFunctionNotEqual;
  case CompareFunction::Always:
    return MTL::CompareFunctionAlways;
  }
  return MTL::CompareFunctionAlways;
}

MTL::StencilOperation stencil_operation(StencilOp operation) noexcept {
  switch (operation) {
  case StencilOp::Keep:
    return MTL::StencilOperationKeep;
  case StencilOp::Zero:
    return MTL::StencilOperationZero;
  case StencilOp::Replace:
    return MTL::StencilOperationReplace;
  case StencilOp::SaturatedIncrement:
    return MTL::StencilOperationIncrementClamp;
  case StencilOp::SaturatedDecrement:
    return MTL::StencilOperationDecrementClamp;
  case StencilOp::Invert:
    return MTL::StencilOperationInvert;
  case StencilOp::Increment:
    return MTL::StencilOperationIncrementWrap;
  case StencilOp::Decrement:
    return MTL::StencilOperationDecrementWrap;
  }
  return MTL::StencilOperationKeep;
}

MTL::BlendOperation blend_operation(BlendOperation operation) noexcept {
  switch (operation) {
  case BlendOperation::Add:
    return MTL::BlendOperationAdd;
  case BlendOperation::Subtract:
    return MTL::BlendOperationSubtract;
  case BlendOperation::Min:
    return MTL::BlendOperationMin;
  case BlendOperation::Max:
    return MTL::BlendOperationMax;
  case BlendOperation::ReverseSubtract:
    return MTL::BlendOperationReverseSubtract;
  }
  return MTL::BlendOperationAdd;
}

// The constant blend factors are Metal's BlendColor and OneMinusBlendColor,
// the factors they name.
MTL::BlendFactor blend_factor(BlendFactor factor) noexcept {
  switch (factor) {
  case BlendFactor::Zero:
    return MTL::BlendFactorZero;
  case BlendFactor::One:
    return MTL::BlendFactorOne;
  case BlendFactor::SourceColor:
    return MTL::BlendFactorSourceColor;
  case BlendFactor::InverseSourceColor:
    return MTL::BlendFactorOneMinusSourceColor;
  case BlendFactor::SourceAlpha:
    return MTL::BlendFactorSourceAlpha;
  case BlendFactor::InverseSourceAlpha:
    return MTL::BlendFactorOneMinusSourceAlpha;
  case BlendFactor::DestinationAlpha:
    return MTL::BlendFactorDestinationAlpha;
  case BlendFactor::InverseDestinationAlpha:
    return MTL::BlendFactorOneMinusDestinationAlpha;
  case BlendFactor::DestinationColor:
    return MTL::BlendFactorDestinationColor;
  case BlendFactor::InverseDestinationColor:
    return MTL::BlendFactorOneMinusDestinationColor;
  case BlendFactor::ConstantBlendFactor:
    return MTL::BlendFactorBlendColor;
  case BlendFactor::InverseConstantBlendFactor:
    return MTL::BlendFactorOneMinusBlendColor;
  case BlendFactor::Source1Color:
    return MTL::BlendFactorSource1Color;
  case BlendFactor::InverseSource1Color:
    return MTL::BlendFactorOneMinusSource1Color;
  case BlendFactor::Source1Alpha:
    return MTL::BlendFactorSource1Alpha;
  case BlendFactor::InverseSource1Alpha:
    return MTL::BlendFactorOneMinusSource1Alpha;
  }
  return MTL::BlendFactorZero;
}

MTL::ColorWriteMask color_write_mask(ColorWriteMask mask) noexcept {
  const auto bits = static_cast<std::uint8_t>(mask);
  NS::UInteger result = MTL::ColorWriteMaskNone;
  if ((bits & static_cast<std::uint8_t>(ColorWriteMask::Red)) != 0U) {
    result |= MTL::ColorWriteMaskRed;
  }
  if ((bits & static_cast<std::uint8_t>(ColorWriteMask::Green)) != 0U) {
    result |= MTL::ColorWriteMaskGreen;
  }
  if ((bits & static_cast<std::uint8_t>(ColorWriteMask::Blue)) != 0U) {
    result |= MTL::ColorWriteMaskBlue;
  }
  if ((bits & static_cast<std::uint8_t>(ColorWriteMask::Alpha)) != 0U) {
    result |= MTL::ColorWriteMaskAlpha;
  }
  return static_cast<MTL::ColorWriteMask>(result);
}

// Border is ClampToBorderColor.
MTL::SamplerAddressMode address_mode(AddressMode mode) noexcept {
  switch (mode) {
  case AddressMode::Wrap:
    return MTL::SamplerAddressModeRepeat;
  case AddressMode::Clamp:
    return MTL::SamplerAddressModeClampToEdge;
  case AddressMode::Mirror:
    return MTL::SamplerAddressModeMirrorRepeat;
  case AddressMode::Border:
    return MTL::SamplerAddressModeClampToBorderColor;
  }
  return MTL::SamplerAddressModeRepeat;
}

// The three named border colors map one to one.
MTL::SamplerBorderColor border_color(BorderColor color) noexcept {
  switch (color) {
  case BorderColor::TransparentBlack:
    return MTL::SamplerBorderColorTransparentBlack;
  case BorderColor::OpaqueBlack:
    return MTL::SamplerBorderColorOpaqueBlack;
  case BorderColor::OpaqueWhite:
    return MTL::SamplerBorderColorOpaqueWhite;
  }
  return MTL::SamplerBorderColorTransparentBlack;
}

MTL::PrimitiveType primitive_type(PrimitiveType type) noexcept {
  switch (type) {
  case PrimitiveType::TriangleList:
    return MTL::PrimitiveTypeTriangle;
  case PrimitiveType::TriangleStrip:
    return MTL::PrimitiveTypeTriangleStrip;
  case PrimitiveType::LineList:
    return MTL::PrimitiveTypeLine;
  case PrimitiveType::PointList:
    return MTL::PrimitiveTypePoint;
  }
  return MTL::PrimitiveTypeTriangle;
}

// CounterClockwise culls Front and Clockwise culls Back, with the front face
// counter-clockwise: the encoder's winding is set to counter-clockwise with
// the cull mode (metal_context.cpp).
MTL::CullMode cull_mode(CullMode mode) noexcept {
  switch (mode) {
  case CullMode::None:
    return MTL::CullModeNone;
  case CullMode::Clockwise:
    return MTL::CullModeBack;
  case CullMode::CounterClockwise:
    return MTL::CullModeFront;
  }
  return MTL::CullModeNone;
}

MTL::TriangleFillMode fill_mode(FillMode mode) noexcept {
  return mode == FillMode::Wireframe ? MTL::TriangleFillModeLines : MTL::TriangleFillModeFill;
}

MTL::DepthClipMode depth_clip_mode(DepthClipMode mode) noexcept {
  return mode == DepthClipMode::Clamp ? MTL::DepthClipModeClamp : MTL::DepthClipModeClip;
}

// NoAction, Load, Clear to DontCare, Load, Clear.
MTL::LoadAction load_action(LoadAction action) noexcept {
  switch (action) {
  case LoadAction::NoAction:
    return MTL::LoadActionDontCare;
  case LoadAction::Load:
    return MTL::LoadActionLoad;
  case LoadAction::Clear:
    return MTL::LoadActionClear;
  }
  return MTL::LoadActionDontCare;
}

// NoAction, Store to DontCare, Store.
MTL::StoreAction store_action(StoreAction action) noexcept {
  return action == StoreAction::Store ? MTL::StoreActionStore : MTL::StoreActionDontCare;
}

MTL::IndexType index_type(IndexFormat format) noexcept {
  return format == IndexFormat::Uint16 ? MTL::IndexTypeUInt16 : MTL::IndexTypeUInt32;
}

NS::UInteger index_bytes(IndexFormat format) noexcept {
  return format == IndexFormat::Uint16 ? 2U : 4U;
}

MTL::TextureType texture_type(TextureShape shape) noexcept {
  switch (shape.dimension) {
  case TextureDimension::Texture2D:
    return shape.sample_count > 1U ? MTL::TextureType2DMultisample : MTL::TextureType2D;
  case TextureDimension::Texture2DArray:
    return shape.sample_count > 1U ? MTL::TextureType2DMultisampleArray : MTL::TextureType2DArray;
  case TextureDimension::Texture3D:
    return MTL::TextureType3D;
  case TextureDimension::TextureCube:
    return shape.array_size > 1U ? MTL::TextureTypeCubeArray : MTL::TextureTypeCube;
  }
  return MTL::TextureType2D;
}

// Blending is enabled unless both color and alpha are Add of One and Zero.
bool blending_enabled(const RenderTargetBlend& blend) noexcept {
  return blend.color_op != BlendOperation::Add || blend.color_destination != BlendFactor::Zero ||
         blend.color_source != BlendFactor::One || blend.alpha_op != BlendOperation::Add ||
         blend.alpha_destination != BlendFactor::Zero || blend.alpha_source != BlendFactor::One;
}

} // namespace bw::rhi::metal
