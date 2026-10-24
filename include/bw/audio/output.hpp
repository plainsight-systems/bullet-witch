// Game audio cue/device boundary. Original banks retain cue variation, RPC,
// category and loop semantics; game action IDs must resolve before this API.
// One control thread owns all methods. Preparation precedes seal(); sealed
// immutable bank/PCM storage outlives every native callback. Original streaming
// flags remain intact; a prepared cue subset may retain its encoded bank in
// memory while only reachable waves own decoded samples. Unprepared cues are
// rejected before native scheduling. Destructor stops/joins device work before
// releasing its domain and original bank bytes. Only one FACT runtime is
// admitted because its allocator/decoder callbacks have process-wide scope.
// Configured capacities and sample budget are explicit. Generational handles
// prevent a stale stop from affecting a reused cue slot. Vendor cue allocation
// is domain-accounted; authored sample callback work is lookup/copy only.
// E.6 (#re-raii) joins ownership; MEM.2 (guidelines/memory/
// MEM.2-pool-allocator-for-fixed-size-objects.md) bounds cue slots and rejects
// exhaustion. Both design corpora consulted. See
// animation-audio-presentation.md.
#pragma once
#include <optional>
#include <span>

#include "bw/math/vector.hpp"
#include "bw/memory/allocator.hpp"

namespace bw::audio {
struct CueHandle {
  std::uint32_t slot{}, generation{};
  friend bool operator==(CueHandle, CueHandle) = default;
};
struct CueRequest {
  std::uint16_t bank{}, cue{};
  std::optional<math::Vector3<float>> position;
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
  virtual void seal() = 0;
  virtual std::optional<CueHandle> play(const CueRequest& request) = 0;
  virtual bool set_position(CueHandle handle,
                            const math::Vector3<float>& position) = 0;
  virtual void set_listener(const Listener& listener) = 0;
  virtual bool stop(CueHandle handle) = 0;
  virtual void pause(bool paused) = 0;
  virtual void update() = 0;
  virtual bool active(CueHandle handle) const = 0;
};
}  // namespace bw::audio
