// Transform original static geometry and evaluate the recovered surface
// families: base and layered textures, geometric/normal-map normals, vertex
// illumination, directional diffuse/specular, Fresnel/cubemap reflection and
// distance/height fog. Optional branches are original program features selected
// by the shared material requirements, not fallback shading. Registry/source
// mappings and numerical limitations live in world-renderer-foundation.md.
// Presentation fade is separate. Shadow footprint correction is an explicit
// native reconstruction inference from pinned Unreal VSM directional sampling;
// it preserves the original projection, jitter taps and caster geometry. See
// stage-shadow-presentation.md for the parity boundary and device evidence.
#include <metal_stdlib>
using namespace metal;
struct WorldLocalLight {
  float4 position, diffuse, specular, attenuation;
};
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
  float4 shadow_clip_rows[4];
  float4 shadow_parameters;
  WorldLocalLight local_lights[4];
  uint4 local_light_meta;
};
struct WorldColorStream {
  uint stride, position, normal, tangent, uv, color;
  uint secondary_uv, modulation_color;
  uint normal_map, secondary_base, environment_map, vertex_color;
  uint vertex_illumination, reflection, fresnel, directional_light;
  uint alpha_cutout, spherical_harmonics, base_texture;
  uint joint_indices, joint_weights, joint_influences;
  uint position_stride, ambient_occlusion;
  uint secondary_alpha_blend, alpha_fresnel, local_lights;
  uint environment_shadow;
};
struct WorldJointMatrix {
  float4 rows[4];
};
struct WorldColorVertex {
  float4 position [[position]];
  float3 world_position;
  float4 shadow_position;
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
float4 joint_transform(float4 value, constant WorldJointMatrix& matrix) {
  return float4(dot(value, matrix.rows[0]), dot(value, matrix.rows[1]),
                dot(value, matrix.rows[2]), dot(value, matrix.rows[3]));
}
// Preserve source influence count, weight conversion and accumulation order.
float4 skin_value(float4 value, uchar4 indices, float4 weights, uint count,
                  constant WorldJointMatrix* joints) {
  const float4 first = joint_transform(value, joints[indices.x]);
  if (count == 1) return first;
  const float4 second = joint_transform(value, joints[indices.y]);
  float4 result = second * weights.y + first * weights.x;
  if (count == 2) return result;
  result += joint_transform(value, joints[indices.z]) * weights.z;
  return result + joint_transform(value, joints[indices.w]) * weights.w;
}
WorldColorVertex transform_world_vertex(uint vertex_id,
                                        device const uchar* vertices,
                                        device const uchar* positions,
                                        constant WorldColorParameters& p,
                                        constant WorldColorStream& layout,
                                        constant WorldJointMatrix* joints) {
  const device uchar* source = vertices + vertex_id * layout.stride;
  const float3 position = float3(*reinterpret_cast<const device packed_float3*>(
      positions + vertex_id * layout.position_stride + layout.position));
  float4 position4 =
      float4(position, 1.0f) * p.position_scale + p.position_offset;
  uchar4 joint_indices = 0;
  float4 joint_weights = float4(1, 0, 0, 0);
  if (layout.joint_influences != 0) {
    joint_indices =
        *reinterpret_cast<const device uchar4*>(source + layout.joint_indices);
    if (layout.joint_influences > 1) {
      joint_weights = float4(*reinterpret_cast<const device uchar4*>(
                          source + layout.joint_weights)) /
                      255.0f;
    }
    position4 = skin_value(position4, joint_indices, joint_weights,
                           layout.joint_influences, joints);
  }
  WorldColorVertex output;
  output.position =
      float4(dot(position4, p.clip_rows[0]), dot(position4, p.clip_rows[1]),
             dot(position4, p.clip_rows[2]), dot(position4, p.clip_rows[3]));
  output.shadow_position = float4(dot(position4, p.shadow_clip_rows[0]),
                                  dot(position4, p.shadow_clip_rows[1]),
                                  dot(position4, p.shadow_clip_rows[2]),
                                  dot(position4, p.shadow_clip_rows[3]));
  output.world_position =
      float3(dot(position4, p.world_rows[0]), dot(position4, p.world_rows[1]),
             dot(position4, p.world_rows[2]));
  output.normal = 0.0f;
  output.tangent = 0.0f;
  if (layout.directional_light || layout.spherical_harmonics ||
      layout.local_lights) {
    float3 normal = decode_direction(source + layout.normal);
    if (layout.joint_influences != 0) {
      normal = skin_value(float4(normal, 0), joint_indices, joint_weights,
                          layout.joint_influences, joints)
                   .xyz;
    }
    output.normal = float3(dot(normal, p.world_rows[0].xyz),
                           dot(normal, p.world_rows[1].xyz),
                           dot(normal, p.world_rows[2].xyz));
  }
  if (layout.normal_map) {
    float3 tangent = decode_direction(source + layout.tangent);
    if (layout.joint_influences != 0) {
      tangent = skin_value(float4(tangent, 0), joint_indices, joint_weights,
                           layout.joint_influences, joints)
                    .xyz;
    }
    output.tangent = float3(dot(tangent, p.world_rows[0].xyz),
                            dot(tangent, p.world_rows[1].xyz),
                            dot(tangent, p.world_rows[2].xyz));
  }
  output.coverage_uv = 0.0f;
  if (layout.base_texture) {
    output.coverage_uv = float2(
        *reinterpret_cast<const device packed_half2*>(source + layout.uv));
  }
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
vertex WorldColorVertex world_color_vertex(
    uint vertex_id [[vertex_id]], device const uchar* vertices [[buffer(0)]],
    constant WorldColorParameters& p [[buffer(1)]],
    constant WorldColorStream& layout [[buffer(2)]],
    constant WorldJointMatrix* joints [[buffer(3)]],
    device const uchar* positions [[buffer(4)]]) {
  return transform_world_vertex(vertex_id, vertices, positions, p, layout,
                                joints);
}
// Evaluate derivatives before cutout discard or divergent receiver rejection.
// A projected triangle is a depth plane in shadow UV; its screen derivatives
// recover that plane even under the original perspective light warp.
float2 shadow_depth_slope(float4 shadow_position) {
  const float safe_w = shadow_position.w == 0.0f ? 1.0f : shadow_position.w;
  const float3 projected = shadow_position.xyz / safe_w;
  const float3 uv_depth(projected.x * 0.5f + 0.5f, 0.5f - projected.y * 0.5f,
                        projected.z);
  float3 dx = dfdx(uv_depth), dy = dfdy(uv_depth);
  if (!all(isfinite(dx)) || !all(isfinite(dy))) return 0.0f;
  // A common scale leaves the solved plane unchanged while avoiding overflow
  // in cross products and underflow for uniformly tiny projected triangles.
  const float3 magnitude = max(abs(dx), abs(dy));
  const float scale = max(magnitude.x, max(magnitude.y, magnitude.z));
  if (scale == 0.0f) return 0.0f;
  dx /= scale;
  dy /= scale;
  const float determinant = dx.x * dy.y - dx.y * dy.x;
  if (determinant == 0.0f) return 0.0f;
  const float2 slope =
      float2(dx.z * dy.y - dy.z * dx.y, dy.z * dx.x - dx.z * dy.x) /
      determinant;
  // Unreal's normalized-depth slope bound prevents near-degenerate planes
  // from turning a subtexel correction into an unbounded shadow displacement.
  return clamp(slope, float2(-0.05f), float2(0.05f));
}
// Four original jitter slices supply two point-depth comparisons each. The
// immutable buffer uses the same wrapped point lookup as the source volume.
// The native pass protects a zero-cleared border sized for this filter's full
// footprint. Point clamping therefore repeats unoccluded far depth, never an
// edge caster; projection reserves the same guard. This is the explicit
// Unreal-informed baseline, not an interpretation of raw source address6.
float directional_visibility(WorldColorVertex input, float2 depth_slope,
                             constant WorldColorParameters& p,
                             texture2d<float> shadow_map,
                             device const uchar4* jitter) {
  if (p.shadow_parameters.x == 0.0f || input.shadow_position.w == 0.0f)
    return 1.0f;
  const float3 projected = input.shadow_position.xyz / input.shadow_position.w;
  const float2 uv =
      float2(projected.x * 0.5f + 0.5f, 0.5f - projected.y * 0.5f);
  if (any(uv < 0.0f) || any(uv > 1.0f)) return 1.0f;
  constexpr sampler depth_sampler(coord::normalized, address::clamp_to_edge,
                                  filter::nearest);
  const uint2 cell = uint2(input.position.xy) % 32;
  const float2 dimensions(shadow_map.get_width(), shadow_map.get_height());
  float visible = 0.0f;
  for (uint slice = 0; slice < 4; ++slice) {
    const float4 offsets =
        float4(jitter[(slice * 32 + cell.y) * 32 + cell.x]) / 255.0f * 2.0f -
        1.0f;
    const float first =
        shadow_map
            .sample(depth_sampler, uv + offsets.xy * p.shadow_parameters.z)
            .x;
    const float second =
        shadow_map
            .sample(depth_sampler, uv + offsets.zw * p.shadow_parameters.z)
            .x;
    const float receiver = projected.z + p.shadow_parameters.y;
    // Compare against the receiver plane at the texel actually sampled, not
    // the center ray. The UE directional optimal-bias rule leaves already-lit
    // offsets unchanged and covers positive depth error conservatively.
    const float2 first_center =
        (clamp(floor((uv + offsets.xy * p.shadow_parameters.z) * dimensions),
               float2(0), dimensions - 1.0f) +
         0.5f) /
        dimensions;
    const float2 second_center =
        (clamp(floor((uv + offsets.zw * p.shadow_parameters.z) * dimensions),
               float2(0), dimensions - 1.0f) +
         0.5f) /
        dimensions;
    const float first_bias =
        2.0f * max(0.0f, dot(depth_slope, first_center - uv));
    const float second_bias =
        2.0f * max(0.0f, dot(depth_slope, second_center - uv));
    visible += float(receiver + first_bias >= first) +
               float(receiver + second_bias >= second);
  }
  return visible * 0.125f;
}
// The caster shares exact position/palette and raw-UV cutout coverage with
// the color draw, while publishing only reversed depth to the retained map.
vertex WorldColorVertex world_shadow_vertex(
    uint vertex_id [[vertex_id]], device const uchar* vertices [[buffer(0)]],
    constant WorldColorParameters& p [[buffer(1)]],
    constant WorldColorStream& layout [[buffer(2)]],
    constant WorldJointMatrix* joints [[buffer(3)]],
    device const uchar* positions [[buffer(4)]]) {
  WorldColorVertex output =
      transform_world_vertex(vertex_id, vertices, positions, p, layout, joints);
  output.position = output.shadow_position;
  return output;
}
fragment float world_shadow_fragment(WorldColorVertex input [[stage_in]],
                                     constant WorldColorParameters& p
                                     [[buffer(1)]],
                                     constant WorldColorStream& features
                                     [[buffer(2)]],
                                     texture2d<float> base_texture
                                     [[texture(0)]],
                                     sampler material_sampler [[sampler(0)]]) {
  if (features.alpha_cutout &&
      p.material_color.a *
              base_texture.sample(material_sampler, input.coverage_uv).a <
          0.5f)
    discard_fragment();
  return input.position.z;
}
fragment float4
world_color_fragment(WorldColorVertex input [[stage_in]],
                     constant float& presentation_opacity [[buffer(0)]],
                     constant WorldColorParameters& p [[buffer(1)]],
                     constant WorldColorStream& features [[buffer(2)]],
                     texture2d<float> base_texture [[texture(0)]],
                     texture2d<float> secondary_texture [[texture(1)]],
                     texture2d<float> normal_texture [[texture(3)]],
                     texture2d<float> occlusion_texture [[texture(5)]],
                     texturecube<float> environment_texture [[texture(7)]],
                     sampler material_sampler [[sampler(0)]],
                     sampler environment_sampler [[sampler(7)]],
                     texture2d<float> shadow_map [[texture(13)]],
                     device const uchar4* jitter [[buffer(4)]]) {
  // This condition is uniform for a draw; cutout coverage below is not.
  const float2 depth_slope =
      features.directional_light && p.shadow_parameters.x != 0.0f
          ? shadow_depth_slope(input.shadow_position)
          : float2(0);
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
  // The original untextured program has no base sampler or UV input.
  float4 sampled_base = 1.0f;
  if (features.base_texture) {
    sampled_base = base_texture.sample(material_sampler, input.uv.xy);
  }
  if (features.secondary_base) {
    const float4 secondary =
        secondary_texture.sample(material_sampler, input.uv.zw) *
        p.indirect_secondary;
    // Clothing interpolates two base colors; the stage layer instead
    // modulates its first base. These are separate original programs.
    const float3 layered = features.secondary_alpha_blend
                               ? secondary.rgb
                               : sampled_base.rgb * secondary.rgb;
    sampled_base.rgb += secondary.a * (layered - sampled_base.rgb);
  }
  float4 base =
      p.material_color * input.modulation * p.indirect_color * sampled_base;
  float3 illumination = input.illumination;
  float4 specular = 0.0f;
  const float distance = length(input.world_position);
  const float3 direction = input.world_position / distance;
  if (features.alpha_fresnel) {
    base.a *= saturate(pow(abs(1.0f - dot(-direction, normal)), p.fresnel.x) *
                           p.fresnel.y +
                       p.fresnel.z);
  }
  float facing = 0.0f;
  float directional_visibility_weight = 1.0f;
  if (features.directional_light) {
    const float visibility =
        directional_visibility(input, depth_slope, p, shadow_map, jitter);
    directional_visibility_weight = visibility;
    facing = saturate(dot(normal, -p.light_direction.xyz));
    if (facing > 0.0f) {
      illumination += p.light_diffuse.xyz * facing * visibility;
      if (features.reflection) {
        const float3 halfway =
            original_normalize(-direction - p.light_direction.xyz);
        const float response =
            pow(saturate(dot(normal, halfway)), p.specular.x) * p.specular.y *
            p.light_specular.w;
        specular = response * p.light_specular * visibility;
      }
    }
  }
  // Original lightsListL loop: point attenuation, componentwise diffuse
  // threshold, and separately accumulated specular alpha. Keep world emitter
  // positions in the same camera-relative space as the skinned surface. These
  // lights do not inherit the directional shadow or ambient-occlusion factor.
  if (features.local_lights) {
    float4 local_specular = 0.0f;
    for (uint i = 0; i < p.local_light_meta.x; ++i) {
      const WorldLocalLight light = p.local_lights[i];
      const float4 world_position = float4(light.position.xyz, 1.0f);
      const float3 relative_position =
          float3(dot(world_position, p.world_rows[0]),
                 dot(world_position, p.world_rows[1]),
                 dot(world_position, p.world_rows[2]));
      const float3 delta = relative_position - input.world_position;
      const float3 light_direction = original_normalize(delta);
      const float local_facing = saturate(dot(normal, light_direction));
      if (local_facing > 0.0f) {
        const float attenuation =
            1.0f /
            (pow(length(delta), light.attenuation.x) * light.attenuation.y +
             light.attenuation.z);
        illumination += max(light.diffuse.rgb * (local_facing * attenuation) -
                                light.attenuation.w,
                            0.0f);
        if (features.reflection) {
          const float3 halfway =
              original_normalize(light_direction - direction);
          const float response =
              pow(saturate(dot(normal, halfway)), p.specular.x) * attenuation *
              p.specular.y;
          local_specular += light.specular * response;
        }
      }
    }
    local_specular.a = saturate(local_specular.a);
    specular += local_specular;
  }
  if (features.environment_map) {
    const float3 reflected =
        direction - normal * (2.0f * dot(direction, normal));
    const float4 environment =
        environment_texture.sample(environment_sampler, reflected) *
        p.environment_power;
    // Actor/background receiver programs share directional visibility here;
    // original static environment programs use unshadowed facing instead.
    // This equation is independent of the direct diffuse/specular policy.
    const float environment_visibility =
        features.environment_shadow ? directional_visibility_weight : 1.0f;
    specular += environment *
                saturate(facing * environment_visibility + p.reflection.w);
  }
  float3 ambient = p.ambient.rgb;
  if (features.spherical_harmonics) {
    // Evaluate the original packed irradiance polynomial at the surface normal.
    // Constant material ambience remains a separate contribution.
    const float4 n = float4(normal, 1.0f);
    const float4 products = n.yzzx * n.xyzz;
    const float3 quadratic =
        float3(dot(p.irradiance_coefficients[3], products),
               dot(p.irradiance_coefficients[4], products),
               dot(p.irradiance_coefficients[5], products));
    ambient += p.irradiance_coefficients[6].xyz * (n.x * n.x - n.y * n.y) +
               quadratic +
               float3(dot(p.irradiance_coefficients[0], n),
                      dot(p.irradiance_coefficients[1], n),
                      dot(p.irradiance_coefficients[2], n));
  }
  // Original AMOC red scales ambient/irradiance, while green scales the final
  // Fresnel/reflection weight. Fetch once; neither channel changes direct
  // diffuse or self illumination. This is the recovered material, not SSAO.
  float reflection_occlusion = 1.0f;
  if (features.ambient_occlusion) {
    const float2 occlusion =
        occlusion_texture.sample(material_sampler, input.uv.xy).rg;
    ambient *= occlusion.r;
    reflection_occlusion = occlusion.g;
  }
  float3 lit = (illumination + ambient + p.illumination.rgb) * base.rgb;
  if (features.reflection) {
    const float3 safe_base = base.rgb + 1.0e-32f;
    const float3 normalized_base =
        safe_base / max(max(safe_base.x, safe_base.y), safe_base.z);
    if (p.reflection.y > 0.0f)
      specular.rgb *= 1.0f + p.reflection.y * (normalized_base - 1.0f);
    float weight = 1.0f;
    if (features.fresnel) {
      weight = saturate(pow(abs(1.0f - dot(-direction, normal)), p.fresnel.x) *
                            p.fresnel.y +
                        p.fresnel.z);
    }
    weight *= reflection_occlusion;
    weight *= p.reflection.x;
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
