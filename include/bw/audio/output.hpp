// Game audio cue/device boundary. Original banks retain cue variation, RPC,
// category and loop semantics; game action IDs must resolve before this API.
// One control thread owns all methods. Loading may register or extend a bank,
// then seal() publishes a new immutable sample lookup. Existing callbacks keep
// the preceding lookup until publication; all versions and bank/PCM storage
// survive until native callbacks have joined. New plays require seal().
// Original streaming flags remain intact; a prepared cue subset may retain its
// encoded bank in memory while only reachable waves own decoded samples.
// Unprepared cues are rejected before native scheduling. Destructor stops/joins
// device work before releasing its domain and original bank bytes. Only one
// FACT runtime is admitted because its allocator/decoder callbacks have
// process-wide scope. Configured capacities and sample budget are explicit.
// Generational handles prevent a stale stop from affecting a reused cue slot.
// Vendor cue allocation is domain-accounted; authored sample callback work is
// lookup/copy only. E.6 (#re-raii) joins ownership; MEM.2 (guidelines/memory/
// MEM.2-pool-allocator-for-fixed-size-objects.md) bounds cue slots and rejects
// exhaustion. Both design corpora consulted. See
// animation-audio-presentation.md.
// PauseDomain makes the original cue-admission distinction explicit: world
// pause freezes gameplay cues, while later menu/presentation cues keep playing.
// One session service pumps update() even when the world is paused. I.1
// (#ri-explicit), MEM.9 (guidelines/memory/MEM.9-embedded-allocate-at-init.md)
// apply to the typed per-slot policy; see native-pause/menu-audio.md.
#pragma once
#include <optional>
#include <span>
#include <stdexcept>

#include "bw/audio/music.hpp"
#include "bw/math/vector.hpp"
#include "bw/memory/allocator.hpp"

namespace bw::audio {
struct CueHandle {
  std::uint32_t slot{}, generation{};
  friend bool operator==(CueHandle, CueHandle) = default;
};
enum class PauseDomain : std::uint8_t { world, presentation };
// Original request ownership selects the mix group independently of an authored
// sound category. A sound named BGM may still be emitted as a game effect.
enum class MixBus : std::uint8_t { effects, music, voice };
struct MixGains {
  float music{1}, effects{1}, voice{1};
};
struct CueRequest {
  std::uint16_t bank{}, cue{};
  std::optional<math::Vector3<float>> position;
  PauseDomain pause_domain{PauseDomain::world};
  MixBus mix_bus{MixBus::effects};
};
struct Listener {
  math::Vector3<float> position{}, forward{0, 0, 1}, up{0, 1, 0};
};
struct Limits {
  std::size_t banks{}, cues{}, decoded_samples{};
};
class CueOutput {
 public:
  CueOutput() = default;
  virtual ~CueOutput() = default;
  CueOutput(const CueOutput&) = delete;
  CueOutput& operator=(const CueOutput&) = delete;
  CueOutput(CueOutput&&) = delete;
  CueOutput& operator=(CueOutput&&) = delete;
  // Empty admission selects all cues; otherwise copy the exact cue set and
  // prepare every referenced track variation before publishing. R.20 retains
  // bank/sample ownership; MEM.9 keeps decoding outside native callbacks.
  void register_bank(std::uint16_t slot,
                     std::span<const std::uint8_t> wrapped_sound,
                     std::span<const std::uint8_t> wave_bank) {
    register_bank(slot, wrapped_sound, wave_bank, {});
  }
  virtual void register_bank(std::uint16_t slot,
                             std::span<const std::uint8_t> wrapped_sound,
                             std::span<const std::uint8_t> wave_bank,
                             std::span<const std::uint16_t> admitted_cues) = 0;
  // Extend a retained bank at a loading boundary; empty selects all cues.
  // A failed extension preserves previous admission and samples. This does not
  // replace native banks or duplicate their encoded bytes. seal() publishes.
  virtual void extend_bank(std::uint16_t, std::span<const std::uint16_t>) {
    throw std::logic_error("Audio bank extension is unsupported");
  }
  virtual void seal() = 0;
  // Narrow music capability on this same device; unsupported backends fail
  // explicitly rather than silently dropping requested frontend music.
  virtual MusicOutput& music() {
    throw std::logic_error("Prepared music output is unsupported");
  }
  virtual std::optional<CueHandle> play(const CueRequest& request) = 0;
  virtual bool set_position(CueHandle handle,
                            const math::Vector3<float>& position) = 0;
  virtual void set_listener(const Listener& listener) = 0;
  virtual bool stop(CueHandle handle) = 0;
  // Finite linear gains [0,1], validated together before mutation. Affects
  // active and future cues without replacing authored category/RPC volume.
  virtual void set_mix_gains(const MixGains& gains) = 0;
  // Affects active and newly requested world cues. Presentation cues are
  // independent; this is not a device-wide suspension or category mute.
  virtual void pause(bool paused) = 0;
  // Mission teardown stops every world cue and clears world suspension without
  // stopping presentation music/cues or discarding prepared session banks.
  virtual void retire_world() {
    throw std::logic_error("World audio retirement is unsupported");
  }
  virtual void update() = 0;
  virtual bool active(CueHandle handle) const = 0;
};
}  // namespace bw::audio
