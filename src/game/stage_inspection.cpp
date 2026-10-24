// Scene color admission applies both-phase I.1 (#ri-explicit) and GPU.6
// (guidelines/gpu/GPU.6-batch-tiny-gpu-work.md): resolve source chunk phase and
// depth state at publication; the renderer reuses its prepared phase order.
// Local lighting: I.11 (#ri-raw) keeps borrowed mission/PHIT ownership
// explicit; MEM.9 (guidelines/memory/MEM.9-embedded-allocate-at-init.md)
// prepares emitters once and reuses bounded selection scratch. Both corpora
// were consulted for design and implementation. Source lighting equations live
// in scene/render; this owner admits authored placed emitters and publishes
// complete packets. Caster admission: both-phase I.1 (#ri-explicit) makes the
// original opaque/ cutout pass selection explicit; GPU.1 (guidelines/gpu/
// GPU.1-keep-data-on-device.md) keeps this a setup-only retained draw decision.
// Actor preparation: both-phase I.11 (#ri-raw) bounds mission asset borrowing;
// CDSA.32
// (guidelines/cpu-dsa/CDSA.32-transform-static-data-into-the-consumer-layout.md)
// resolves names/attachments once before frame-time pose and palette updates.
// Both-phase P.9 (#rp-waste) eliminates repeated per-draw curve evaluation;
// MEM.9 keeps prepared curve/slot storage fixed during advancement.
// Material animation refinement applies both-phase R.2 (#rr-use-ptr) to
// bounded borrowed labels/keys and MEM.9 (guidelines/memory/
// MEM.9-embedded-allocate-at-init.md) to setup-only metadata allocation.
// Build the stage view and optional actor presentation from retained assets.
// Selection uses ordinary static stage objects and sky, supported original
// shaders and original texture slots. The initial camera consumes authored
// player settings, scripted spawn and static clearance. Source initialization
// enables all mesh groups, then authored background masks disable selected
// groups. Device residency belongs to the mission; this view clears draws
// before it retires. Algorithm/cost and both-phase E.6/GPU.1 guidance are in
// stage_inspection.hpp; no stage decode, image upload or draw reconstruction
// repeats each frame. Camera/sky composition also applies both-phase I.1
// explicit view policy and MEM.9 fixed transform/coefficient storage as cited
// in the header. This slice queried both corpora in design/implementation: C.32
// (#rc-dtor-ptr) retains borrowed mission data; MEM.9 (guidelines/memory/
// MEM.9-embedded-allocate-at-init.md) packs fog/palettes only at construction.
#include "stage_inspection.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <numbers>
#include <stdexcept>

#include "bw/render/world_material.hpp"
#include "formats/reader.hpp"
#include "game/camera/initial_camera.hpp"
#include "mission_loading.hpp"
#include "placed_collision.hpp"
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
constexpr std::uint32_t kInspectionFeatures = 0x00e00000;
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
void register_stage_collision(const content::StageDefinition& stage,
                              scene::StaticCollision& collision) {
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
// Presentation converts prepared engine values to backend-neutral bindings;
// no scene sampling or field ownership belongs to the actor/controller.
render::WorldLightingUpdate lighting_packet(
    const scene::ReceiverLighting& lighting) {
  render::WorldLightingUpdate update;
  update.irradiance_coefficients = lighting.irradiance;
  const auto& selected = lighting.local_lights;
  update.local_light_count = static_cast<std::uint32_t>(selected.count);
  for (std::size_t index = 0; index < selected.count; ++index) {
    const auto& source = selected.lights.at(index);
    auto& destination = update.local_lights.at(index);
    destination.position = {source.position[0], source.position[1],
                            source.position[2], 1};
    destination.diffuse = {source.diffuse[0], source.diffuse[1],
                           source.diffuse[2], 1};
    destination.specular = {source.specular[0], source.specular[1],
                            source.specular[2], 1};
    destination.attenuation = {source.attenuation[0], source.attenuation[1],
                               source.attenuation[2],
                               selected.contribution_threshold};
  }
  return update;
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
StageInspection::StageInspection(
    render::WorldOutput& output, StageViewSource stage_source,
    StageViewSource sky_source, const std::shared_ptr<memory::Domain>& domain,
    const std::optional<render::ViewPose>& camera_pose,
    StageBackgroundSource background,
    const std::optional<PlayerActorSource>& player,
    const content::LoadProgress& progress, StageCharacterLightSource light,
    const std::optional<ScriptedPlayerPose>& inspection_placement)
    : output_(output),
      memory_(domain),
      collision_(domain),
      static_lighting_updates_(
          memory::Allocator<render::WorldLightingUpdate>(domain)),
      animated_curves_(memory::Allocator<AnimatedCurve>(domain)),
      animated_materials_(memory::Allocator<AnimatedMaterial>(domain)),
      texture_updates_(
          memory::Allocator<render::WorldTextureOffsetUpdate>(domain)) {
  if (!player && !inspection_placement) {
    throw std::invalid_argument(
        "Playerless inspection requires an explicit placement");
  }
  if (player) {
    if (light.character == nullptr) {
      throw std::invalid_argument(
          "Player requires authored character light field");
    }
  }
  const auto& stage = stage_source.definition.get();
  if (!stage.environment) {
    throw std::runtime_error("Stage inspection requires authored environment");
  }
  environment_ = *stage.environment;
  environment_channels_ = prepare_stage_environment(stage, domain);
  const auto& environment = *environment_;
  const auto customization = formats::CustomizationDefinition::parse(
      stage.customization_bytes(), domain);
  const auto camera_settings =
      formats::PlayerCameraSettings::parse_initial(customization);
  register_stage_collision(stage, collision_);
  memory::Vector<scene::LocalLight> emitters{
      memory::Allocator<scene::LocalLight>(domain)};
  if (background.placements != nullptr) {
    emitters.reserve(background.placements->placements().size());
  }
  // Prepare each placed model's authored shapes once, then register its frozen
  // body poses. Render geometry is never substituted for collision geometry.
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
      PreparedPropCollision prepared(*model.physics_collision, domain);
      for (const auto& placement : placements.placements()) {
        if (placement.model != model_index) continue;
        const auto body_world =
            std::span(placements.matrices())
                .subspan(placement.first_matrix, placement.matrix_count);
        prepared.register_instance(body_world, collision_);
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
  automatic_targeting_camera_ = camera_settings.automatic_targeting_camera;
  collision_.prepare_character_queries();
  const auto placement =
      player ? player->placement : inspection_placement.value();
  auto camera =
      make_initial_player_camera(placement, camera_settings, collision_);
  if (camera_pose) camera.pose = *camera_pose;
  fixed_view_ = camera;
  if (environment_channels_) {
    // Scene-ready startup requests one immediate environment publication.
    // Region transitions thereafter use the source per-world-update blend.
    environment_channels_->update(camera.pose.eye, true);
    environment_ =
        stage_environment_snapshot(environment, *environment_channels_);
  }
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
  lighting_.emplace(
      scene::SceneLightingSource{
          std::span(fields).first(field_count),
          emitters,
          local_light_settings(customization),
          {environment.ambient_color,
           {environment.light_vector, environment.light_diffuse,
            environment.light_specular}}},
      domain);
  const auto directional = lighting_->directional_lighting();
  const auto& direction = directional.direction;
  lighting_->set_view(lighting_view(camera));
  const auto static_lighting = lighting_packet(lighting_->evaluate(
      {.policy = scene::ReceiverLightingPolicy::stage_ambient}));
  const auto sky_lighting = lighting_packet(lighting_->evaluate(
      {.policy = scene::ReceiverLightingPolicy::sky_white}));
  audio_listener_ = {camera.pose.eye, camera.pose.forward, camera.pose.up};
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
  if (player) {
    for (unsigned index = 0; index < weapon_settings_.size(); ++index) {
      weapon_settings_.at(index) =
          WeaponPresentationSettings::parse(customization, index);
    }
    const auto weapon_name = player->weapon.name;
    if (weapon_name.size() != 3 || !weapon_name.starts_with("wa") ||
        weapon_name[2] < '0' || weapon_name[2] > '3') {
      throw std::invalid_argument("Unknown admitted player weapon class");
    }
    weapon_class_ = static_cast<unsigned>(weapon_name[2] - '0');
    weapon_gate_.emplace(weapon_settings_.at(weapon_class_).rate_per_second);
    constexpr std::array death_path{
        std::string_view("witch"), std::string_view("actor"),
        std::string_view("pw0"), std::string_view("body")};
    const resources::detail::Reader body_reader{
        customization.lookup(death_path)};
    constexpr std::uint64_t kDeathWeights = 0x124,
                            kPositiveDeathWeights = 0x13c;
    const auto decode_weights = [&](auto& weights, std::uint64_t offset) {
      for (std::size_t index = 0; index < weights.size(); ++index) {
        weights.at(index) = std::bit_cast<float>(
            body_reader.u32(offset + index * sizeof(float)));
        if (!std::isfinite(weights.at(index)) || weights.at(index) < 0) {
          throw std::invalid_argument(
              "Invalid authored death selection weight");
        }
      }
    };
    decode_weights(review_death_.nonpositive_weights, kDeathWeights);
    decode_weights(review_death_.positive_weights, kPositiveDeathWeights);
    set_review_status(
        "Presentation controls - no combat outcomes | Menu: Animation Review");
    player_.emplace(*player, domain, progress);
    if (!camera_pose) {
      controller_.emplace(*player_, collision_,
                          PlayerBodySettings::parse(customization), placement,
                          targeting_);
      camera_.emplace(camera_settings, placement, collision_);
    }
    capacity += player->body.definition.get().draws().size() +
                player->weapon.definition.get().draws().size();
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
      auto flags = material.shader_selector[0] | kInspectionFeatures;
      const auto variant = static_cast<std::uint16_t>(
          material.shader_selector[1] & kVariantMask);
      auto lighting = render::WorldLightingPolicy::supplied;
      // The source untextured surface consumes material and scene lighting,
      // without a base sampler or texture coordinates.
      if (material.shader_selector[0] == 0 && variant == 0) {
        flags = kFogFeature;
        lighting = render::WorldLightingPolicy::supplied;
      }
      constexpr std::uint32_t kLayeredFresnel = 0x00020882;
      constexpr std::uint32_t kLocalLighting = 0x04200882;
      constexpr std::uint16_t kLayeredVariant = 0x102;
      constexpr std::uint32_t kAlphaSelector = 1;
      const auto surface = material.shader_selector[0] & ~kAlphaSelector;
      if ((surface == kLayeredFresnel && variant == kLayeredVariant) ||
          (surface == kLocalLighting && variant == 1)) {
        // These actual cached programs require their distinct selector;
        // recovered light and shadow inputs now accompany the material terms.
        constexpr std::uint32_t kCachedLayered = 0x00820882;
        constexpr std::uint32_t kCachedLocal = 0x04a00882;
        flags = (variant == kLayeredVariant ? kCachedLayered : kCachedLocal) |
                (material.shader_selector[0] & kAlphaSelector);
        lighting = render::WorldLightingPolicy::supplied;
      }
      if (placement != nullptr) {
        // The cached prop vertex programs retain their rigid/two-joint input;
        // ordinary static-world selector augmentation is not valid here.
        flags = material.shader_selector[0] | kFogFeature;
        lighting = render::WorldLightingPolicy::supplied;
      }
      // Runtime irradiance remains enabled for every static object, including
      // families with independently omitted directional or shadow terms.
      constexpr std::uint32_t kSphericalHarmonics = 0x200;
      flags |= kSphericalHarmonics;
      const auto requirements =
          render::world_material_requirements(flags, variant, lighting);
      if (!requirements.supported) {
        continue;
      }
      render::WorldColorDraw draw;
      draw.view_space =
          sky ? render::WorldViewSpace::sky : render::WorldViewSpace::world;
      material_parameters(material, requirements, draw.parameters);
      require_textures(material, requirements);
      environment_parameters(
          environment, sky ? sky_transform : view, sky, directional,
          -view.camera_relative_world_rows[1][3], draw.parameters);
      const auto& model_light = sky ? sky_lighting : static_lighting;
      draw.parameters.irradiance_coefficients =
          model_light.irradiance_coefficients;
      draw.parameters.local_lights = model_light.local_lights;
      draw.parameters.local_light_meta[0] = model_light.local_light_count;
      if (!sky) {
        auto update = static_lighting;
        update.model = handle;
        update.draw = index;
        static_lighting_updates_.push_back(update);
      }
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
    auto& admitted_player = player_.value();
    append_player_draws(draws, admitted_player, false, camera);
    append_player_draws(draws, admitted_player, true, camera);
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
  output_.set_world_draws(draws);
  publish_view();
}
// Prepare only the selected appearance; resident static world bindings remain
// untouched by a costume transaction.
// The original model observer transforms group-zero bounds center by the
// published model matrix, then samples the mission character field. Bones and
// material colors do not define this point; body and weapon sample separately.
render::WorldLightingUpdate StageInspection::model_lighting(
    const ResidentModelView& resident, const animation::Matrix& transform) {
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
  auto update = lighting_packet(lighting_->evaluate(receiver));
  update.model = resident.handle;
  return update;
}
// All destinations were admitted with the actor. Reuse a fixed update packet;
// each model samples once, independently of draw count, without resource lookup
// or device geometry work. The device validates every submitted packet.
void StageInspection::publish_player_lighting() {
  if (!player_) return;
  const auto& actor = player_.value();
  for (const bool weapon : {false, true}) {
    const auto& resident = weapon ? actor.source().weapon : actor.source().body;
    auto update = model_lighting(resident, actor.model_transform(weapon));
    for (std::size_t draw = 0; draw < resident.definition.get().draws().size();
         ++draw) {
      if (!actor.includes_draw(weapon, draw)) continue;
      update.draw = draw;
      output_.update_world_lighting(std::span(&update, 1));
    }
  }
}
void StageInspection::append_player_draws(
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
  const auto lighting = model_lighting(resident, actor.model_transform(weapon));
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
    material_parameters(material, requirements, draw.parameters);
    require_textures(material, requirements);
    environment_parameters(environment, view, false, directional,
                           camera.pose.eye[1], draw.parameters);
    draw.parameters.irradiance_coefficients = lighting.irradiance_coefficients;
    draw.parameters.local_lights = lighting.local_lights;
    draw.parameters.local_light_meta[0] = lighting.local_light_count;
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
    draws.push_back(draw);
  }
}
void StageInspection::change_costume(MissionLoading& loading,
                                     unsigned costume) {
  if (!paused_ || !player_ || !controller_ || !camera_) {
    throw std::logic_error("Costume change requires paused player traversal");
  }
  auto& actor = player_.value();
  if (loading.current_player_costume() == costume) return;
  loading.prepare_player_costume(costume);
  try {
    auto source = actor.source();
    source.body = loading.candidate_player();
    PlayerActor candidate(source, actor, memory_);
    memory::Vector<render::WorldColorDraw> draws{
        memory::Allocator<render::WorldColorDraw>(memory_)};
    draws.reserve(source.body.definition.get().draws().size());
    append_player_draws(draws, candidate, false, camera_->view());
    std::size_t old_body_draws = 0;
    const auto& old_source = actor.source();
    for (std::size_t draw = 0;
         draw < old_source.body.definition.get().draws().size(); ++draw) {
      if (actor.includes_draw(false, draw)) ++old_body_draws;
    }
    output_.replace_world_model_draws(old_source.body.handle, draws);
    // Everything that can fail precedes publication. Retire old instance data
    // before its mission-owned geometry/definitions, keeping controller
    // address.
    actor.adopt_appearance(std::move(candidate));
    loading.commit_player_costume();
    player_draws_ = player_draws_ - old_body_draws + draws.size();
  } catch (...) {
    loading.discard_player_costume();
    throw;
  }
}
StageInspection::~StageInspection() { output_.clear_world_draws(); }
void StageInspection::publish_view() {
  if ((!camera_ && !fixed_view_) || !environment_ || !lighting_) return;
  const auto& camera = camera_ ? camera_->view() : *fixed_view_;
  const auto& environment = *environment_;
  const auto directional = lighting_->directional_lighting();
  const auto& direction = directional.direction;
  const auto world =
      render::make_view_transforms(camera.pose, camera.perspective);
  const auto sky = sky_view(world, environment);
  const auto shadow = render::make_directional_shadow_projection(
      camera.pose, camera.perspective,
      initial_stage_shadow_settings(environment.shadow),
      {direction[0], direction[1], direction[2]});
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
           environment.shadow.casters_enabled}});
  output_.set_world_output_parameters(
      display_parameters(environment.tone_mapping));
  if (!static_lighting_updates_.empty()) {
    const auto packet = lighting_packet(lighting_->evaluate(
        {.policy = scene::ReceiverLightingPolicy::stage_ambient}));
    for (auto& update : static_lighting_updates_) {
      update.irradiance_coefficients = packet.irradiance_coefficients;
      update.local_lights = packet.local_lights;
      update.local_light_count = packet.local_light_count;
    }
    output_.update_world_lighting(static_lighting_updates_);
  }
}
AliciaActionUpdate StageInspection::begin_player_scripted_jump(
    const AcceptedScriptedJump& request) {
  if (!controller_) {
    throw std::logic_error("Scripted jump requires a physical player owner");
  }
  return controller_->begin_scripted_jump(request);
}
void StageInspection::bind_audio(const PreparedCharacterAudio& prepared,
                                 audio::CueOutput& output,
                                 SourceRandom& random) {
  if (audio_output_ != nullptr || !player_) {
    throw std::logic_error("Stage audio requires an unbound physical player");
  }
  player_->bind_audio({prepared, output, collision_, random}, memory_);
  output.set_listener(audio_listener_);
  player_->set_audio_listener(audio_listener_);
  output.pause(paused_);
  audio_output_ = &output;
}
void StageInspection::set_paused(bool paused) {
  if (paused == paused_) return;
  if (audio_output_ != nullptr) audio_output_->pause(paused);
  paused_ = paused;
}
void StageInspection::advance(double delta_seconds) {
  advance(delta_seconds, {});
}
void StageInspection::advance(double delta_seconds, StageTraversalInput input) {
  const double next = animation_seconds_ + delta_seconds;
  if (!std::isfinite(delta_seconds) || delta_seconds < 0 ||
      !std::isfinite(next)) {
    throw std::invalid_argument(
        "Stage animation requires finite nonnegative time");
  }
  if (input.pause_pressed) set_paused(!paused_);
  if (paused_) return;
  if (controller_ && camera_) {
    if (!player_) {
      throw std::logic_error("Traversal controller has no player owner");
    }
    auto& actor = player_.value();
    auto& controller = controller_.value();
    auto& camera = camera_.value();
    constexpr float kFireTriggerThreshold = .8F;
    const auto& view = camera.view().pose;
    if (audio_output_ != nullptr) {
      audio_listener_ = {view.eye, view.forward, view.up};
      audio_output_->set_listener(audio_listener_);
      actor.set_audio_listener(audio_listener_);
    }
    input.action.aim.anchor = math::orthonormal_basis(view.forward, view.up);
    math::set_translation(input.action.aim.anchor, view.eye);
    const auto* targeting_upper =
        actor.animation_state(CharacterMotionBank::upper_body);
    constexpr std::size_t kFirstTargetingUpper = 0x1d0;
    const bool fire_targeting =
        input.presentation.fire_trigger > kFireTriggerThreshold &&
        (targeting_upper == nullptr ||
         targeting_upper->binding >= kFirstTargetingUpper);
    const bool ordinary_shooter = controller.accepts_shooter_input();
    if (controller.action_owner().updates_shooter()) {
      targeting_.advance(
          static_cast<float>(delta_seconds),
          ordinary_shooter && input.presentation.aim_mode_pressed,
          ordinary_shooter && fire_targeting, automatic_targeting_camera_);
    }
    present_input(input.presentation, input.movement, delta_seconds);
    // The current host exposes normal horizontal look. Use the source initial
    // sensitivity field for its raised turn animation, not camera angular
    // speed. Persisted inversion/sensitivity options remain an explicit input
    // owner.
    constexpr std::uint8_t kInitialLookSensitivity = 60;
    if (input.look.right != 0) {
      input.action.aim.signed_turn_input =
          raised_turn_input(input.look.right, kInitialLookSensitivity, false);
    }
    input.action.aim.weapon_class = weapon_class_;
    input.action.aim.weapon_movement_scale =
        weapon_settings_.at(weapon_class_).movement_scale;
    input.action.aim.accepted_refresh =
        input.action.aim.accepted_refresh || targeting_.active();
    prepare_review_observations(input.action);
    double consumed = 0;
    if (review_fixed_pose_) {
      // Catalog inspection deliberately fixes physical placement; it still
      // uses the same four banks, pose modifiers, events, audio and renderer.
      (void)actor.advance_motion(delta_seconds);
      actor.publish_pose(controller.state().pose,
                         controller.state().orientation,
                         controller.body_radius(), output_);
      consumed = delta_seconds;
    } else if (review_next_phase_) {
      // An external phase acknowledgement is an edge, not a true value repeated
      // across every physical substep in a long host frame.
      const auto first = player_simulation_step(delta_seconds);
      consumed = controller.advance(input.movement, camera.yaw_radians(), first,
                                    output_, input.action);
      review_next_phase_ = false;
      prepare_review_observations(input.action);
      if (consumed == first && delta_seconds > first) {
        consumed +=
            controller.advance(input.movement, camera.yaw_radians(),
                               delta_seconds - first, output_, input.action);
      }
    } else {
      consumed = controller.advance(input.movement, camera.yaw_radians(),
                                    delta_seconds, output_, input.action);
    }
    if (review_entry_) {
      const auto outcome = controller.action_status().outcome;
      if (outcome == AliciaActionOutcome::dynamic_body_required) {
        set_review_status(
            "PREVIEW held at dynamic-body handoff (not implemented) | Menu: "
            "Stop");
      } else if (review_spell_ &&
                 controller.action_owner().kind() != AliciaActionKind::magic) {
        review_spell_.reset();
        set_review_status(
            "PREVIEW completed; no world spell outcome | Menu: Stop");
      }
    }
    camera.set_context(targeting_.camera_targeted()
                           ? PlayerCameraContext::targeting
                           : PlayerCameraContext::ordinary,
                       controller.state().stance == AliciaStance::crouched);
    camera.advance(controller.state().pose, input.look,
                   review_entry_ ? delta_seconds : consumed);
    if (audio_output_ != nullptr) audio_output_->update();
  } else if (player_) {
    player_->advance(delta_seconds, output_);
    if (audio_output_ != nullptr) audio_output_->update();
  }
  if (environment_channels_ && environment_ && lighting_ &&
      (camera_ || fixed_view_)) {
    const auto& camera = camera_ ? camera_->view() : fixed_view_.value();
    environment_channels_->update(camera.pose.eye);
    environment_ =
        stage_environment_snapshot(*environment_, *environment_channels_);
    const auto& value = *environment_;
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
  }
  publish_view();
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
  animation_seconds_ = next;
}
}  // namespace bw::game
