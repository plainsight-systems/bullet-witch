// Transform original static geometry and evaluate the recovered surface
// families: base and layered textures, geometric/normal-map normals, vertex
// illumination, directional diffuse/specular, Fresnel/cubemap reflection and
// distance/height fog. Optional branches are original program features selected
// by the shared material requirements, not fallback shading. Registry/source
// mappings and numerical limitations live in world-renderer-foundation.md.
// Presentation fade is separate.
#include <metal_stdlib>
using namespace metal;
struct WorldColorParameters {
  float4 position_scale, position_offset;
  float4 clip_rows[4];
  float4 world_rows[3];
  float4 texture_offset[2];
  float4 indirect_color, material_color, fresnel;
  float4 illumination, ambient, reflection, specular;
  float4 light_diffuse, light_specular, light_direction;
  float4 fog_color, fog_distance, fog_height, fog_density;
  float4 indirect_secondary, environment_power;
  float4 irradiance_coefficients[7];
};
struct WorldColorStream {
  uint stride, position, normal, tangent, uv, color;
  uint secondary_uv, modulation_color;
  uint normal_map, secondary_base, environment_map, vertex_color;
  uint vertex_illumination, reflection, fresnel, directional_light;
  uint alpha_cutout, spherical_harmonics;
};
struct WorldColorVertex {
  float4 position [[position]];
  float3 world_position;
  float3 normal;
  float3 tangent;
  float4 uv;
  float2 coverage_uv;
  float3 illumination;
  float4 modulation;
};
float3 original_normalize(float3 value) {
  const float squared = dot(value, value);
  return squared == 0.0f ? float3(0.0f) : value * rsqrt(squared);
}
float3 decode_direction(const device uchar* source) {
  return max(
      float3(*reinterpret_cast<const device packed_short3*>(source)) / 32767.0f,
      -1.0f);
}
vertex WorldColorVertex world_color_vertex(
    uint vertex_id [[vertex_id]], device const uchar* vertices [[buffer(0)]],
    constant WorldColorParameters& p [[buffer(1)]],
    constant WorldColorStream& layout [[buffer(2)]]) {
  const device uchar* source = vertices + vertex_id * layout.stride;
  const float3 position = float3(
      *reinterpret_cast<const device packed_float3*>(source + layout.position));
  const float4 position4 =
      float4(position, 1.0f) * p.position_scale + p.position_offset;
  WorldColorVertex output;
  output.position =
      float4(dot(position4, p.clip_rows[0]), dot(position4, p.clip_rows[1]),
             dot(position4, p.clip_rows[2]), dot(position4, p.clip_rows[3]));
  output.world_position =
      float3(dot(position4, p.world_rows[0]), dot(position4, p.world_rows[1]),
             dot(position4, p.world_rows[2]));
  output.normal = 0.0f;
  output.tangent = 0.0f;
  if (layout.directional_light || layout.spherical_harmonics) {
    const float3 normal = decode_direction(source + layout.normal);
    output.normal = float3(dot(normal, p.world_rows[0].xyz),
                           dot(normal, p.world_rows[1].xyz),
                           dot(normal, p.world_rows[2].xyz));
  }
  if (layout.normal_map) {
    const float3 tangent = decode_direction(source + layout.tangent);
    output.tangent = float3(dot(tangent, p.world_rows[0].xyz),
                            dot(tangent, p.world_rows[1].xyz),
                            dot(tangent, p.world_rows[2].xyz));
  }
  output.coverage_uv =
      float2(*reinterpret_cast<const device packed_half2*>(source + layout.uv));
  output.uv.xy = output.coverage_uv + p.texture_offset[0].xy;
  output.uv.zw = p.texture_offset[1].xy;
  if (layout.secondary_base) {
    output.uv.zw += float2(*reinterpret_cast<const device packed_half2*>(
        source + layout.secondary_uv));
  }
  output.illumination = 0.0f;
  if (layout.vertex_illumination) {
    const float4 color =
        float4(*reinterpret_cast<const device uchar4*>(source + layout.color)) /
        255.0f;
    output.illumination = exp2(color.a * 255.0f - 128.0f) * color.rgb;
  }
  output.modulation = 1.0f;
  if (layout.vertex_color) {
    output.modulation = float4(*reinterpret_cast<const device uchar4*>(
                            source + layout.modulation_color)) /
                        255.0f;
  }
  return output;
}
fragment float4
world_color_fragment(WorldColorVertex input [[stage_in]],
                     constant float& presentation_opacity [[buffer(0)]],
                     constant WorldColorParameters& p [[buffer(1)]],
                     constant WorldColorStream& features [[buffer(2)]],
                     texture2d<float> base_texture [[texture(0)]],
                     texture2d<float> secondary_texture [[texture(1)]],
                     texture2d<float> normal_texture [[texture(3)]],
                     texturecube<float> environment_texture [[texture(7)]],
                     sampler material_sampler [[sampler(0)]],
                     sampler environment_sampler [[sampler(7)]]) {
  // Preserve original depth-mask coverage in this direct inspection pass.
  // The original depth vertex program forwards UV without material offset.
  if (features.alpha_cutout &&
      p.material_color.a *
              base_texture.sample(material_sampler, input.coverage_uv).a <
          0.5f) {
    discard_fragment();
  }
  float3 normal = original_normalize(input.normal);
  if (features.normal_map) {
    const float2 map =
        normal_texture.sample(material_sampler, input.uv.xy).xy * 2.0f - 1.0f;
    const float3 tangent = original_normalize(input.tangent);
    // Original rsq consumes abs, including quantized samples outside the disk.
    normal = normal * sqrt(abs(1.0f - dot(map, map))) + tangent * map.x +
             cross(tangent, normal) * map.y;
  }
  float4 sampled_base = base_texture.sample(material_sampler, input.uv.xy);
  if (features.secondary_base) {
    const float4 secondary =
        secondary_texture.sample(material_sampler, input.uv.zw) *
        p.indirect_secondary;
    sampled_base.rgb +=
        secondary.a * (sampled_base.rgb * secondary.rgb - sampled_base.rgb);
  }
  const float4 base =
      p.material_color * input.modulation * p.indirect_color * sampled_base;
  float3 illumination = input.illumination;
  float4 specular = 0.0f;
  const float distance = length(input.world_position);
  const float3 direction = input.world_position / distance;
  float facing = 0.0f;
  if (features.directional_light) {
    facing = saturate(dot(normal, -p.light_direction.xyz));
    if (facing > 0.0f) {
      illumination += p.light_diffuse.xyz * facing;
      if (features.reflection) {
        const float3 halfway =
            original_normalize(-direction - p.light_direction.xyz);
        const float response =
            pow(saturate(dot(normal, halfway)), p.specular.x) * p.specular.y *
            p.light_specular.w;
        specular = response * p.light_specular;
      }
    }
  }
  if (features.environment_map) {
    const float3 reflected =
        direction - normal * (2.0f * dot(direction, normal));
    const float4 environment =
        environment_texture.sample(environment_sampler, reflected) *
        p.environment_power;
    specular += environment * saturate(facing + p.reflection.w);
  }
  if (features.spherical_harmonics) {
    // Evaluate the original packed irradiance polynomial at the surface normal.
    // Constant material ambience remains a separate contribution.
    const float4 n = float4(normal, 1.0f);
    const float4 products = n.yzzx * n.xyzz;
    const float3 quadratic =
        float3(dot(p.irradiance_coefficients[3], products),
               dot(p.irradiance_coefficients[4], products),
               dot(p.irradiance_coefficients[5], products));
    illumination += p.irradiance_coefficients[6].xyz * (n.x * n.x - n.y * n.y) +
                    quadratic +
                    float3(dot(p.irradiance_coefficients[0], n),
                           dot(p.irradiance_coefficients[1], n),
                           dot(p.irradiance_coefficients[2], n));
  }
  float3 lit = (illumination + p.ambient.rgb + p.illumination.rgb) * base.rgb;
  if (features.reflection) {
    const float3 safe_base = base.rgb + 1.0e-32f;
    const float3 normalized_base =
        safe_base / max(max(safe_base.x, safe_base.y), safe_base.z);
    if (p.reflection.y > 0.0f)
      specular.rgb *= 1.0f + p.reflection.y * (normalized_base - 1.0f);
    float weight = p.reflection.x;
    if (features.fresnel) {
      weight *= saturate(pow(abs(1.0f - dot(-direction, normal)), p.fresnel.x) *
                             p.fresnel.y +
                         p.fresnel.z);
    }
    const float luminance =
        dot(base.rgb, float3(0.298912f, 0.586611f, 0.114478f));
    weight *= 1.0f + p.reflection.z * (luminance - 1.0f);
    const float opacity = specular.a * weight;
    const float3 reflection =
        specular.rgb * weight / (opacity + 0.000244140625f);
    lit += saturate(opacity) * (reflection - lit);
  }
  const float distance_fog =
      1.0f - exp2(-max(distance - p.fog_density.x, 0.0f) * p.fog_density.y *
                  1.442695f);
  const float height_gate =
      saturate(input.world_position.y * p.fog_distance.z + p.fog_distance.w);
  const float height_fog =
      saturate(input.world_position.y * p.fog_height.z + p.fog_height.w) *
      p.fog_height.y;
  const float fog = saturate(distance_fog * height_gate + height_fog);
  return float4((lit + fog * (p.fog_color.rgb - lit)) * presentation_opacity,
                base.a);
}
