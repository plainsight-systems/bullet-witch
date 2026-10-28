// Original ordered sprite update and CPU quad publication. Core SL.con.3
// (#rsl-bounds) bounds fixed pools and generation checks; performance MEM.9
// (guidelines/memory/MEM.9-embedded-allocate-at-init.md) keeps successful frame
// work allocation-free. Both implementation corpora reread. All vector/matrix
// arithmetic uses bw::math; effects owns only source-specific axis conventions,
// timing, curve selection, random ordering and resource identities.
#include "bw/effects/sprite_world.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <thread>

#include "bw/effects/particle_sampling.hpp"
#include "bw/math/euler.hpp"
#include "bw/memory/allocator.hpp"

namespace bw::effects {
namespace {
constexpr float kQuarterTurn = 1.5707963705062866F;
constexpr float kHalfTurn = 3.1415927410125732F;
constexpr float kAngularFrames = 60;
constexpr float kMinimumLife = .01F;
constexpr float kBindMinimumPeriod = .001F;
constexpr float kUpdateMinimumPeriod = .0001F;
constexpr float kWindSeedScale = .00003051800013054162F;
constexpr std::size_t kQuadVertices = 6;
constexpr float kFinalCurveSample = 31;
constexpr float kDepthOrderEpsilon = .0001220703125F;
constexpr float kSortShrink = .7692307829856873F;
constexpr float kSortInitialDivisor = 1.2999999523162842F;
constexpr std::size_t kSortGapNine = 9, kSortGapTen = 10, kSortGapEleven = 11;
constexpr float kAxialParallelThreshold = .9999899864196777F;
struct Particle {
  SpriteBasis basis{};
  SpriteVector position{}, velocity{}, angular_velocity{};
  float roll{}, age{}, life{}, sample_scale{}, width{}, height{};
  std::uint16_t seed{};
  std::uint8_t texture{};
  bool collided{};
};
struct Emitter {
  std::size_t first{}, count{};
  float age{}, next_emission{};
  bool initial_burst{true};
};
struct Instance {
  const SpriteProgram* program{};
  SpritePlacement placement{};
  std::array<Emitter, formats::kEffectPartSlots> emitters{};
  std::uint32_t generation{};
  float age{}, fading{-1}, depth{};
};
SpriteBasis basis(const math::Affine3<float>& transform) {
  return {transform[0], transform[1], transform[2]};
}
SpriteBasis forward_basis(const SpriteVector& forward) {
  const SpriteVector up = forward[0] == 0 && forward[2] == 0
                              ? SpriteVector{1, 0, 0}
                              : SpriteVector{0, 1, 0};
  const auto frame = math::orthonormal_basis(forward, up);
  return {{{frame[0][0], frame[0][1], frame[0][2]},
           {frame[1][0], frame[1][1], frame[1][2]},
           {frame[2][0], frame[2][1], frame[2][2]}}};
}
SpriteVector direction(const SpriteVector& value,
                       const SpriteBasis& transform) {
  return math::multiply(math::transpose(transform), value);
}
float sample(const std::optional<formats::EffectScalarCurve>& curve,
             const Particle& particle) {
  if (!curve) return 1;
  const float coordinate = particle.age * particle.sample_scale;
  const auto index = static_cast<std::uint32_t>(coordinate);
  return sample_scalar_curve(*curve, index,
                             coordinate - static_cast<float>(index));
}
SpriteVector velocity(const SpritePart& part, const Particle& particle) {
  auto result = particle.velocity;
  if (!particle.collided) {
    if (part.uniform_velocity_curve) {
      result = math::scale(result, sample(part.velocity_curves[0], particle));
    } else {
      for (std::size_t lane = 0; lane < 3; ++lane) {
        result.at(lane) *= sample(part.velocity_curves.at(lane), particle);
      }
    }
    result = math::add(result, math::scale(part.acceleration, particle.age));
  }
  return result;
}
SpriteVector jitter(ParticleRandom& random, const SpriteVector& radius) {
  return {random.between(-radius[0], radius[0]),
          random.between(-radius[1], radius[1]),
          random.between(-radius[2], radius[2])};
}
math::Affine3<float> emitter_transform(const SpritePart& part,
                                       const SpritePlacement& placement) {
  if (part.emitter_transform_mode == 0) return placement.transform;
  const auto rotation = math::euler_zxy_rotation(part.emitter_rotation);
  const auto rotated = math::multiply(rotation, basis(placement.transform));
  return {rotated[0], rotated[1], rotated[2], placement.transform[3]};
}
Particle create_particle(const SpritePart& part,
                         const math::Affine3<float>& transform,
                         ParticleRandom& random) {
  Particle result;
  result.seed = random.next();
  const auto speed =
      random.between(-part.speed_jitter, part.speed_jitter) + part.speed;
  const auto pitch =
      random.between(-part.direction_jitter[0], part.direction_jitter[0]);
  const auto yaw =
      random.between(-part.direction_jitter[1], part.direction_jitter[1]);
  const auto base_direction = math::euler_zxy_rotation(
      SpriteVector{part.direction[0], part.direction[1], 0});
  const auto spread = math::euler_zxy_rotation(SpriteVector{pitch, yaw, 0});
  const auto direction_rotation = math::multiply(spread, base_direction);
  result.velocity = direction(SpriteVector{0, 0, speed}, direction_rotation);
  const auto angles =
      math::add(part.rotation, jitter(random, part.rotation_jitter));
  const bool authored = part.orientation == SpriteOrientation::authored ||
                        part.orientation == SpriteOrientation::axial;
  const auto plane_y =
      math::euler_zxy_rotation(SpriteVector{0, kQuarterTurn, 0});
  if (authored) {
    result.basis = math::euler_zxy_rotation(angles);
    if (part.align_velocity &&
        math::dot(result.velocity, result.velocity) > 0) {
      result.basis = math::multiply(
          result.basis,
          math::multiply(plane_y, forward_basis(result.velocity)));
    }
    if (part.basis_plane != 0) {
      const auto plane =
          part.basis_plane == 1
              ? math::euler_zxy_rotation(SpriteVector{kQuarterTurn, 0, 0})
              : plane_y;
      result.basis = math::multiply(plane, result.basis);
    }
  } else {
    result.basis = math::identity_matrix<float, 3>();
    result.roll = angles[2];
  }
  result.position = part.position;
  if (part.outer_radius > 0) {
    const auto raw_direction = jitter(random, part.shape_axes);
    const auto unit = math::divide(raw_direction, math::length(raw_direction));
    const auto radial_sample = random.between(0, 1);
    const auto radius = (1 - radial_sample * radial_sample) *
                            (part.outer_radius - part.inner_radius) +
                        part.inner_radius;
    result.position = math::add(result.position, math::scale(unit, radius));
  }
  if (part.radial_direction != 0) {
    const auto inward = math::subtract(part.position, result.position);
    if (math::dot(inward, inward) > 0) {
      const auto radial_frame = forward_basis(inward);
      const auto directed = math::multiply(direction_rotation, radial_frame)[2];
      const auto magnitude = math::length(result.velocity) *
                             (part.radial_direction == 1 ? 1.F : -1.F);
      result.velocity = math::scale(directed, magnitude);
      result.basis = math::multiply(
          result.basis, math::multiply(plane_y, forward_basis(directed)));
    }
  }
  if (part.world_space) {
    result.basis = math::multiply(result.basis, basis(transform));
    result.position =
        math::transform_point_basis_first(result.position, transform);
    result.velocity =
        math::transform_direction<float>(result.velocity, transform);
  }
  if (authored) {
    result.angular_velocity =
        math::add(part.angular_velocity, jitter(random, part.angular_jitter));
  } else {
    result.angular_velocity[2] =
        random.between(-part.angular_jitter[2], part.angular_jitter[2]) +
        part.angular_velocity[2];
  }
  result.life =
      random.guarded_jitter(part.life, part.life_jitter, kMinimumLife);
  result.sample_scale = kFinalCurveSample / result.life;
  const auto size_jitter = part.size_jitter_percent * .01F;
  const auto scale = 1 - random.between(-size_jitter, size_jitter);
  result.width = part.width * scale;
  result.height = part.height * scale;
  result.texture =
      static_cast<std::uint8_t>(random.next() % part.texture_count);
  return result;
}
void advance_particle(const SpritePart& part, Particle& particle,
                      const math::Affine3<float>& transform, float delta,
                      SpriteHandle owner, ParticleRandom& random,
                      const SpriteEnvironment& environment) {
  particle.age += delta;
  const auto angular_scale =
      delta * kAngularFrames * sample(part.angular_curve, particle);
  if (part.orientation == SpriteOrientation::authored ||
      part.orientation == SpriteOrientation::axial) {
    if (part.rotate_basis) {
      particle.basis =
          math::multiply(math::euler_zxy_rotation(math::scale(
                             particle.angular_velocity, angular_scale)),
                         particle.basis);
    }
  } else {
    particle.roll += angular_scale * particle.angular_velocity[2];
  }
  if (particle.collided) {
    particle.velocity =
        math::add(particle.velocity, math::scale(part.acceleration, delta));
  }
  auto motion = velocity(part, particle);
  if (part.wind_strength > 0) {
    auto force = environment.force_at(particle.position, owner);
    const auto factor =
        (static_cast<float>(particle.seed) * kWindSeedScale * .6F + .7F) *
        part.wind_strength;
    force = math::scale(force, factor);
    if (!part.world_space) {
      force = direction(force, math::transpose(basis(transform)));
    }
    motion = math::add(motion, force);
  }
  auto candidate = math::add(particle.position, math::scale(motion, delta));
  if (part.collision_offset > 0) {
    if (const auto contact =
            environment.intersect(particle.position, candidate)) {
      const auto attenuation =
          -(static_cast<float>(random.next()) * .2999999523162842F *
                .000030518509447574615F +
            .8500000238418579F) *
          part.collision_attenuation;
      const auto angle =
          (random.between(-part.collision_angle, part.collision_angle) + 1) *
          kHalfTurn;
      const auto half = math::planar_rotation(angle * .5F);
      const auto axis = math::divide(contact->response_axis,
                                     math::length(contact->response_axis));
      const auto rotation =
          math::affine_transform(math::TransformComponents<float>{
              {axis[0] * half.sine, axis[1] * half.sine, axis[2] * half.sine,
               half.cosine},
              {0, 0, 0},
              {1, 1, 1}});
      particle.velocity = math::transform_direction<float>(
          math::scale(motion, attenuation), rotation);
      particle.collided = true;
      candidate =
          math::add(contact->position,
                    math::scale(contact->response_axis, part.collision_offset));
    }
  }
  particle.position = candidate;
}
std::array<float, 4> color(const SpritePart& part, const Particle& particle) {
  const auto intensity =
      part.intensity * sample(part.intensity_curve, particle);
  SpriteVector rgb{intensity, intensity, intensity};
  if (part.color_curve) {
    const auto& curve = *part.color_curve;
    if (curve.back()[0] < 0) {
      rgb = math::scale(curve[0], intensity);
    } else {
      const auto coordinate =
          part.random_color ? static_cast<float>(particle.seed) *
                                  .000030518509447574615F * kFinalCurveSample
                            : particle.age * particle.sample_scale;
      const auto index = std::min(static_cast<std::size_t>(coordinate),
                                  formats::kEffectCurveSamples - 1);
      const auto fraction = coordinate - static_cast<float>(index);
      // Endpoint access is bounded: the original evaluator's next sample is
      // mathematically unused at integer 31, but its load crosses the record.
      rgb = math::scale(
          math::add(
              math::scale(curve.at(index), 1 - fraction),
              math::scale(curve.at(std::min(index + 1,
                                            formats::kEffectCurveSamples - 1)),
                          fraction)),
          intensity);
    }
  }
  return {rgb[0], rgb[1], rgb[2], sample(part.alpha_curve, particle)};
}
SpriteBasis oriented_frame(const SpritePart& part, const Particle& particle,
                           SpriteBasis frame, const SpriteCamera& camera) {
  if (part.orientation == SpriteOrientation::axial) {
    const auto in_view = math::multiply(frame, camera.view_rotation);
    if (in_view[2][2] < kAxialParallelThreshold) {
      const auto angle =
          static_cast<float>(std::atan2(-static_cast<double>(in_view[0][2]),
                                        static_cast<double>(in_view[1][2])));
      frame = math::multiply(
          math::euler_zxy_rotation(SpriteVector{0, 0, angle}), frame);
    }
    frame = math::multiply(
        math::euler_zxy_rotation(SpriteVector{kQuarterTurn, 0, 0}), frame);
  } else if (part.orientation != SpriteOrientation::authored) {
    float roll = particle.roll;
    if (part.orientation == SpriteOrientation::velocity) {
      const auto projected =
          direction(velocity(part, particle), camera.view_rotation);
      roll = -static_cast<float>(std::atan2(static_cast<double>(projected[0]),
                                            static_cast<double>(projected[1])));
    }
    frame = part.orientation == SpriteOrientation::cylindrical
                ? camera.cylindrical_billboard
                : camera.billboard;
    if (roll != 0) {
      frame = math::multiply(math::euler_zxy_rotation(SpriteVector{0, 0, roll}),
                             frame);
    }
  }
  return frame;
}
void validate(const SpritePlacement& placement) {
  for (const auto& row : placement.transform) {
    if (!math::all_finite(row)) {
      throw std::invalid_argument("Nonfinite effect transform");
    }
  }
  if (!math::all_finite(placement.tint) ||
      !math::all_finite(placement.size_scale)) {
    throw std::invalid_argument("Nonfinite effect tint or size");
  }
}
}  // namespace

SpriteCamera make_sprite_camera(const math::Affine3<float>& view,
                                const SpriteVector& eye) {
  if (!math::all_finite(view) || !math::all_finite(eye)) {
    throw std::invalid_argument("Nonfinite sprite camera observation");
  }
  const SpriteBasis rotation{view[0], view[1], view[2]};
  constexpr SpriteVector up{0, 1, 0};
  const auto right =
      math::normalize_or(math::cross(up, rotation[2]), SpriteVector{}, 0.F);
  const auto forward =
      math::normalize_or(math::cross(right, up), SpriteVector{}, 0.F);
  if (math::dot(right, right) == 0 || math::dot(forward, forward) == 0) {
    throw std::invalid_argument("Degenerate fixed-up sprite camera");
  }
  return {eye, view[3], rotation, math::transpose(rotation),
          math::transpose(SpriteBasis{right, up, forward})};
}

struct SpriteWorld::State {
  State(const std::shared_ptr<memory::Domain>& domain,
        const world::WorldClock& owner, ParticleRandom& random_stream,
        std::span<const SpriteProgram> resources,
        SpriteWorldConfiguration configuration)
      : clock(owner),
        programs(resources),
        random(random_stream),
        instances(memory::Allocator<Instance>(domain)),
        particles(memory::Allocator<Particle>(domain)),
        order(memory::Allocator<std::uint32_t>(domain)),
        vertices(memory::Allocator<SpriteVertex>(domain)),
        batches(memory::Allocator<SpriteBatch>(domain)) {
    const auto capacity = configuration.instance_capacity;
    if (capacity == 0 || programs.empty()) {
      throw std::invalid_argument("Empty sprite world capacity");
    }
    for (const auto& program : programs) {
      std::size_t count = 0;
      for (const auto& part : program.parts()) count += part.capacity;
      particles_per_instance = std::max(particles_per_instance, count);
    }
    if (particles_per_instance == 0 ||
        capacity > std::numeric_limits<std::uint32_t>::max() /
                       particles_per_instance / kQuadVertices) {
      throw std::length_error("Sprite world capacity overflow");
    }
    instances.resize(capacity);
    particles.resize(capacity * particles_per_instance);
    order.reserve(capacity);
    vertices.reserve(particles.size() * kQuadVertices);
    batches.reserve(particles.size());
  }
  ~State() = default;
  State(const State&) = delete;
  State& operator=(const State&) = delete;
  State(State&&) = delete;
  State& operator=(State&&) = delete;
  void thread_check() const {
    if (std::this_thread::get_id() != thread) {
      throw std::logic_error("Sprite world used off owner thread");
    }
  }
  Instance* find(SpriteHandle handle) {
    if (handle.slot >= instances.size()) return nullptr;
    auto& instance = instances.at(handle.slot);
    return instance.program && instance.generation == handle.generation
               ? &instance
               : nullptr;
  }
  void burst(const SpritePart& part, Emitter& emitter,
             const SpritePlacement& placement) {
    const auto count = random.count(part.count, part.count_jitter);
    const auto transform = emitter_transform(part, placement);
    for (std::int32_t i = 0; i < count && emitter.count < part.capacity; ++i) {
      particles.at(emitter.first + emitter.count++) =
          create_particle(part, transform, random);
    }
  }
  void retire(Instance& instance) { instance.program = nullptr; }
  const world::WorldClock& clock;
  std::span<const SpriteProgram> programs;
  ParticleRandom& random;
  const std::thread::id thread{std::this_thread::get_id()};
  std::size_t particles_per_instance{};
  std::uint64_t sequence{};
  memory::Vector<Instance> instances;
  memory::Vector<Particle> particles;
  memory::Vector<std::uint32_t> order;
  memory::Vector<SpriteVertex> vertices;
  memory::Vector<SpriteBatch> batches;
};
SpriteWorld::SpriteWorld(const std::shared_ptr<memory::Domain>& domain,
                         const world::WorldClock& clock, ParticleRandom& random,
                         std::span<const SpriteProgram> programs,
                         SpriteWorldConfiguration configuration)
    : state_(std::allocate_shared<State>(memory::Allocator<State>(domain),
                                         domain, clock, random, programs,
                                         configuration)) {}
SpriteWorld::~SpriteWorld() = default;
std::optional<SpriteHandle> SpriteWorld::spawn(
    std::uint32_t effect, const SpritePlacement& placement) {
  auto& state = *state_;
  state.thread_check();
  validate(placement);
  const auto program =
      std::ranges::find(state.programs, effect, &SpriteProgram::id);
  if (program == state.programs.end()) {
    throw std::invalid_argument("Unprepared effect identity");
  }
  const auto free = std::ranges::find_if(
      state.instances, [](const auto& item) { return !item.program; });
  if (free == state.instances.end()) return std::nullopt;
  auto& instance = *free;
  if (instance.generation == std::numeric_limits<std::uint32_t>::max()) {
    throw std::overflow_error("Effect handle generation exhausted");
  }
  const auto slot = static_cast<std::uint32_t>(free - state.instances.begin());
  instance.program = &*program;
  instance.placement = placement;
  if (program->translation_only()) {
    const auto translation = instance.placement.transform[3];
    instance.placement.transform = {
        {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}, translation}};
  }
  ++instance.generation;
  instance.age = 0;
  instance.fading = -1;
  std::size_t first = slot * state.particles_per_instance, index = 0;
  for (const auto& part : program->parts()) {
    auto& emitter = instance.emitters.at(index++);
    emitter = {first, 0, 0, -1, true};
    if (part.period > 0) {
      emitter.next_emission =
          part.delay + state.random.guarded_jitter(
                           part.period, part.period_jitter, kBindMinimumPeriod);
    }
    first += part.capacity;
  }
  state.order.push_back(slot);
  return SpriteHandle{slot, instance.generation};
}
bool SpriteWorld::move(SpriteHandle handle, const SpritePlacement& placement) {
  auto& state = *state_;
  state.thread_check();
  validate(placement);
  auto* instance = state.find(handle);
  if (!instance) return false;
  instance->placement = placement;
  if (instance->program->translation_only()) {
    const auto translation = instance->placement.transform[3];
    instance->placement.transform = {
        {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}, translation}};
  }
  return true;
}
bool SpriteWorld::stop(SpriteHandle handle) {
  auto& state = *state_;
  state.thread_check();
  auto* instance = state.find(handle);
  if (!instance) return false;
  if (instance->program->fade_seconds() == 0) {
    state.retire(*instance);
    std::erase(state.order, handle.slot);
  } else if (instance->fading < 0) {
    instance->fading = 0;
  }
  return true;
}
bool SpriteWorld::alive(SpriteHandle handle) const {
  state_->thread_check();
  return state_->find(handle) != nullptr;
}
void SpriteWorld::advance(const world::WorldFrame& frame,
                          const SpriteEnvironment& environment) {
  auto& state = *state_;
  state.thread_check();
  if (!frame.belongs_to(state.clock) || frame.sequence() <= state.sequence) {
    throw std::invalid_argument("Stale or foreign sprite world frame");
  }
  state.sequence = frame.sequence();
  if (!frame.admission().simulation) return;
  const auto delta = frame.delta_seconds();
  for (const auto slot : state.order) {
    auto& instance = state.instances.at(slot);
    if (!instance.program) continue;
    const auto parts = instance.program->parts();
    for (std::size_t i = 0; i < parts.size(); ++i) {
      const auto& part = parts[i];
      auto& emitter = instance.emitters.at(i);
      const auto transform = emitter_transform(part, instance.placement);
      if (part.delay <= emitter.age) {
        if (emitter.initial_burst && instance.fading < 0) {
          state.burst(part, emitter, instance.placement);
          emitter.initial_burst = false;
        }
        std::size_t remaining = 0;
        for (std::size_t p = 0; p < emitter.count; ++p) {
          auto& particle = state.particles.at(emitter.first + p);
          advance_particle(part, particle, transform, delta,
                           {slot, instance.generation}, state.random,
                           environment);
          if (particle.age < particle.life) {
            if (remaining != p) {
              state.particles.at(emitter.first + remaining) = particle;
            }
            ++remaining;
          }
        }
        emitter.count = remaining;
      }
      emitter.age += delta;
      if (part.duration > 0 && part.delay + part.duration <= emitter.age) {
        continue;
      }
      if (instance.fading < 0 && emitter.next_emission > 0 &&
          emitter.next_emission < emitter.age) {
        emitter.next_emission += state.random.guarded_jitter(
            part.period, part.period_jitter, kUpdateMinimumPeriod);
        state.burst(part, emitter, instance.placement);
      }
    }
    instance.age += delta;
    if (instance.age >= instance.program->lifetime() && instance.fading < 0) {
      instance.fading = 0;
    }
    if (instance.fading >= 0) {
      instance.fading += delta;
      if (instance.program->fade_seconds() == 0 ||
          instance.fading > instance.program->fade_seconds()) {
        state.retire(instance);
      }
    }
  }
  std::erase_if(state.order,
                [&](auto slot) { return !state.instances.at(slot).program; });
}
std::size_t SpriteWorld::active_instances() const {
  state_->thread_check();
  return static_cast<std::size_t>(std::ranges::count_if(
      state_->instances,
      [](const auto& item) { return item.program != nullptr; }));
}
std::size_t SpriteWorld::active_particles() const {
  state_->thread_check();
  std::size_t count = 0;
  for (const auto& instance : state_->instances) {
    if (instance.program) {
      for (std::size_t i = 0; i < instance.program->parts().size(); ++i) {
        count += instance.emitters.at(i).count;
      }
    }
  }
  return count;
}
SpriteDraw SpriteWorld::publish(const SpriteCamera& camera) {
  auto& state = *state_;
  state.thread_check();
  if (!math::all_finite(camera.position) ||
      !math::all_finite(camera.view_translation) ||
      !math::all_finite(camera.view_rotation) ||
      !math::all_finite(camera.billboard) ||
      !math::all_finite(camera.cylindrical_billboard)) {
    throw std::invalid_argument("Nonfinite sprite camera");
  }
  // Original publication sorts the active list itself; next simulation update
  // consumes this same order and therefore the same shared RNG sequence.
  float epsilon = kDepthOrderEpsilon;
  for (const auto slot : state.order) {
    auto& instance = state.instances.at(slot);
    const auto& point = instance.placement.transform[3];
    const auto& view = camera.view_rotation;
    instance.depth = ((view[1][2] * point[1] + view[0][2] * point[0] +
                       view[2][2] * point[2]) +
                      camera.view_translation[2]) +
                     instance.program->sort_bias() - epsilon;
    epsilon += kDepthOrderEpsilon;
  }
  // Source comb sort retains its gap schedule and equal-key behavior.
  auto gap = static_cast<std::size_t>(static_cast<float>(state.order.size()) /
                                      kSortInitialDivisor);
  std::size_t last_swap = 0;
  if (state.order.size() > 1) {
    bool again = true;
    while (again) {
      if (gap == 0) gap = 1;
      while (gap > 0) {
        if (gap == kSortGapNine || gap == kSortGapTen) gap = kSortGapEleven;
        last_swap = 0;
        for (std::size_t i = 0; i + gap < state.order.size(); ++i) {
          if (state.instances.at(state.order.at(i)).depth <
              state.instances.at(state.order.at(i + gap)).depth) {
            std::swap(state.order.at(i), state.order.at(i + gap));
            last_swap = i;
          }
        }
        gap = static_cast<std::size_t>(static_cast<float>(gap) * kSortShrink);
      }
      again = last_swap != 0;
    }
  }
  state.vertices.clear();
  state.batches.clear();
  for (const auto slot : state.order) {
    const auto& instance = state.instances.at(slot);
    if (!instance.program) continue;
    const auto parts = instance.program->parts();
    const float fade =
        instance.fading < 0
            ? 1
            : 1 - instance.fading / instance.program->fade_seconds();
    for (std::size_t i = 0; i < parts.size(); ++i) {
      const auto& part = parts[i];
      const auto& emitter = instance.emitters.at(i);
      const auto transform = emitter_transform(part, instance.placement);
      for (std::size_t p = 0; p < emitter.count; ++p) {
        const auto& particle = state.particles.at(emitter.first + p);
        auto rgba = color(part, particle);
        for (std::size_t lane = 0; lane < 4; ++lane) {
          if (lane == 3 || !part.ignore_tint) {
            rgba.at(lane) *= instance.placement.tint.at(lane);
          }
        }
        rgba[3] *= fade;

        auto frame = particle.basis;
        auto position = particle.position;
        if (!part.world_space) {
          frame = math::multiply(frame, basis(transform));
          position = math::transform_point_basis_first(position, transform);
        }
        if (part.distance_fade) {
          const auto delta = math::subtract(position, camera.position);
          const float squared =
              delta[1] * delta[1] + delta[0] * delta[0] + delta[2] * delta[2];
          rgba[3] *= std::clamp(
              (squared - part.fade_near_squared) * part.fade_squared_reciprocal,
              0.F, 1.F);
        }
        if (rgba[3] <= kMinimumLife) continue;
        frame = oriented_frame(part, particle, frame, camera);
        float width = particle.width * instance.placement.size_scale[0];
        float height = particle.height * instance.placement.size_scale[1];
        if (part.sample_size) {
          width *= sample(part.width_curve, particle);
          height *= sample(part.height_curve, particle);
        }
        const auto right = math::scale(frame[0], width);
        const auto up = math::scale(frame[1], height);
        const auto upper_right = math::add(math::add(right, up), position);
        const auto upper_left =
            math::add(math::add(math::scale(frame[0], -width), up), position);
        const auto twice_center = math::scale(position, 2.F);
        const std::array<SpriteVector, 4> corners{
            upper_left, math::subtract(twice_center, upper_right),
            math::subtract(twice_center, upper_left), upper_right};
        const auto& region = part.textures.at(particle.texture);
        const auto [u0, v0, u1, v1] = region.uv;
        const std::array<std::array<float, 2>, 4> uv{
            {{u0, v0}, {u0, v1}, {u1, v1}, {u1, v0}}};
        const auto first = static_cast<std::uint32_t>(state.vertices.size());
        for (const auto corner : std::array<std::size_t, 6>{0, 1, 2, 2, 3, 0}) {
          state.vertices.push_back({corners.at(corner), rgba, uv.at(corner)});
        }
        if (!state.batches.empty() &&
            state.batches.back().pack_slot == instance.program->pack_slot() &&
            state.batches.back().texture_index == region.texture.index &&
            state.batches.back().blend == part.blend) {
          state.batches.back().vertex_count += kQuadVertices;
        } else {
          state.batches.push_back({first, kQuadVertices,
                                   instance.program->pack_slot(),
                                   region.texture.index, part.blend});
        }
      }
    }
  }
  return {state.vertices, state.batches};
}
}  // namespace bw::effects
