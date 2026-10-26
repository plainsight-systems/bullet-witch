// FACT-backed native game audio. Shared cue contract is in bw/audio/output.hpp.
// The target selects this backend explicitly; it does not replace movie audio.
// Global settings and banks are copied into the injected domain. Borrowing ends
// at each preparation call. Streaming WMA admits absent or whole-wave loops;
// selected partial loop regions reject before native construction. Native
// library/device allocations are separately accounted; the C bridge uses the
// same owner for every FACT/FAudio object. Design E.6 (#re-raii), MEM.2/MEM.9
// as cited by the shared audio contracts.
// Retained slots carry the explicit pause domain; world pause never affects
// presentation cues. Session service owns pumping, not cue submission.
#pragma once
#include "bw/audio/output.hpp"

namespace bw::audio::fact {
class Output final : public CueOutput {
 public:
  Output(std::span<const std::uint8_t> global_settings, Limits limits,
         const std::shared_ptr<memory::Domain>& domain);
  ~Output() override;
  Output(const Output&) = delete;
  Output& operator=(const Output&) = delete;
  Output(Output&&) = delete;
  Output& operator=(Output&&) = delete;
  using CueOutput::register_bank;
  void register_bank(std::uint16_t slot,
                     std::span<const std::uint8_t> wrapped_sound,
                     std::span<const std::uint8_t> wave_bank,
                     std::span<const std::uint16_t> admitted_cues) override;
  void seal() override;
  std::optional<CueHandle> play(const CueRequest& request) override;
  bool set_position(CueHandle handle,
                    const math::Vector3<float>& position) override;
  void set_listener(const Listener& listener) override;
  bool stop(CueHandle handle) override;
  void set_mix_gains(const MixGains& gains) override;
  void pause(bool paused) override;
  void update() override;
  bool active(CueHandle handle) const override;

 private:
  struct State;
  std::shared_ptr<State> state_;
};
}  // namespace bw::audio::fact
