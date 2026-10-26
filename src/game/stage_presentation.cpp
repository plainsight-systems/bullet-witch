// Slice 2 implementation E.6 (#re-raii) and MEM.9 retain prepared receiver
// storage and transactional native appearance replacement. LightingPublication
// owns sample packing/distribution; this adapter supplies authored
// observations. Implement the owner contract in the matching header. Design
// I.1/C.9 keeps authoritative state private; implementation E.6 (#re-raii)
// preserves cleanup on failed preparation. Both-phase MEM.9
// (guidelines/memory/MEM.9-embedded-allocate-at-init.md) keeps retained buffers
// fixed during admitted frames. Existing source algorithms and ordering remain.
#include "stage_presentation.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <numbers>
#include <stdexcept>

#include "bw/render/world_material.hpp"
#include "formats/reader.hpp"
#include "placed_lighting.hpp"
#include "stage_shadow.hpp"
namespace bw::game {
namespace {
using Vector = std::array<float, 4>;
scene::LocalLightSettings local_light_settings(
    const formats::CustomizationDefinition& customization) {
  constexpr std::array<std::string_view, 3> kSetupPath{"witch", "general",
                                                       "setup"};
  const resources::detail::Reader source{customization.lookup(kSetupPath)};
  constexpr std::size_t kThreshold = 0x64, kMargin = 0x68,
                        kMaximumDirect = 0x6c, kOverflow = 0x70;
  constexpr std::uint32_t kByteMask = 0xff;
  return {std::bit_cast<float>(source.u32(kThreshold)),
          std::bit_cast<float>(source.u32(kMargin)), source.u32(kMaximumDirect),
          (source.u32(kOverflow) & kByteMask) != 0};
}

constexpr std::uint32_t kVariantMask = 0xffff;
constexpr std::uint32_t kFogFeature = 0x00800000;
constexpr std::size_t kJointPaletteCapacity = 32;
constexpr std::uint32_t kGroupBitsPerWord = 32;
constexpr std::uint32_t kSourceGroupCount = 128;
constexpr std::uint32_t kBaseTexture = 0x42545830;
constexpr std::uint32_t kNormalTexture = 0x4e4d4150;
constexpr std::uint32_t kSecondaryTexture = 0x42545831;
constexpr std::uint32_t kEnvironmentPower = 0x454e5650;
constexpr std::uint32_t kSourceAlphaBlend = 0x42544e4f;
constexpr std::uint32_t kColor = 0x43434f4c;
constexpr std::uint32_t kFresnel = 0x46525352, kAlphaFresnel = 0x46525341;
constexpr std::uint32_t kIllumination = 0x4c554d49;
constexpr std::uint32_t kAmbient = 0x414d4249;
constexpr std::uint32_t kReflection = 0x52454650;
constexpr std::uint32_t kSpecular = 0x53504343;
constexpr std::uint32_t kTextureOffset = 0x544f4630;
constexpr std::uint32_t kSecondaryTextureOffset = 0x544f4631;

const Vector* constant(const formats::ModelMaterial& material,
                       std::uint32_t semantic) {
  const auto found = std::ranges::find_if(
      material.constants,
      [semantic](const auto& value) { return value.semantic[0] == semantic; });
  return found == material.constants.end() ? nullptr : &found->value;
}
void material_parameters(const formats::ModelMaterial& material,
                         const render::WorldMaterialRequirements& requirements,
                         render::WorldColorParameters& parameters) {
  struct Field {
    std::uint32_t semantic;
    Vector* output;
    bool required;
  };
  const std::array fields{
      Field{kColor, &parameters.material_color, true},
      Field{requirements.alpha_fresnel ? kAlphaFresnel : kFresnel,
            &parameters.fresnel,
            requirements.fresnel || requirements.alpha_fresnel},
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
      throw std::runtime_error("Supported stage material lacks constant " +
                               std::to_string(field.semantic) +
                               " for selector " +
                               std::to_string(material.shader_selector[0]));
    }
  }
  // Original constructor/global defaults; the distinct PS0/PS1 initialization
  // witnesses are recorded in world-renderer-state.md.
  parameters.indirect_color = {1, 1, 1, 1};
  parameters.indirect_secondary = {1, 1, 1, 1};
  parameters.position_scale = {1, 1, 1, 1};
}
// The source stage activates its first material clip. Bind labels to model
// slots by name; unmatched clip labels do not write another material's slot.
render::MaterialFloat2Curve material_curve(
    const content::StageDefinition& stage, std::string_view name) {
  std::optional<render::MaterialFloat2Curve> result;
  for (const auto& list : stage.inputs) {
    if (list.animations.empty()) continue;
    if (list.labels.empty()) {
      throw std::runtime_error("Stage material animation has no label table");
    }
    const auto& animation =
        stage.inputs.at(list.animations.front()).material_animation;
    const auto& labels = stage.inputs.at(list.labels.front()).material_labels;
    if (!animation || !labels) {
      throw std::runtime_error(
          "Stage material animation definitions are missing");
    }
    for (std::size_t index = 0; index < labels->count; ++index) {
      if (labels->name(index) != name) continue;
      if (result) {
        throw std::runtime_error("Stage material animation label is ambiguous");
      }
      result =
          render::MaterialFloat2Curve::bind(*animation, labels->track(index));
    }
  }
  if (!result) {
    throw std::runtime_error("Stage material animation slot is unbound");
  }
  return *result;
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
  if ((requirements.base_texture && !present(kBaseTexture)) ||
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
// Original model chunks enter separate scene command lists. Keep this disk
// compatibility mapping here; native rendering consumes descriptive phases.
render::WorldColorPhase color_phase(std::uint32_t chunk, bool sky) {
  if (sky) return render::WorldColorPhase::background;
  switch (chunk) {
    case 0:
      return render::WorldColorPhase::opaque;
    case 1:
      return render::WorldColorPhase::masked;
    case 2:
      return render::WorldColorPhase::translucent;
    case 3:
      return render::WorldColorPhase::material_blended;
    default:
      throw std::runtime_error("Unresolved original model color phase");
  }
}
// Pack the original distance and height fog in camera-relative world axes.
// Disabled controls use their source defaults; reversed ranges remain valid.
void fog_parameters(const formats::StageEnvironment& environment,
                    float eye_height, bool sky,
                    render::WorldColorParameters& parameters) {
  const auto& fog = sky ? environment.sky_fog : environment.surface_fog;
  constexpr float kDisabledStart = 2499, kDisabledEnd = 2500;
  float start = fog.distance_enabled ? fog.distance_start : kDisabledStart;
  auto gate = fog.height_gate_enabled
                  ? fog.height_gate
                  : std::array{kDisabledStart, kDisabledEnd};
  auto height = fog.height_enabled ? fog.height_range
                                   : std::array{kDisabledStart, kDisabledEnd};
  if (sky) {
    constexpr float kSkyFogScale = .01F;
    start *= kSkyFogScale;
    for (auto& value : gate) value = (eye_height + value) * kSkyFogScale;
    for (auto& value : height) value = (eye_height + value) * kSkyFogScale;
  }
  const float gate_span = gate[1] - gate[0];
  const float height_span = height[1] - height[0];
  parameters.fog_color = {environment.fog_color[0], environment.fog_color[1],
                          environment.fog_color[2], 1};
  // Admitted source mode-1 programs use only the height-gate lanes here.
  // AREA can set equal linear-distance endpoints: the original uploads unused
  // infinities/NaNs to xy, while exponential fog uses start/density below.
  // Keep unused lanes explicitly zero, never perturb the active denominator.
  parameters.fog_distance = {0, 0, -1 / gate_span,
                             (gate[1] - eye_height) / gate_span};
  parameters.fog_height = {0, fog.height_enabled ? fog.height_opacity : 0,
                           -1 / height_span,
                           (height[1] - eye_height) / height_span};
  parameters.fog_density = {start, fog.distance_enabled ? fog.density : 0, 0,
                            0};
  for (const auto& values : {parameters.fog_distance, parameters.fog_height}) {
    if (!std::ranges::all_of(
            values, [](float value) { return std::isfinite(value); })) {
      throw std::runtime_error(
          "Authored fog coefficients are not representable");
    }
  }
}
render::WorldOutputParameters display_parameters(
    const formats::StageToneMapping& tone) {
  render::WorldOutputParameters result;
  result.tone_mapping_enabled = tone.enabled;
  result.exposure_key = tone.exposure_key;
  result.gamma = tone.gamma;
  result.global_color_mode = tone.color_filter_mode;
  result.global_color = {tone.color_filter[0], tone.color_filter[1],
                         tone.color_filter[2], 1};
  result.bloom_enabled = tone.bloom_enabled;
  result.bloom_exposure_key = tone.bloom_exposure_key;
  result.bloom_threshold = tone.bloom_threshold;
  result.bloom_soft_knee = tone.bloom_soft_knee;
  result.exposure = {tone.history_window, tone.adaptation_slew,
                     tone.minimum_luminance, tone.maximum_luminance};
  return result;
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
// Material preparation and appearance replacement share identical stage
// illumination, fog and view policy; only their material inputs differ.
void environment_parameters(const formats::StageEnvironment& environment,
                            const render::ViewTransforms& transform, bool sky,
                            const scene::DirectionalLighting& light,
                            float eye_height,
                            render::WorldColorParameters& parameters) {
  fog_parameters(environment, eye_height, sky, parameters);
  parameters.world_rows = transform.camera_relative_world_rows;
  parameters.clip_rows = transform.clip_rows;
  parameters.light_diffuse = light.diffuse;
  parameters.light_specular = light.specular;
  parameters.light_direction = light.direction;
}
scene::LocalLightViewQuery lighting_view(const InitialPlayerCamera& camera) {
  const auto& pose = camera.pose;
  const auto& perspective = camera.perspective;
  const float height =
      std::tan(perspective.vertical_fov_radians * .5F) * perspective.near_plane;
  const float width = height * perspective.aspect;
  return {pose.eye, math::make_perspective_frustum(
                        {pose.right, pose.up, pose.forward, pose.eye},
                        {-width, width, -height, height, perspective.near_plane,
                         perspective.far_plane})};
}
}  // namespace
StagePresentation::StagePresentation(
    render::WorldPresentation& output, StageViewSource stage_source,
    StageViewSource sky_source, StageBackgroundSource background,
    StageCharacterLightSource light,
    const formats::CustomizationDefinition& customization,
    const PlayerActor* player, const StageCameraAdmission& cameras,
    bool force_player_visibility, const std::shared_ptr<memory::Domain>& domain)
    : output_(output),
      player_(player),
      memory_(domain),
      placed_lighting_(memory::Allocator<scene::ReceiverPublication>(domain)),
      static_shadow_updates_(
          memory::Allocator<render::WorldVisibilityUpdate>(domain)),
      animated_curves_(memory::Allocator<AnimatedCurve>(domain)),
      animated_materials_(memory::Allocator<AnimatedMaterial>(domain)),
      texture_updates_(
          memory::Allocator<render::WorldTextureOffsetUpdate>(domain)) {
  if ((player || (background.placements != nullptr &&
                  !background.placements->placements().empty())) &&
      light.character == nullptr) {
    throw std::invalid_argument(
        "Spatial receivers require authored character light field");
  }
  const auto& stage = stage_source.definition.get();
  if (!stage.environment) {
    throw std::runtime_error("Stage inspection requires authored environment");
  }
  environment_ = *stage.environment;
  const auto& environment = *environment_;
  constexpr std::array<std::string_view, 3> kSetupPath{"witch", "general",
                                                       "setup"};
  const resources::detail::Reader setup{customization.lookup(kSetupPath)};
  constexpr std::size_t kOpaquePreparation = 0x24, kMaskedPreparation = 0x28;
  scene_passes_ = {setup.u32(kOpaquePreparation) == 1,
                   setup.u32(kMaskedPreparation) == 1,
                   true,
                   {}};
  environment_channels_ = prepare_stage_environment(
      stage, domain, cameras.initial.perspective.far_plane);
  memory::Vector<scene::LocalLight> emitters{
      memory::Allocator<scene::LocalLight>(domain)};
  if (background.placements != nullptr) {
    emitters.reserve(background.placements->placements().size());
  }
  // The original placed emitter admission requires the same matched placement
  // and authored physics record as contact preparation, but owns no contact
  // data.
  if (background.placements != nullptr) {
    const auto& placements = *background.placements;
    for (const auto& model : background.models) {
      const auto name = std::ranges::find(placements.names(), model.name);
      if (name == placements.names().end() ||
          model.physics_collision == nullptr) {
        continue;
      }
      const auto model_index =
          static_cast<std::size_t>(name - placements.names().begin());
      for (const auto& placement : placements.placements()) {
        if (placement.model != model_index) continue;
        const auto body_world =
            std::span(placements.matrices())
                .subspan(placement.first_matrix, placement.matrix_count);
        // These bodies are frozen initial placements. Admit only their authored
        // emitter flag, and reject any unavailable owner-transform fallback.
        const auto& skeleton = model.definition.get().skeleton();
        const auto joint_count = skeleton ? skeleton->bones().size() : 0;
        if (auto emitter = prepare_placed_light(
                *model.physics_collision, body_world, nullptr, joint_count)) {
          emitters.push_back(*emitter);
        }
      }
    }
  }
  auto camera = cameras.initial;
  if (environment_channels_) {
    // Scene-ready startup requests one immediate environment publication.
    // Region transitions thereafter use the source per-world-update blend.
    environment_channels_->update(camera.pose.eye, true);
    environment_ =
        stage_environment_snapshot(environment, *environment_channels_);
    camera.perspective.far_plane =
        stage_environment_camera_far(*environment_channels_);
  }
  initial_far_distance_ = camera.perspective.far_plane;
  std::array<scene::SceneLightFieldSource, 2> fields{};
  std::size_t field_count = 0;
  if (light.character != nullptr) {
    character_field_ = {field_count};
    fields.at(field_count++) = {light.character->bytes(),
                                environment.character_light_intensity};
  }
  if (light.enemy != nullptr) {
    enemy_field_ = {field_count};
    fields.at(field_count++) = {light.enemy->bytes(),
                                environment.enemy_light_intensity};
  }
  // The original mandatory cube-zero gate is independent of camera AREA
  // publication. Prepare receiver-space region/table lookup once, retaining
  // native handles only in this scene-to-render adapter.
  const formats::AreaDefinition* reflection_regions = nullptr;
  const formats::SectionTable* reflection_table = nullptr;
  for (const auto& input : stage.inputs) {
    if (input.name.ends_with(".areadata.bin") && input.area) {
      reflection_regions = &*input.area;
    }
    if (input.name.ends_with(".areadata.settbl") && input.table) {
      reflection_table = &*input.table;
    }
  }
  scene::ReflectionSlotResidency reflection_residency{};
  scene::ReflectionBindings reflection_textures{};
  for (std::size_t slot = 0; slot < stage.environment_textures.size(); ++slot) {
    const auto index = stage.environment_textures.at(slot);
    if (!index) continue;
    if (*index >= stage_source.textures.size() ||
        stage_source.textures[*index].value == 0) {
      throw std::invalid_argument("Scene reflection texture is not resident");
    }
    reflection_textures.at(slot) = stage_source.textures[*index];
    reflection_residency.at(slot) = true;
  }
  if (reflection_regions != nullptr && reflection_table != nullptr) {
    reflection_field_.emplace(scene::AreaRegions(*reflection_regions, domain),
                              *reflection_table, reflection_residency, domain);
  } else if (reflection_residency[0]) {
    throw std::invalid_argument(
        "Configured reflection field lacks receiver regions/table");
  }
  lighting_.emplace(
      scene::SceneLightingSource{
          std::span(fields).first(field_count),
          emitters,
          local_light_settings(customization),
          {environment.ambient_color,
           {environment.light_vector, environment.light_diffuse,
            environment.light_specular}},
          reflection_field_ ? &*reflection_field_ : nullptr},
      reflection_textures, output_.lighting_publisher(), domain);
  const auto directional = lighting_->directional_lighting();
  const auto& direction = directional.direction;
  lighting_->set_view(lighting_view(camera));
  const auto sky_lighting =
      lighting_->evaluate({.policy = scene::ReceiverLightingPolicy::sky_white});
  const auto view =
      render::make_view_transforms(camera.pose, camera.perspective);
  const auto sky_transform = sky_view(view, environment);
  if (stage_source.models.size() != stage.models.size() ||
      sky_source.models.size() != sky_source.definition.get().models.size()) {
    throw std::invalid_argument(
        "Stage view residency does not match definitions");
  }
  memory::Vector<render::WorldColorDraw> draws{
      memory::Allocator<render::WorldColorDraw>(domain)};
  std::size_t capacity = sky_source.models.size();
  if (player_) {
    capacity += player_->source().body.definition.get().draws().size() +
                player_->source().weapon.definition.get().draws().size();
  }
  for (const auto& model : stage.models) {
    if (model.static_world_object) capacity += model.definition.draws().size();
  }
  const auto background_model =
      [&](std::size_t index) -> const ResidentModelView& {
    const auto name = background.placements->names().at(index);
    const auto found =
        std::ranges::find(background.models, name, &ResidentModelView::name);
    if (found == background.models.end()) {
      throw std::runtime_error("Placed background model is not resident");
    }
    return *found;
  };
  std::size_t background_capacity = 0;
  if (background.placements != nullptr) {
    for (const auto& placement : background.placements->placements()) {
      background_capacity +=
          background_model(placement.model).definition.get().draws().size();
    }
  }
  draws.reserve(capacity + background_capacity);
  static_shadow_updates_.reserve(capacity);
  static_lighting_.emplace(lighting_->prepare_receiver(
      {.policy = scene::ReceiverLightingPolicy::stage_ambient}, capacity));
  if (background.placements != nullptr) {
    placed_lighting_.reserve(background.placements->placements().size());
  }
  // Palette storage stays stable until the native owner copies the complete
  // draw list. Allocate once for the bounded placement/draw census.
  using JointPalette =
      std::array<render::WorldJointMatrix, kJointPaletteCapacity>;
  memory::Vector<JointPalette> palettes{
      memory::Allocator<JointPalette>(domain)};
  palettes.reserve(background_capacity);
  const auto append_model = [&](const formats::ModelDefinition& model,
                                render::WorldModelHandle handle, bool sky,
                                const formats::BackgroundPlacement* placement =
                                    nullptr,
                                const ResidentModelView* resident = nullptr) {
    if (!output_.geometry_status(handle).texture_bindings_ready) {
      throw std::runtime_error(
          "Stage view requires resident geometry and textures");
    }
    std::size_t selected = 0;
    std::optional<std::size_t> placed_index;
    if (placement != nullptr) {
      if (resident == nullptr || background.placements == nullptr) {
        throw std::logic_error("Placed lighting requires its resident owner");
      }
      const auto root_position = placed_receiver_position(
          *resident,
          std::span(background.placements->matrices())
              .subspan(placement->first_matrix, placement->matrix_count));
      const resources::detail::Reader header{model.header()};
      constexpr std::size_t kGroupRadius = 0x40;
      placed_index = placed_lighting_.size();
      placed_lighting_.push_back(lighting_->prepare_placed(
          character_field_, root_position,
          std::bit_cast<float>(header.u32(kGroupRadius)),
          model.draws().size()));
    }
    for (std::size_t index = 0; index < model.draws().size(); ++index) {
      const auto group = model.draws().at(index).group;
      if (group >= kSourceGroupCount) {
        throw std::runtime_error("Mesh group exceeds original instance mask");
      }
      // Fresh instances enable all four mask words. Background state disables
      // authored low-word groups; later destruction owns subsequent changes.
      if (placement != nullptr && group < kGroupBitsPerWord &&
          (placement->primary_parameters[2] & (1U << group)) != 0) {
        continue;
      }
      const auto& material =
          model.materials().at(model.draws().at(index).material);
      auto flags = material.shader_selector[0] | kFogFeature;
      const auto variant = static_cast<std::uint16_t>(
          material.shader_selector[1] & kVariantMask);
      auto lighting = render::WorldLightingPolicy::supplied;
      // Runtime irradiance remains enabled for every static object, including
      // families with independently omitted directional or shadow terms.
      constexpr std::uint32_t kSphericalHarmonics = 0x200;
      flags |= kSphericalHarmonics;
      const auto requirements =
          render::world_material_requirements(flags, variant, lighting);
      if (!requirements.supported) {
        throw std::runtime_error("Unsupported source world material variant");
      }
      render::WorldColorDraw draw;
      draw.view_space =
          sky ? render::WorldViewSpace::sky : render::WorldViewSpace::world;
      material_parameters(material, requirements, draw.parameters);
      require_textures(material, requirements);
      environment_parameters(
          environment, sky ? sky_transform : view, sky, directional,
          -view.camera_relative_world_rows[1][3], draw.parameters);
      if (placed_index) draw.instance = *placed_index + 1;
      if (placement != nullptr) {
        if (resident == nullptr || requirements.joint_influences == 0 ||
            resident->body_joints.size() != placement->matrix_count) {
          throw std::runtime_error(
              "Placed prop body/palette association differs");
        }
        auto& palette = palettes.emplace_back();
        std::array<bool, kJointPaletteCapacity> assigned{};
        std::size_t count = 0;
        for (const auto& reference : material.matrices) {
          constexpr std::uint32_t kJointSemantic = 0x4a494e54;
          if (reference.semantic != kJointSemantic ||
              reference.destination_matrix >= palette.size()) {
            throw std::runtime_error("Unresolved placed-prop matrix reference");
          }
          const auto body = std::ranges::find(
              resident->body_joints,
              static_cast<std::int32_t>(reference.source_matrix));
          if (body == resident->body_joints.end()) {
            throw std::runtime_error(
                "Placed prop joint has no authored body pose");
          }
          const auto body_index =
              static_cast<std::size_t>(body - resident->body_joints.begin());
          const auto& matrix = background.placements->matrices().at(
              placement->first_matrix + body_index);
          auto& destination = palette.at(reference.destination_matrix);
          // Physics writes world joint poses directly; the render path skips
          // inverse-bind composition in this mode and uploads transposed rows.
          for (std::size_t row = 0; row < 4; ++row) {
            for (std::size_t column = 0; column < 4; ++column) {
              destination.at(row).at(column) = matrix.at(column).at(row);
            }
          }
          assigned.at(reference.destination_matrix) = true;
          count = std::max(
              count,
              static_cast<std::size_t>(reference.destination_matrix) + 1);
        }
        if (count == 0 ||
            !std::ranges::all_of(std::span(assigned).first(count),
                                 [](bool value) { return value; })) {
          throw std::runtime_error(
              "Placed prop palette contains unresolved slots");
        }
        draw.joint_matrices = std::span(palette).first(count);
      }
      draw.model = handle;
      draw.draw = index;
      draw.effective_shader_flags = flags;
      draw.lighting = lighting;
      draw.color_phase = color_phase(model.draws().at(index).chunk, sky);
      if (!sky) draw.owner_opacity = 1;
      // Later scene lists test the established opaque depth without writing it.
      // Their material blend records remain independent from phase ordering.
      draw.state = {render::WorldTextureFilter::anisotropic,
                    render::WorldTextureFilter::linear,
                    render::WorldTextureAddress::repeat,
                    render::WorldTextureAddress::repeat,
                    render::WorldCull::counter_clockwise,
                    sky ? render::WorldDepthCompare::always
                        : render::WorldDepthCompare::greater_equal,
                    !sky && model.draws().at(index).chunk <= 1,
                    material_blend(material),
                    4,
                    render::WorldMipFilter::linear};
      if (!sky) {
        // Static casters submit only the opaque and cutout pass selectors.
        // Transparent surfaces and initially placed props still receive
        // shadows.
        draw.casts_shadow =
            placement == nullptr && model.draws().at(index).chunk <= 1;
        if (draw.casts_shadow) {
          static_shadow_updates_.push_back(
              {handle, index, draw.visible, true, draw.instance});
        }
      }
      // Capture the effective command state at this draw. Later TRST commands
      // reset subsequent draws; they must not erase this draw's TANM binding.
      AnimatedMaterial animated;
      const auto& animation_slots =
          model.draws().at(index).texture_animation_slots;
      for (std::size_t slot = 0; slot < animation_slots.size(); ++slot) {
        if (!animation_slots.at(slot)) continue;
        const auto& name =
            model.material_animation_names().at(*animation_slots.at(slot));
        auto found =
            std::ranges::find(animated_curves_, name, &AnimatedCurve::name);
        if (found == animated_curves_.end()) {
          auto curve = material_curve(stage, name);
          animated_curves_.push_back({name, curve, curve.sample(0)});
          found = std::prev(animated_curves_.end());
        }
        const auto value = found->value;
        draw.parameters.texture_offset.at(slot) = {value[0], value[1], 0, 0};
        animated.curves.at(slot) =
            static_cast<std::size_t>(found - animated_curves_.begin());
      }
      if (std::ranges::any_of(animated.curves, [](const auto& curve) {
            return curve.has_value();
          })) {
        animated_materials_.push_back(animated);
        texture_updates_.push_back(
            {handle, index, draw.parameters.texture_offset});
      }
      if (sky) {
        draw.reflection_texture = sky_lighting.reflection_texture;
        draw.parameters.irradiance_coefficients =
            sky_lighting.irradiance_coefficients;
        draw.parameters.local_lights = sky_lighting.local_lights;
        draw.parameters.local_light_meta[0] = sky_lighting.local_light_count;
      } else if (placed_index) {
        placed_lighting_.at(*placed_index).add_draw(draw);
      } else {
        static_lighting_->add_draw(draw);
      }
      draws.push_back(draw);
      ++selected;
    }
    if (placed_index) placed_lighting_.at(*placed_index).seal();
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
  // with their separate initial placement owner below; overlapping bounds do
  // not imply a LOD choice.
  for (std::size_t index = 0; index < stage.models.size(); ++index) {
    const auto& model = stage.models.at(index);
    if (!model.static_world_object) continue;
    const auto count =
        append_model(model.definition, stage_source.models[index], false);
    selected_draws_ += count;
    omitted_draws_ += model.definition.draws().size() - count;
  }
  if (background.placements != nullptr) {
    for (const auto& placement : background.placements->placements()) {
      const auto& resident = background_model(placement.model);
      const auto& model = resident.definition.get();
      const auto count =
          append_model(model, resident.handle, false, &placement, &resident);
      background_draws_ += count;
      omitted_draws_ += model.draws().size() - count;
    }
  }
  if (player_) {
    const auto before_player = draws.size();
    auto& admitted_player = *player_;
    player_lighting_[0].emplace(
        append_player_draws(draws, admitted_player, false, camera));
    player_lighting_[1].emplace(
        append_player_draws(draws, admitted_player, true, camera));
    player_draws_ = draws.size() - before_player;
  }
  if (selected_draws_ == 0 || sky_draws_ == 0) {
    throw std::runtime_error(
        "Static stage view requires supported world and sky draws");
  }
  // Stage activation enables shadow setup before publishing its observers.
  // The source camera warp is shared verbatim by caster and receiver passes.
  {
    const auto projection = render::make_directional_shadow_projection(
        camera.pose, camera.perspective,
        initial_stage_shadow_settings(environment.shadow),
        {direction[0], direction[1], direction[2]});
    shadow_projection_ = projection;
    constexpr float kShadowResolution =
        static_cast<float>(render::kDirectionalShadowExtent);
    for (auto& draw : draws) {
      if (draw.state.depth_compare == render::WorldDepthCompare::always) {
        continue;
      }
      draw.parameters.shadow_clip_rows = projection.clip_rows;
      draw.parameters.shadow_parameters = {
          1, 0, environment.shadow.receiver_radius / kShadowResolution, 0};
    }
  }
  // Publish only after the complete view validates. Device storage stays with
  // the mission, so view construction neither duplicates nor releases assets.
  output_.set_world_output_parameters(
      display_parameters(environment.tone_mapping));
  if (!static_lighting_) {
    throw std::logic_error("Stage lighting receiver was not prepared");
  }
  static_lighting_->seal();
  output_.register_draws(draws);
  auto published_camera = cameras.completed;
  published_camera.perspective.far_plane = initial_far_distance_;
  publish_view(published_camera, force_player_visibility);
}
scene::LightingReceiver StagePresentation::model_receiver(
    const ResidentModelView& resident,
    const animation::Matrix& transform) const {
  if (!lighting_) throw std::logic_error("Scene lighting was not prepared");
  const resources::detail::Reader reader{resident.definition.get().header()};
  scene::LightingReceiver receiver;
  receiver.policy = scene::ReceiverLightingPolicy::spatial;
  receiver.field = character_field_;
  receiver.model_world = math::affine_part(transform);
  constexpr std::size_t kGroupCenter = 0x30, kGroupRadius = 0x40;
  for (std::size_t axis = 0; axis < receiver.model_center.size(); ++axis) {
    receiver.model_center.at(axis) =
        std::bit_cast<float>(reader.u32(kGroupCenter + axis * sizeof(float)));
  }
  receiver.model_radius = std::bit_cast<float>(reader.u32(kGroupRadius));
  return receiver;
}
void StagePresentation::publish_player_lighting() {
  if (!player_) return;
  for (const bool weapon : {false, true}) {
    auto& receiver = player_lighting_.at(weapon ? 1 : 0);
    if (!receiver) {
      throw std::logic_error("Player lighting receiver was not prepared");
    }
    receiver->publish_model(
        math::affine_part(player_->model_transform(weapon)));
  }
}
scene::ReceiverPublication StagePresentation::append_player_draws(
    memory::Vector<render::WorldColorDraw>& draws, const PlayerActor& actor,
    bool weapon, const InitialPlayerCamera& camera) {
  const auto& resident = weapon ? actor.source().weapon : actor.source().body;
  const auto& model = resident.definition.get();
  if (!output_.geometry_status(resident.handle).texture_bindings_ready ||
      !environment_ || !lighting_) {
    throw std::runtime_error(
        "Player appearance requires resident stage resources");
  }
  const auto& environment = *environment_;
  const auto directional = lighting_->directional_lighting();
  const auto& direction = directional.direction;
  const auto view =
      render::make_view_transforms(camera.pose, camera.perspective);
  const auto shadow = render::make_directional_shadow_projection(
      camera.pose, camera.perspective,
      initial_stage_shadow_settings(environment.shadow),
      {direction[0], direction[1], direction[2]});
  auto lighting = lighting_->prepare_receiver(
      model_receiver(resident, actor.model_transform(weapon)),
      model.draws().size());
  for (std::size_t index = 0; index < model.draws().size(); ++index) {
    if (!actor.includes_draw(weapon, index)) continue;
    const auto& source_draw = model.draws().at(index);
    if (source_draw.group >= kSourceGroupCount ||
        std::ranges::any_of(
            source_draw.texture_animation_slots,
            [](const auto& slot) { return slot.has_value(); })) {
      throw std::runtime_error(
          "Player draw has unsupported group or material animation");
    }
    const auto& material = model.materials().at(source_draw.material);
    constexpr std::uint32_t kSphericalHarmonics = 0x200;
    const auto flags =
        material.shader_selector[0] | kFogFeature | kSphericalHarmonics;
    const auto variant =
        static_cast<std::uint16_t>(material.shader_selector[1] & kVariantMask);
    const auto requirements =
        render::world_material_requirements(flags, variant);
    if (!requirements.supported) {
      throw std::runtime_error("Player material family is not reconstructed");
    }
    render::WorldColorDraw draw;
    draw.model = resident.handle;
    draw.draw = index;
    draw.view_space = render::WorldViewSpace::world;
    draw.effective_shader_flags = flags;
    draw.lighting = render::WorldLightingPolicy::supplied;
    draw.color_phase = color_phase(source_draw.chunk, false);
    draw.owner_opacity = 1;
    material_parameters(material, requirements, draw.parameters);
    require_textures(material, requirements);
    environment_parameters(environment, view, false, directional,
                           camera.pose.eye[1], draw.parameters);
    draw.parameters.shadow_clip_rows = shadow.clip_rows;
    draw.parameters.shadow_parameters = {
        1, 0,
        environment.shadow.receiver_radius /
            static_cast<float>(render::kDirectionalShadowExtent),
        0};
    draw.joint_matrices = actor.draw_palette(weapon, index);
    draw.visible = actor.draw_visible(weapon, index);
    draw.use_shape_positions = source_draw.shape_stream.has_value();
    draw.casts_shadow = source_draw.chunk <= 1;
    draw.state = {render::WorldTextureFilter::anisotropic,
                  render::WorldTextureFilter::linear,
                  render::WorldTextureAddress::repeat,
                  render::WorldTextureAddress::repeat,
                  render::WorldCull::counter_clockwise,
                  render::WorldDepthCompare::greater_equal,
                  source_draw.chunk <= 1,
                  material_blend(material),
                  4,
                  render::WorldMipFilter::linear};
    lighting.add_draw(draw);
    draws.push_back(draw);
  }
  lighting.seal();
  return lighting;
}
void StagePresentation::publish_shadow_owners(const InitialPlayerCamera& camera,
                                              bool force_player_visibility) {
  using scene::visibility::make_shadow_selection_volume;
  using scene::visibility::query_model_shadow_caster;
  using scene::visibility::query_static_shadow_caster;
  using scene::visibility::shadow_caster_admitted;
  using scene::visibility::ShadowModelCaster;
  using scene::visibility::ShadowSelectionCamera;
  if (!environment_ || !environment_->shadow.setup_enabled) return;
  const auto& shadow = environment_->shadow;
  const bool static_admitted = shadow_caster_admitted(
      query_static_shadow_caster(shadow.minimum_projected_size));
  for (auto& update : static_shadow_updates_) {
    update.casts_shadow = static_admitted;
  }
  if (!static_shadow_updates_.empty()) {
    output_.update_world_visibility(static_shadow_updates_);
  }
  if (!player_) return;
  const auto& pose = camera.pose;
  const auto& perspective = camera.perspective;
  const float height =
      std::tan(perspective.vertical_fov_radians * .5F) * perspective.near_plane;
  const float width = height * perspective.aspect;
  const ShadowSelectionCamera selected_camera{
      {pose.right, pose.up, pose.forward, pose.eye},
      {-width, width, -height, height, perspective.near_plane,
       perspective.far_plane}};
  auto toward_light = environment_->light_vector;
  constexpr float kSourceLightDirectionBias = .001F;
  toward_light[2] += kSourceLightDirectionBias;
  const auto volume = make_shadow_selection_volume(
      selected_camera, shadow.far_distance, toward_light);
  const auto& actor = *player_;
  for (const bool weapon : {false, true}) {
    const auto& resident = weapon ? actor.source().weapon : actor.source().body;
    const auto transform = math::affine_part(actor.model_transform(weapon));
    const resources::detail::Reader header{resident.definition.get().header()};
    constexpr std::size_t kGroupCenter = 0x30, kGroupRadius = 0x40;
    math::Vector3<float> group_center{};
    for (std::size_t axis = 0; axis < group_center.size(); ++axis) {
      group_center.at(axis) =
          std::bit_cast<float>(header.u32(kGroupCenter + axis * sizeof(float)));
    }
    // Original model owner initializes a separate .95 projected sphere at
    // local (0,.9,0). Its intersection sphere is authored GROUP0, unscaled.
    const ShadowModelCaster owner{
        {math::transform_point<float>({0, .9F, 0}, transform), .95F},
        {math::transform_point<float>(group_center, transform),
         std::bit_cast<float>(header.u32(kGroupRadius))},
        true,
        !weapon && force_player_visibility,
        false};
    const bool admitted = shadow_caster_admitted(query_model_shadow_caster(
        selected_camera, volume, shadow.minimum_projected_size, owner));
    for (std::size_t index = 0;
         index < resident.definition.get().draws().size(); ++index) {
      if (!actor.includes_draw(weapon, index)) continue;
      const auto& draw = resident.definition.get().draws().at(index);
      const render::WorldVisibilityUpdate update{
          resident.handle, index, actor.draw_visible(weapon, index),
          admitted && draw.chunk <= 1, 0};
      output_.update_world_visibility(std::span(&update, 1));
    }
  }
}
void StagePresentation::publish_view(const InitialPlayerCamera& camera,
                                     bool force_player_visibility) {
  if (!environment_ || !lighting_) return;
  const auto& environment = *environment_;
  const auto directional = lighting_->directional_lighting();
  const auto& direction = directional.direction;
  const auto world =
      render::make_view_transforms(camera.pose, camera.perspective);
  const auto sky = sky_view(world, environment);
  if (environment.shadow.cull_selector > 2) {
    throw std::invalid_argument("Unclassified source shadow cull policy");
  }
  if (environment.shadow.setup_enabled) {
    shadow_projection_ = render::make_directional_shadow_projection(
        camera.pose, camera.perspective,
        initial_stage_shadow_settings(environment.shadow),
        {direction[0], direction[1], direction[2]});
  }
  if (!shadow_projection_) {
    throw std::logic_error("Shadow projection has not been initialized");
  }
  const auto& shadow = *shadow_projection_;
  const auto parameters = [&](bool background) {
    const auto& transform = background ? sky : world;
    render::WorldColorParameters fog;
    fog_parameters(environment, camera.pose.eye[1], background, fog);
    return render::WorldViewParameters{
        .clip_rows = transform.clip_rows,
        .world_rows = transform.camera_relative_world_rows,
        .fog_color = fog.fog_color,
        .fog_distance = fog.fog_distance,
        .fog_height = fog.fog_height,
        .fog_density = fog.fog_density,
        .shadow_clip_rows = shadow.clip_rows};
  };
  lighting_->set_view(lighting_view(camera));
  output_.update_world_view(
      {parameters(false), parameters(true),
       render::WorldViewUpdate::Lighting{
           directional.direction, directional.diffuse, directional.specular,
           environment.shadow.receiver_radius /
               static_cast<float>(render::kDirectionalShadowExtent),
           environment.shadow.casters_enabled},
       render::WorldViewUpdate::ScenePasses{
           scene_passes_.opaque_preparation, scene_passes_.masked_preparation,
           scene_passes_.integer_pixel_centers, environment.clear_color},
       render::WorldViewUpdate::ShadowPass{
           environment.shadow.setup_enabled,
           environment.shadow.cull_selector == 0 ? render::WorldCull::none
           : environment.shadow.cull_selector == 1
               ? render::WorldCull::clockwise
               : render::WorldCull::counter_clockwise,
           environment.shadow.raster_depth_bias,
           environment.shadow.raster_slope_bias}});
  publish_shadow_owners(camera, force_player_visibility);
  output_.set_world_output_parameters(
      display_parameters(environment.tone_mapping));
  if (!static_lighting_) {
    throw std::logic_error("Stage lighting receiver was not prepared");
  }
  static_lighting_->publish();
  for (auto& placed : placed_lighting_) placed.publish();
}
std::optional<float> StagePresentation::update_environment(
    const InitialPlayerCamera& camera) {
  if (environment_channels_ && environment_ && lighting_) {
    environment_channels_->update(camera.pose.eye);
    environment_ =
        stage_environment_snapshot(*environment_, *environment_channels_);
    const auto& value = *environment_;
    const float far_distance =
        stage_environment_camera_far(*environment_channels_);
    lighting_->set_parameters(
        {value.ambient_color,
         {value.light_vector, value.light_diffuse, value.light_specular}});
    if (character_field_.index != std::numeric_limits<std::size_t>::max()) {
      lighting_->set_field_intensity(character_field_,
                                     value.character_light_intensity);
    }
    if (enemy_field_.index != std::numeric_limits<std::size_t>::max()) {
      lighting_->set_field_intensity(enemy_field_, value.enemy_light_intensity);
    }
    return far_distance;
  }
  return std::nullopt;
}
void StagePresentation::present(const InitialPlayerCamera& camera,
                                bool force_player_visibility, double next) {
  publish_view(camera, force_player_visibility);
  publish_player_lighting();
  // Evaluate each authored channel once, then distribute through prepared
  // slots.
  for (auto& curve : animated_curves_) curve.value = curve.curve.sample(next);
  for (std::size_t index = 0; index < animated_materials_.size(); ++index) {
    const auto& animated = animated_materials_.at(index);
    auto& update = texture_updates_.at(index);
    for (std::size_t slot = 0; slot < animated.curves.size(); ++slot) {
      const auto& curve = animated.curves.at(slot);
      if (!curve) continue;
      const auto value = animated_curves_.at(*curve).value;
      update.texture_offset.at(slot) = {value[0], value[1], 0, 0};
    }
  }
  if (!texture_updates_.empty()) {
    output_.update_world_texture_offsets(texture_updates_);
  }
}
// Renderer replacement is synchronous and atomic. The simulation adopts the
// candidate's prepared buffers next, before MissionLoading retires old assets.
void StagePresentation::replace_appearance(const PlayerActor& candidate,
                                           bool weapon,
                                           const InitialPlayerCamera& camera) {
  if (player_ == nullptr) {
    throw std::logic_error("Appearance requires a player");
  }
  const auto& resident =
      weapon ? candidate.source().weapon : candidate.source().body;
  const auto& previous =
      weapon ? player_->source().weapon : player_->source().body;
  memory::Vector<render::WorldColorDraw> draws{
      memory::Allocator<render::WorldColorDraw>(memory_)};
  draws.reserve(resident.definition.get().draws().size());
  auto lighting = append_player_draws(draws, candidate, weapon, camera);
  std::size_t old_draws = 0;
  for (std::size_t index = 0; index < previous.definition.get().draws().size();
       ++index) {
    if (player_->includes_draw(weapon, index)) ++old_draws;
  }
  output_.replace_model_draws(previous.handle, draws);
  // Candidate draw destinations/caches are already prepared; adoption cannot
  // allocate or throw after the native transaction has committed.
  auto& installed = player_lighting_.at(weapon ? 1 : 0);
  installed.reset();
  installed.emplace(std::move(lighting));
  player_draws_ = player_draws_ - old_draws + draws.size();
}
}  // namespace bw::game
