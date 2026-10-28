// Compose prepared AI owners without importing movement, physics or renderer
// mutation. Core E.4 rejects unknown trees/reentrancy before lifecycle changes;
// I.1 exposes separate query/command dependencies. MEM.9 retains each Lua VM
// in domain storage and performs no runtime archive work. Source behavior and
// remaining external-producer contracts are in first-combat-ai.md.
#include "npc_brain.hpp"

#include <algorithm>
#include <stdexcept>
#include <thread>

#include "behavior_lua.hpp"
namespace bw::game {
namespace {
std::string_view canonical(std::string_view name) {
  if (name.starts_with("ai.")) name.remove_prefix(3);
  if (name.ends_with(".amb")) name.remove_suffix(4);
  return name;
}
class DispatchGuard {
 public:
  explicit DispatchGuard(bool& busy) : busy_(busy) {
    if (busy_) throw std::logic_error("Reentrant NPC brain dispatch");
    busy_ = true;
  }
  ~DispatchGuard() { busy_ = false; }
  DispatchGuard(const DispatchGuard&) = delete;
  DispatchGuard& operator=(const DispatchGuard&) = delete;
  DispatchGuard(DispatchGuard&&) = delete;
  DispatchGuard& operator=(DispatchGuard&&) = delete;

 private:
  bool& busy_;
};
}  // namespace
// A VM never retains an actor. Only the private call scope routes observations;
// shared immutable function chunks avoid one Lua heap per actor per wrapper.
struct NpcBehaviorLibrary::Impl final : BehaviorObservationSource {
  struct Vm {
    Vm(const Vm&) = delete;
    Vm& operator=(const Vm&) = delete;
    Vm(Vm&&) = delete;
    Vm& operator=(Vm&&) = delete;
    ~Vm() = default;
    std::string_view name;
    const formats::BehaviorTreeDefinition& definition;
    script::Context context;
    Vm(NpcBehaviorDefinition source, Impl& owner,
       const std::shared_ptr<memory::Domain>& domain)
        : name(canonical(source.name)),
          definition(*source.definition),
          context(domain, source.lua_heap_bytes) {
      bind_scoped_behavior_observations(context, owner.identity, owner);
      context.load_binary(definition.bytecode());
    }
  };
  std::thread::id thread{std::this_thread::get_id()};
  std::uint32_t identity{};
  const BehaviorObservationSource* source{};
  memory::Vector<std::shared_ptr<Vm>> machines;
  Impl(std::span<const NpcBehaviorDefinition> definitions,
       const std::shared_ptr<memory::Domain>& domain)
      : machines(memory::Allocator<std::shared_ptr<Vm>>(domain)) {
    if (definitions.empty()) {
      throw std::invalid_argument("Empty NPC behavior closure");
    }
    machines.reserve(definitions.size());
    for (const auto& item : definitions) {
      if (!item.definition || item.lua_heap_bytes == 0 ||
          canonical(item.name).empty() ||
          std::ranges::any_of(machines, [&](const auto& vm) {
            return vm->name == canonical(item.name);
          })) {
        throw std::invalid_argument("Invalid NPC behavior closure identity");
      }
      machines.push_back(std::allocate_shared<Vm>(memory::Allocator<Vm>(domain),
                                                  item, *this, domain));
    }
  }
  void check_thread() const {
    if (thread != std::this_thread::get_id()) {
      throw std::logic_error("NPC scoring on foreign thread");
    }
  }
  const BehaviorObservationSource& facts() const {
    if (!source) throw std::logic_error("NPC scoring outside actor scope");
    return *source;
  }
  double scalar(BehaviorObservation key) const override {
    return facts().scalar(key);
  }
  bool callback_on(std::string_view name) const override {
    return facts().callback_on(name);
  }
  BehaviorScriptSettings script_settings() const override {
    return facts().script_settings();
  }
  bool previous_labels(std::int32_t depth, std::int32_t label) const override {
    return facts().previous_labels(depth, label);
  }
  double path_distance_squared(std::int32_t point) const override {
    return facts().path_distance_squared(point);
  }
  double path_angle(std::int32_t point) const override {
    return facts().path_angle(point);
  }
  struct Scope {
    Impl& owner;
    Scope(Impl& value, std::uint32_t actor,
          const BehaviorObservationSource& facts)
        : owner(value) {
      owner.check_thread();
      if (owner.source || actor == 0) {
        throw std::logic_error("Invalid or reentrant NPC scoring scope");
      }
      owner.identity = actor;
      owner.source = &facts;
    }
    ~Scope() {
      owner.source = nullptr;
      owner.identity = 0;
    }
    Scope(const Scope&) = delete;
    Scope& operator=(const Scope&) = delete;
    Scope(Scope&&) = delete;
    Scope& operator=(Scope&&) = delete;
  };
};
NpcBehaviorLibrary::NpcBehaviorLibrary(
    std::span<const NpcBehaviorDefinition> definitions,
    const std::shared_ptr<memory::Domain>& domain)
    : impl_(std::allocate_shared<Impl>(memory::Allocator<Impl>(domain),
                                       definitions, domain)) {}
NpcBehaviorLibrary::~NpcBehaviorLibrary() = default;
NpcBehaviorId NpcBehaviorLibrary::resolve(std::string_view name) const {
  impl_->check_thread();
  name = canonical(name);
  for (std::size_t i = 0; i < impl_->machines.size(); ++i) {
    if (impl_->machines[i]->name == name) return {i};
  }
  throw std::out_of_range("NPC behavior outside prepared closure");
}
std::string_view NpcBehaviorLibrary::name(NpcBehaviorId id) const {
  impl_->check_thread();
  return impl_->machines.at(id.value)->name;
}
const formats::BehaviorTreeDefinition& NpcBehaviorLibrary::definition(
    NpcBehaviorId id) const {
  impl_->check_thread();
  return impl_->machines.at(id.value)->definition;
}
float NpcBehaviorLibrary::evaluate(
    NpcBehaviorId id, const formats::BehaviorNode& node, std::uint32_t identity,
    const BehaviorObservationSource& observations) {
  Impl::Scope scope(*impl_, identity, observations);
  return evaluate_behavior(impl_->machines.at(id.value)->context, identity,
                           node);
}
struct NpcBrain::Impl final : BehaviorObservationSource, BehaviorScoring {
  const world::WorldClock& clock;
  SourceRandom& random;
  const BehaviorObservationSource& observations;
  NpcPerception perception;
  NpcPerception secondary_perception;
  NativeActions actions;
  BehaviorRuntime runtime;
  NpcBehaviorLibrary& behaviors;
  std::uint32_t identity;
  std::optional<NpcBehaviorId> selected, original;
  float level_random{};
  std::uint64_t sequence{};
  bool busy{};
  Impl(const world::WorldClock& world_clock, BehaviorSchedule& schedule,
       PerceptionSchedule& perception_schedule, SourceRandom& source_random,
       NpcBehaviorLibrary& prepared_behaviors,
       const NativeActionLibrary& library, PerceptionSettings settings,
       std::uint32_t identity, const BehaviorObservationSource& source,
       const NpcActionQueries& queries, NpcActionReceiver& receiver,
       const std::shared_ptr<memory::Domain>& domain)
      : clock(world_clock),
        random(source_random),
        observations(source),
        perception(world_clock, perception_schedule, settings),
        secondary_perception(world_clock, perception_schedule, settings, 1),
        actions(library, queries, receiver, source_random, domain),
        runtime(world_clock, schedule, actions),
        behaviors(prepared_behaviors),
        identity(identity) {
    if (identity == 0) {
      throw std::invalid_argument("Invalid NPC actor identity");
    }
  }
  void begin_level() override { level_random = random.unit(); }
  float evaluate(const formats::BehaviorNode& node) override {
    if (!selected) throw std::logic_error("NPC scorer has no selected tree");
    return behaviors.evaluate(*selected, node, identity, *this);
  }
  double scalar(BehaviorObservation key) const override {
    switch (key) {
      case BehaviorObservation::enemy_knowledge:
        return static_cast<double>(perception.state().knowledge);
      case BehaviorObservation::completion_code:
        return runtime.current_node()
                   ? actions.state(runtime.active_slot()).completion_code
                   : 0;
      case BehaviorObservation::elapsed_seconds:
        return runtime.elapsed_seconds();
      case BehaviorObservation::level_random:
        return level_random;
      default:
        return observations.scalar(key);
    }
  }
  bool callback_on(std::string_view name) const override {
    return observations.callback_on(name);
  }
  BehaviorScriptSettings script_settings() const override {
    return observations.script_settings();
  }
  bool previous_labels(std::int32_t depth, std::int32_t label) const override {
    return runtime.previous_labels({depth, label});
  }
  double path_distance_squared(std::int32_t point) const override {
    return observations.path_distance_squared(point);
  }
  double path_angle(std::int32_t point) const override {
    return observations.path_angle(point);
  }
  struct Selection {
    NpcBehaviorId index{};
    std::size_t entry{};
  };
  void select(Selection selection) {
    const auto [index, entry] = selection;
    const auto& tree = behaviors.definition(index);
    if (entry == 0 || entry > tree.entries().size()) {
      throw std::out_of_range("NPC behavior entry is outside prepared tree");
    }
    selected = index;
    runtime.start(tree, *this, entry);
  }
};
NpcBrain::NpcBrain(const world::WorldClock& clock, BehaviorSchedule& schedule,
                   PerceptionSchedule& perception_schedule,
                   SourceRandom& random, NpcBehaviorLibrary& behaviors,
                   const NativeActionLibrary& library,
                   PerceptionSettings settings, std::uint32_t identity,
                   const BehaviorObservationSource& observations,
                   const NpcActionQueries& queries, NpcActionReceiver& receiver,
                   const std::shared_ptr<memory::Domain>& domain)
    : impl_(std::allocate_shared<Impl>(
          memory::Allocator<Impl>(domain), clock, schedule, perception_schedule,
          random, behaviors, library, settings, identity, observations, queries,
          receiver, domain)) {}
NpcBrain::~NpcBrain() { impl_->runtime.stop(); }
void NpcBrain::start(std::string_view name, std::size_t entry) {
  DispatchGuard guard(impl_->busy);
  const auto selected = impl_->behaviors.resolve(name);
  impl_->original = selected;
  impl_->select({selected, entry});
}
void NpcBrain::replace(std::string_view name, std::size_t entry) {
  DispatchGuard guard(impl_->busy);
  if (!impl_->original) {
    throw std::logic_error("NPC brain has not been started");
  }
  const auto selected = name.starts_with("retn")
                            ? *impl_->original
                            : impl_->behaviors.resolve(name);
  impl_->select({selected, entry});
}
std::optional<PerceptionTransition> NpcBrain::advance(
    const world::WorldFrame& frame, const PerceptionInput& input,
    const PerceptionSight& sight) {
  DispatchGuard guard(impl_->busy);
  if (!frame.belongs_to(impl_->clock) || frame.sequence() <= impl_->sequence) {
    throw std::invalid_argument("Invalid NPC brain frame");
  }
  impl_->sequence = frame.sequence();
  constexpr std::size_t kRecognitionRise = 41, kRecognitionFall = 42;
  const auto result = impl_->perception.advance(frame, input, sight);
  const auto known = [&] {
    return impl_->perception.state().knowledge == TargetKnowledge::known ||
           impl_->secondary_perception.state().knowledge ==
               TargetKnowledge::known;
  };
  // Original primary transition dispatch precedes the secondary scan. Its
  // priority floor therefore observes the still-retained secondary knowledge.
  if (result) {
    const auto entry =
        result->after > result->before ? kRecognitionRise : kRecognitionFall;
    (void)impl_->runtime.request_event(entry, known());
  }
  const auto secondary =
      impl_->secondary_perception.advance(frame, input, sight);
  if (secondary) {
    (void)impl_->runtime.request_event(secondary->after > secondary->before
                                           ? kRecognitionRise
                                           : kRecognitionFall,
                                       known());
  }
  impl_->runtime.advance(frame);
  return result;
}
void NpcBrain::stop() {
  DispatchGuard guard(impl_->busy);
  impl_->runtime.stop();
  impl_->selected.reset();
}
void NpcBrain::receive_target(std::optional<std::uint64_t> replacement,
                              TargetKnowledge knowledge,
                              std::span<const PerceptionActor> actors) {
  DispatchGuard guard(impl_->busy);
  impl_->perception.receive_target(replacement, knowledge, actors);
}
PerceptionState NpcBrain::perception() const noexcept {
  return impl_->perception.state();
}
PerceptionState NpcBrain::secondary_perception() const noexcept {
  return impl_->secondary_perception.state();
}
bool NpcBrain::request_event(std::size_t entry) {
  DispatchGuard guard(impl_->busy);
  return impl_->runtime.request_event(
      entry, impl_->perception.state().knowledge == TargetKnowledge::known ||
                 impl_->secondary_perception.state().knowledge ==
                     TargetKnowledge::known);
}
std::string_view NpcBrain::behavior() const {
  return impl_->selected ? impl_->behaviors.name(*impl_->selected)
                         : std::string_view{};
}
std::string_view NpcBrain::return_behavior() const {
  return impl_->original ? impl_->behaviors.name(*impl_->original)
                         : std::string_view{};
}
std::optional<std::size_t> NpcBrain::current_node() const noexcept {
  return impl_->runtime.current_node();
}
}  // namespace bw::game
