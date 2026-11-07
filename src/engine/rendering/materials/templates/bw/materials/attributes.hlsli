// The attributes a material template writes for a pass to shade
// (docs/architecture/materials.md, "Attributes" and invariant 6). The set a
// template may write is its shading model's row of the attribute table
// (bw/rendering/materials/shading.hpp, attribute_mask), which the template's
// shaders receive as the define BW_MATERIAL_ATTRIBUTES: the struct declares
// a member only when its bit is set, so a template that writes an attribute
// its shading model does not read does not compile, and the compiler names
// the member.
//
// Bits are the MaterialAttribute enumerators. World position offset (11) and
// pixel depth offset (12) have no member whatever the mask: no pass has an
// input for either (bw/rendering/scene/forward_pass.hpp, contract 3), so a
// template that writes one does not compile either.
#ifndef BW_MATERIALS_ATTRIBUTES_HLSLI
#define BW_MATERIALS_ATTRIBUTES_HLSLI

#ifndef BW_MATERIAL_ATTRIBUTES
#error "a material template's shaders are compiled with BW_MATERIAL_ATTRIBUTES"
#endif

#define BW_ATTRIBUTE_ACTIVE(bit) ((BW_MATERIAL_ATTRIBUTES >> (bit)) & 1)

struct BwMaterialAttributes {
#if BW_ATTRIBUTE_ACTIVE(0)
  float3 diffuse_albedo;
#endif
#if BW_ATTRIBUTE_ACTIVE(1)
  float3 f0;
#endif
#if BW_ATTRIBUTE_ACTIVE(2)
  float3 f90;
#endif
#if BW_ATTRIBUTE_ACTIVE(3)
  float roughness;
#endif
#if BW_ATTRIBUTE_ACTIVE(4)
  float3 normal;
#endif
#if BW_ATTRIBUTE_ACTIVE(5)
  float4 tangent;
#endif
#if BW_ATTRIBUTE_ACTIVE(6)
  float3 emissive;
#endif
#if BW_ATTRIBUTE_ACTIVE(7)
  float ambient_occlusion;
#endif
#if BW_ATTRIBUTE_ACTIVE(8)
  float3 subsurface_color;
#endif
#if BW_ATTRIBUTE_ACTIVE(9)
  float opacity;
#endif
#if BW_ATTRIBUTE_ACTIVE(10)
  float opacity_mask;
#endif
};

#endif
