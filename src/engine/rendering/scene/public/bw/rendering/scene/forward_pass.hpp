#ifndef BW_RENDERING_SCENE_FORWARD_PASS_HPP
#define BW_RENDERING_SCENE_FORWARD_PASS_HPP

#include "bw/rendering/geometry/vertex_streams.hpp"
#include "bw/rendering/materials/lit_template.hpp"
#include "bw/rendering/materials/material.hpp"
#include "bw/rendering/scene/draw_records.hpp"
#include "bw/rendering/scene/fixture_light.hpp"
#include "bw/rendering/scene/instance.hpp"
#include "bw/rendering/scene/view.hpp"
#include "bw/rendering/shaders/parameter_block.hpp"

#include <cstdint>
#include <string_view>

// Responsibility: the forward pass's lit shaders as the engine declares
// them: their names, their per-draw block, the blocks they read, and the
// lit shading model they apply to a template's attributes
// (docs/architecture/materials.md, invariant 7 and "Boundaries": the
// lighting model is the scene renderer's; a material supplies attributes
// and nothing else). The shading model's HLSL is
// src/engine/rendering/scene/hlsl/bw/scene/lit_shading.hlsli and is
// durable; the light it is called with this slice is the fixture's
// (fixture_light.hpp). Whoever records draws (bw/rendering/model_draw.hpp)
// binds a RecordDraw per record list (draw_records.hpp); no per-draw
// constant carries a transform (docs/architecture/scene.md, invariant 3).
//
// Stand-in: the scene renderer lights opaque and masked surfaces from
// surface data in a lighting pass and keeps this pass for the blended
// modes (docs/architecture/scene-renderer.md, invariants 1 and 2). Until
// that renderer exists these shaders draw every blend mode, opaque and
// masked included.
//
// Contract.
//   1. Shaders. kForwardLitVertexShader (vertex, entry main) and
//      kForwardLitPixelShader (pixel, entry main) both have the lit
//      template's domain and predicate (lit_template.hpp), the per-draw
//      block RecordDraw and the static blocks View and FixtureLight. The
//      vertex shader reads the DrawRecord at the draw's index in the list
//      RecordDraw names and the InstanceRecord it names (draw_records.hpp,
//      contract 2), and declares both as buffer blocks; it reads no
//      material. The pixel shader has no draw index of its own: it reads
//      the InstanceRecord at the instance index the vertex stage passes it
//      (contract 4) and a LitMaterial from the buffer array at that
//      instance's material, and declares those two as buffer blocks. One
//      artifact of each exists for every admitted permutation.
//   2. Vertex stage. The streams and object_to_world are the instance
//      record's. The vertex is SV_VertexID + streams.base_vertex
//      (vertex_streams.hpp). It reads the attributes of
//      lit_vertex_attributes for its permutation, each with the type
//      lit_template.hpp states. World position is the position as a row
//      vector times object_to_world; clip position is that times
//      View.world_to_clip. The normal and the tangent's xyz are
//      transformed by object_to_world's upper 3x3 and passed on unit
//      length; tangent.w passes through. That is exact for a rotation, a
//      translation and a uniform scale; an object_to_world with any other
//      scale is not supported.
//   3. Pixel stage. With V the unit vector from the world position to
//      View.eye_position, A the attributes the lit template writes for
//      the pixel given V, N = A.normal, L = FixtureLight.direction,
//      H = normalize(V + L), n_l = saturate(N.L), n_h = saturate(N.H),
//      v_h = saturate(V.H), n_v = abs(N.V) + 1e-5:
//        with masked, the pixel is discarded when A.opacity_mask < 0;
//        r = clamp(A.roughness, kMinimumRoughness, 1); a = r * r;
//        f90 = A.f90 * saturate(kGrazingFadeScale * max(A.f0.r, A.f0.g,
//          A.f0.b));
//        F = A.f0 + (f90 - A.f0) * (1 - v_h)^5;
//        D = a^2 / (pi * (n_h^2 * (a^2 - 1) + 1)^2);
//        Vis = 0.5 / (n_l * sqrt(n_v^2 * (1 - a^2) + a^2) +
//          n_v * sqrt(n_l^2 * (1 - a^2) + a^2));
//        E = A.f0 * ab.x + f90 * ab.y, the fraction of light the specular
//          term reflects over all directions, where with c0 = (-1,
//          -0.0275, -0.572, 0.022), c1 = (1, 0.0425, 1.04, -0.04),
//          q = r * c0 + c1 and t = min(q.x^2, 2^(-9.28 * n_v)) * q.x +
//          q.y: ab = (-1.04, 1.04) * t + q.zw;
//        diffuse = A.diffuse_albedo * (1 - dot(E, (0.2126, 0.7152,
//          0.0722))), the Rec. 709 luminance the lit template also uses;
//        color = (diffuse / pi + D * Vis * F) * FixtureLight.color * n_l
//          + FixtureLight.ambient * A.diffuse_albedo * A.ambient_occlusion
//          + A.emissive;
//        the target receives float4(color, A.opacity), linear; nothing is
//        tone mapped, so a value above 1 clips at the target.
//      The lit attributes the pass has no input for are world position
//      offset and pixel depth offset: the attribute struct has no member
//      for either, so a template that writes one does not compile.
//   4. The instance index reaches the pixel stage unchanged across a
//      triangle. While an instance's bone range is kNone, a record's bone
//      is not applied (instance.hpp, contract 2).
//
// The diffuse is Lambert; the specular is D F G / (4 n.l n.v) with a GGX D
// and a = roughness^2, a Schlick F written as F = f90 * Fc + (1 - Fc) * f0
// with Fc = (1 - v_h)^5, and the height-correlated masking-shadowing term
// in the visibility form Vis above, with n_v = abs(N.V) + 1e-5. The
// grazing reflectance is F90 times saturate(50 * max(F0)): the fade is the
// only specular occlusion, F0 already falls to 0 on its own, and a
// reflectance under 2 percent is treated as shadowing. E is the fraction
// the specular term reflects over all directions, f0 * ab.x + f90 * ab.y,
// with ab from the analytic approximation; the diffuse is scaled by
// 1 - luminance(E). Occlusion applies to indirect light only.
// There are no image-based or area lights and no specular occlusion of
// indirect light. The height-correlated term is used as it stands, with no
// roughness remap for analytic lights. The diffuse is Lambert, not
// retro-reflective. E is the analytic approximation, not a table, and the
// specular term is not raised for multiple scattering; the energy term is
// applied to the diffuse only.
// kMinimumRoughness is the floor for a light with no area: max(roughness,
// 0.04).
//
// Read guidance: I.1 (every input of a draw is in a block), Con.5.
namespace bw::scene {

inline constexpr std::string_view kForwardLitVertexShader = "forward_lit_mesh";
inline constexpr std::string_view kForwardLitPixelShader = "forward_lit";

// Roughness below this is shaded as this, so the distribution stays finite.
inline constexpr float kMinimumRoughness = 0.04F;
// The grazing reflectance is saturate(kGrazingFadeScale * f0): full from 2
// percent up, fading to none below.
inline constexpr float kGrazingFadeScale = 50.0F;

} // namespace bw::scene

#endif
