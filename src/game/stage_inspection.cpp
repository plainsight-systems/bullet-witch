// Build the declared static inspection view from original model/material data.
// Selection uses ordinary static stage objects and sky, supported original
// shaders and original texture slots. The initial camera consumes authored
// player settings, scripted spawn and static clearance; all-group selection
// remains inspection policy. Device residency belongs to the mission; this view
// clears draws before it retires. Algorithm/cost and both-phase E.6/GPU.1
// guidance are in stage_inspection.hpp; no stage decode, image upload or draw
// reconstruction repeats each frame.
// Camera/sky composition also applies both-phase I.1 explicit view policy and
// MEM.9 fixed transform/coefficient storage as cited in the header.
#include "stage_inspection.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <stdexcept>

#include "bw/render/world_material.hpp"
#include "game/camera/initial_camera.hpp"

namespace bw::game {
namespace {
using Vector = std::array<float, 4>;
constexpr std::uint32_t kVariantMask = 0xffff;
constexpr std::uint32_t kInspectionFeatures = 0x00e00000;
constexpr std::uint32_t kBaseTexture = 0x42545830;
constexpr std::uint32_t kNormalTexture = 0x4e4d4150;
constexpr std::uint32_t kSecondaryTexture = 0x42545831;
constexpr std::uint32_t kEnvironmentPower = 0x454e5650;
constexpr std::uint32_t kSourceAlphaBlend = 0x42544e4f;
constexpr std::uint32_t kColor = 0x43434f4c;
constexpr std::uint32_t kFresnel = 0x46525352;
constexpr std::uint32_t kIllumination = 0x4c554d49;
constexpr std::uint32_t kAmbient = 0x414d4249;
constexpr std::uint32_t kReflection = 0x52454650;
constexpr std::uint32_t kSpecular = 0x53504343;
constexpr std::uint32_t kTextureAnimationRecord = 0x54414e4d;
constexpr std::uint32_t kTextureTransformRecord = 0x54525354;
constexpr std::uint32_t kTextureOffset = 0x544f4630;
constexpr std::uint32_t kSecondaryTextureOffset = 0x544f4631;

const Vector* constant(const formats::ModelMaterial& material,
                       std::uint32_t semantic) {
  const auto found = std::ranges::find_if(
      material.constants,
      [semantic](const auto& value) { return value.semantic[0] == semantic; });
  return found == material.constants.end() ? nullptr : &found->value;
}
bool material_parameters(const formats::ModelDefinition& model,
                         const formats::ModelMaterial& material,
                         const render::WorldMaterialRequirements& requirements,
                         render::WorldColorParameters& parameters) {
  const auto first = std::ranges::lower_bound(
      model.records(), material.records.offset, {},
      [](const auto& record) { return record.payload.offset; });
  for (auto record = first;
       record != model.records().end() &&
       record->payload.offset < material.records.offset + material.records.size;
       ++record) {
    if (record->tag == kTextureAnimationRecord ||
        record->tag == kTextureTransformRecord) {
      return false;  // Animated texture coordinates need their original owner.
    }
  }
  struct Field {
    std::uint32_t semantic;
    Vector* output;
    bool required;
  };
  const std::array fields{
      Field{kColor, &parameters.material_color, true},
      Field{kFresnel, &parameters.fresnel, requirements.fresnel},
      Field{kIllumination, &parameters.illumination, true},
      Field{kReflection, &parameters.reflection, requirements.reflection},
      Field{kSpecular, &parameters.specular, requirements.reflection},
      Field{kEnvironmentPower, &parameters.environment_power,
            requirements.environment_map},
      Field{kAmbient, &parameters.ambient, false},
      Field{kTextureOffset, &parameters.texture_offset[0], false},
      Field{kSecondaryTextureOffset, &parameters.texture_offset[1], false}};
  for (const auto& field : fields) {
    const auto* value = constant(material, field.semantic);
    if (value != nullptr) {
      *field.output = *value;
    } else if (field.required) {
      throw std::runtime_error(
          "Supported stage material lacks a required constant");
    }
  }
  // Original constructor/global defaults; the distinct PS0/PS1 initialization
  // witnesses are recorded in world-renderer-state.md.
  parameters.indirect_color = {1, 1, 1, 1};
  parameters.indirect_secondary = {1, 1, 1, 1};
  parameters.position_scale = {1, 1, 1, 1};
  return true;
}
void require_textures(const formats::ModelMaterial& material,
                      const render::WorldMaterialRequirements& requirements) {
  const auto present = [&material](std::uint32_t semantic) {
    return std::ranges::any_of(material.textures,
                               [semantic](const auto& texture) {
                                 return texture.semantic[0] == semantic;
                               });
  };
  // The native pass explicitly supplies its original black cube when a
  // material has no environment binding; present bindings must still reside.
  if (!present(kBaseTexture) ||
      (requirements.normal_map && !present(kNormalTexture)) ||
      (requirements.secondary_base && !present(kSecondaryTexture))) {
    throw std::runtime_error(
        "Supported stage material lacks a required texture");
  }
}
render::WorldBlend material_blend(const formats::ModelMaterial& material) {
  auto blend = render::WorldBlend::opaque;
  for (const auto& state : material.render_states) {
    if (state.semantic != kSourceAlphaBlend) {
      throw std::runtime_error("Unresolved original stage render state");
    }
    blend = render::WorldBlend::source_alpha;
  }
  return blend;
}
// Convert the authored source vector exactly as the original directional-light
// command does. This is scene data, independent of the inspection eye position.
Vector light_direction(const formats::StageEnvironment& environment) {
  constexpr float kDirectionBias = 0.001F;
  const float x = environment.light_vector[0];
  const float y = environment.light_vector[1];
  const float z = environment.light_vector[2] + kDirectionBias;
  const float magnitude = std::sqrt(x * x + y * y + z * z);
  if (!std::isfinite(magnitude) || magnitude == 0) {
    throw std::runtime_error("Invalid authored directional light vector");
  }
  return {-x / magnitude, -y / magnitude, -z / magnitude, 0};
}
render::WorldOutputParameters display_parameters(
    const formats::StageToneMapping& tone) {
  render::WorldOutputParameters result;
  result.tone_mapping_enabled = tone.enabled;
  result.exposure_key = tone.exposure_key;
  result.gamma = tone.gamma;
  result.bloom_enabled = tone.bloom_enabled;
  result.bloom_exposure_key = tone.bloom_exposure_key;
  result.bloom_threshold = tone.bloom_threshold;
  result.bloom_soft_knee = tone.bloom_soft_knee;
  result.exposure = {tone.history_window, tone.adaptation_slew,
                     tone.minimum_luminance, tone.maximum_luminance};
  return result;
}
// Collision ownership follows the original static-object construction path.
// A combined file replaces the separate character/environment registrations.
const formats::CollisionDefinition* collision_input(
    const content::StageDefinition& stage, std::string_view model,
    std::string_view suffix) {
  const auto found = std::ranges::find_if(stage.inputs, [&](const auto& input) {
    std::string_view name = input.name;
    constexpr std::string_view kPrefix = "model.";
    if (!name.starts_with(kPrefix)) return false;
    name.remove_prefix(kPrefix.size());
    return name.starts_with(model) && name.substr(model.size()) == suffix;
  });
  return found != stage.inputs.end() && found->collision ? &*found->collision
                                                         : nullptr;
}
render::ViewTransforms initial_view(
    const content::StageDefinition& stage,
    const std::shared_ptr<memory::Domain>& domain,
    const std::optional<render::ViewPose>& camera_pose) {
  const auto customization = formats::CustomizationDefinition::parse(
      stage.customization_bytes(), domain);
  const auto settings =
      formats::PlayerCameraSettings::parse_initial(customization);
  if (camera_pose) {
    constexpr float kOriginalAspect = 16.0F / 9.0F;
    return render::make_view_transforms(
        *camera_pose, {settings.vertical_fov_radians, kOriginalAspect,
                       settings.near_plane, settings.far_plane});
  }
  scene::StaticCollision collision(domain);
  for (const auto& model : stage.models) {
    if (!model.static_world_object) continue;
    if (const auto* combined = collision_input(stage, model.name, ".hit")) {
      collision.add(*combined, scene::CollisionSourceMode::combined);
      continue;
    }
    if (const auto* character = collision_input(stage, model.name, ".chit")) {
      collision.add(*character, scene::CollisionSourceMode::character);
    }
    if (const auto* environment = collision_input(stage, model.name, ".ehit")) {
      collision.add(*environment, scene::CollisionSourceMode::environment);
    }
  }
  // Fresh One Shot Kill inspection uses the original start() position/facing.
  // Actor setup has not run: this source snapshot must be replaced by its live
  // scripted pose when that owner exists, not presented as an executed binding.
  constexpr ScriptedPlayerPose kFreshMissionPose{{-61.91F, 0.65F, -97.57F},
                                                 -0.28F};
  const auto camera =
      make_initial_player_camera(kFreshMissionPose, settings, collision);
  return render::make_view_transforms(camera.pose, camera.perspective);
}
// Sky follows camera translation but retains its authored world yaw and height.
// Compose that model transform into clip rows; material-space translation then
// cancels the eye, so moving the camera never leaves the sky mesh behind.
render::ViewTransforms sky_view(const render::ViewTransforms& view,
                                const formats::StageEnvironment& environment) {
  const float yaw =
      environment.sky_yaw_degrees * (std::numbers::pi_v<float> / 180.0F);
  const float cosine = std::cos(yaw);
  const float sine = std::sin(yaw);
  const Vector translation{
      -view.camera_relative_world_rows[0][3],
      -view.camera_relative_world_rows[1][3] + environment.sky_height,
      -view.camera_relative_world_rows[2][3], 1};
  render::ViewTransforms result = view;
  result.camera_relative_world_rows = {{{cosine, 0, sine, 0},
                                        {0, 1, 0, environment.sky_height},
                                        {-sine, 0, cosine, 0}}};
  for (std::size_t index = 0; index < view.clip_rows.size(); ++index) {
    const auto& row = view.clip_rows.at(index);
    result.clip_rows.at(index) = {row[0] * cosine - row[2] * sine, row[1],
                                  row[0] * sine + row[2] * cosine,
                                  row[0] * translation[0] +
                                      row[1] * translation[1] +
                                      row[2] * translation[2] + row[3]};
  }
  return result;
}
}  // namespace
StageInspection::StageInspection(
    render::WorldOutput& output, StageViewSource stage_source,
    StageViewSource sky_source, const std::shared_ptr<memory::Domain>& domain,
    const std::optional<render::ViewPose>& camera_pose)
    : output_(output) {
  const auto& stage = stage_source.definition.get();
  if (!stage.environment) {
    throw std::runtime_error("Stage inspection requires authored environment");
  }
  const auto& environment = *stage.environment;
  const auto direction = light_direction(environment);
  const auto view = initial_view(stage, domain, camera_pose);
  const auto sky_transform = sky_view(view, environment);
  if (stage_source.models.size() != stage.models.size() ||
      sky_source.models.size() != sky_source.definition.get().models.size()) {
    throw std::invalid_argument(
        "Stage view residency does not match definitions");
  }
  memory::Vector<render::WorldColorDraw> draws{
      memory::Allocator<render::WorldColorDraw>(domain)};
  std::size_t capacity = sky_source.models.size();
  for (const auto& model : stage.models) {
    if (model.static_world_object) capacity += model.definition.draws().size();
  }
  draws.reserve(capacity);
  const auto append_model = [&](const formats::ModelDefinition& model,
                                render::WorldModelHandle handle, bool sky) {
    if (!output_.geometry_status(handle).texture_bindings_ready) {
      throw std::runtime_error(
          "Stage view requires resident geometry and textures");
    }
    std::size_t selected = 0;
    for (std::size_t index = 0; index < model.draws().size(); ++index) {
      const auto& material =
          model.materials().at(model.draws().at(index).material);
      auto flags = material.shader_selector[0] | kInspectionFeatures;
      const auto variant = static_cast<std::uint16_t>(
          material.shader_selector[1] & kVariantMask);
      auto lighting = render::WorldLightingPolicy::supplied;
      constexpr std::uint32_t kLayeredFresnel = 0x00020882;
      constexpr std::uint32_t kLocalLighting = 0x04200882;
      constexpr std::uint16_t kLayeredVariant = 0x102;
      constexpr std::uint32_t kAlphaSelector = 1;
      const auto surface = material.shader_selector[0] & ~kAlphaSelector;
      if ((surface == kLayeredFresnel && variant == kLayeredVariant) ||
          (surface == kLocalLighting && variant == 1)) {
        // These actual cached programs require scene lighting/shadow inputs.
        // Evaluate their recovered material/vertex contribution explicitly.
        constexpr std::uint32_t kCachedLayered = 0x00820882;
        constexpr std::uint32_t kCachedLocal = 0x04a00882;
        flags = (variant == kLayeredVariant ? kCachedLayered : kCachedLocal) |
                (material.shader_selector[0] & kAlphaSelector);
        lighting = render::WorldLightingPolicy::without_direct_lighting;
      }
      // Runtime irradiance remains enabled for every static object, including
      // families with independently omitted directional or shadow terms.
      constexpr std::uint32_t kSphericalHarmonics = 0x200;
      flags |= kSphericalHarmonics;
      const auto requirements =
          render::world_material_requirements(flags, variant, lighting);
      if (!requirements.supported) continue;
      render::WorldColorDraw draw;
      if (!material_parameters(model, material, requirements,
                               draw.parameters)) {
        continue;
      }
      // The source sky supplies white; ordinary objects use authored stage
      // RGB. Preserve the two CPU normalization steps and float rounding,
      // without substituting these coefficients for material ambient.
      constexpr float kConstantHarmonicScale = 3.544907569885254F;
      constexpr float kConstantPolynomialScale = 0.2820947766304016F;
      for (std::size_t channel = 0; channel < 3; ++channel) {
        const float color = sky ? 1.0F : environment.ambient_color.at(channel);
        const float harmonic = color * kConstantHarmonicScale;
        draw.parameters.irradiance_coefficients.at(channel) = {
            0, 0, 0, harmonic * kConstantPolynomialScale};
      }
      draw.parameters.irradiance_coefficients.back() = {0, 0, 0, 1};
      require_textures(material, requirements);
      const auto& transform = sky ? sky_transform : view;
      draw.parameters.world_rows = transform.camera_relative_world_rows;
      draw.parameters.clip_rows = transform.clip_rows;
      if (lighting == render::WorldLightingPolicy::supplied) {
        draw.parameters.light_diffuse = environment.light_diffuse;
        draw.parameters.light_specular = environment.light_specular;
        draw.parameters.light_direction = direction;
      }
      draw.model = handle;
      draw.draw = index;
      draw.effective_shader_flags = flags;
      draw.lighting = lighting;
      // Original main chunks0/1 are opaque; chunk3 contains all BTNO entries.
      // Preserve source order and the documented default counter-clockwise
      // culling baseline. Full inherited pass scheduling remains separate.
      draw.state = {render::WorldTextureFilter::anisotropic,
                    render::WorldTextureFilter::linear,
                    render::WorldTextureAddress::repeat,
                    render::WorldTextureAddress::repeat,
                    render::WorldCull::counter_clockwise,
                    sky ? render::WorldDepthCompare::always
                        : render::WorldDepthCompare::greater_equal,
                    !sky,
                    material_blend(material),
                    4};
      draws.push_back(draw);
      ++selected;
    }
    return selected;
  };
  // Sky supplies the original textured backdrop. It writes no depth so the
  // main geometry can occlude it regardless of the inspection eye position.
  for (std::size_t index = 0; index < sky_source.models.size(); ++index) {
    sky_draws_ +=
        append_model(sky_source.definition.get().models.at(index).definition,
                     sky_source.models[index], true);
  }
  // The original stage constructor instantiates every ordinary namespace,
  // including background, detail and cave models. Behavior-owned props remain
  // with their later world owners; overlapping bounds do not imply a LOD
  // choice.
  for (std::size_t index = 0; index < stage.models.size(); ++index) {
    const auto& model = stage.models.at(index);
    if (!model.static_world_object) continue;
    const auto count =
        append_model(model.definition, stage_source.models[index], false);
    selected_draws_ += count;
    omitted_draws_ += model.definition.draws().size() - count;
  }
  if (selected_draws_ == 0 || sky_draws_ == 0) {
    throw std::runtime_error(
        "Static stage view requires supported world and sky draws");
  }
  // Publish only after the complete view validates. Device storage stays with
  // the mission, so view construction neither duplicates nor releases assets.
  output_.set_world_output_parameters(
      display_parameters(environment.tone_mapping));
  output_.set_world_draws(draws);
}
StageInspection::~StageInspection() { output_.clear_world_draws(); }
}  // namespace bw::game
