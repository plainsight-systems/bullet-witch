// Receiver-to-render publication over scene-local lighting. The stable owner
// owns prepared fields/emitters and copied reflection handles, borrowing only
// a RenderScene lighting capability and immutable reflection field. Receivers
// borrow this owner and must die first; GPU assets outlive scene retirement.
// Algorithm: prepare source fields -> publish view -> prepare receiver/sample
// -> add its draw destinations -> seal -> sample/publish at completed views.
// The game supplies policy, bounds, transforms and authored channel values;
// this engine boundary owns packet packing, retained strict sampling and draw
// distribution. It holds no actor, camera controller or asset-file knowledge.
// Candidate receiver preparation can throw without touching native draws.
// After native appearance replacement, noexcept receiver move adopts prepared
// storage. Moved-from/sealed preparation calls reject. Publication is serial,
// requires active RenderScene membership and rejects stale destinations there.
// Construction/add-draw preparation allocates only the declared capacity;
// seal/publish allocate nothing. Cost is one original sample per receiver plus
// O(receiver draws) fixed packet copies. No second authoritative draw registry.
// Design Core F.7 (#rf-smart), C.32 (#rc-dtor-ptr) document restricted borrows;
// performance COPY.4 (guidelines/copy-move/COPY.4-rule-of-five-rule-of-zero.md)
// fixes owner identity and noexcept candidate transfer; CDSA.32
// (guidelines/cpu-dsa/CDSA.32-transform-static-data-into-the-consumer-layout.md)
// prepares destination storage once. Source/reference and verification:
// research/engine/subsystems/lighting-output-integration.md. Implementation
// E.6 (#re-raii) and MEM.9
// (guidelines/memory/MEM.9-embedded-allocate-at-init.md) preserve candidate
// rollback and preparation-only allocation.
#pragma once
#include "bw/render/scene/render_scene.hpp"
#include "scene_lighting.hpp"

namespace bw::scene {
class ReceiverPublication;
using ReflectionBindings =
    std::array<render::WorldTextureHandle, kReflectionSlotCount>;
class LightingPublication {
 public:
  LightingPublication(const SceneLightingSource& source,
                      const ReflectionBindings& reflections,
                      render::LightingPublisher& output,
                      const std::shared_ptr<memory::Domain>& domain);
  ~LightingPublication() = default;
  LightingPublication(const LightingPublication&) = delete;
  LightingPublication& operator=(const LightingPublication&) = delete;
  LightingPublication(LightingPublication&&) = delete;
  LightingPublication& operator=(LightingPublication&&) = delete;
  // Authored definitions remain game policy; source selection stays here.
  void set_parameters(const SceneLightingParameters& parameters);
  void set_field_intensity(LightingFieldId field, float intensity);
  void set_view(const LocalLightViewQuery& view);
  void replace_local_lights(std::span<const LocalLight> lights);
  // Retire preparation-time emitters without invalidating dynamic handles.
  // Owner-thread, allocation-free; the next view refreshes selected packets.
  void clear_static_emitters();
  void prepare_emitters(std::size_t capacity);
  LocalLightEmitter acquire_emitter();
  DirectionalLighting directional_lighting() const noexcept;
  // Setup-only one-off packet, e.g. immutable white sky. Returned storage owns
  // its values; no references escape sampler scratch.
  render::WorldLightingUpdate evaluate(const LightingReceiver& receiver);
  ReceiverPublication prepare_receiver(const LightingReceiver& receiver,
                                       std::size_t draw_capacity);
  // Placed receivers keep the source strict-zone cache keyed by root position
  // and live intensity. They never substitute the actor transformed center.
  ReceiverPublication prepare_placed(LightingFieldId field,
                                     const math::Vector3<float>& root,
                                     float radius, std::size_t draw_capacity);

 private:
  friend class ReceiverPublication;
  render::WorldLightingUpdate packet(const ReceiverLighting& value) const;
  SceneLighting scene_;
  ReflectionBindings reflections_;
  render::LightingPublisher& output_;
  std::shared_ptr<memory::Domain> domain_;
};
class ReceiverPublication {
 public:
  ~ReceiverPublication() = default;
  ReceiverPublication(const ReceiverPublication&) = delete;
  ReceiverPublication& operator=(const ReceiverPublication&) = delete;
  ReceiverPublication(ReceiverPublication&& other) noexcept;
  ReceiverPublication& operator=(ReceiverPublication&&) = delete;
  // Preparing only: applies the sampled packet to an initial draw and copies
  // only its model/draw/instance identity into reserved publication storage.
  void add_draw(render::WorldColorDraw& draw);
  void seal();
  // Bound receiver only: evaluate using its prepared observation. Dynamic
  // model publication replaces only the final model transform. Neither grants
  // mutation of another receiver, fields, emitters or scene membership.
  void publish();
  void publish_model(const math::Affine3<float>& transform);

 private:
  friend class LightingPublication;
  ReceiverPublication(LightingPublication& owner,
                      const LightingReceiver& receiver,
                      std::optional<math::Vector3<float>> placed_root,
                      std::size_t draw_capacity);
  void require_bound() const;
  render::WorldLightingUpdate sample(const LightingReceiver& receiver);
  void publish_packet(const render::WorldLightingUpdate& packet);
  LightingPublication* owner_;
  LightingReceiver receiver_;
  std::optional<math::Vector3<float>> placed_root_;
  std::optional<RetainedLightingReceiver> retained_;
  render::WorldLightingUpdate initial_;
  memory::Vector<render::WorldLightingUpdate> updates_;
  std::size_t capacity_;
  bool bound_{};
};
}  // namespace bw::scene
