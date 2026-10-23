// Native startup window, input and media output behind one platform-neutral
// header; see docs/native-startup-host.md. Construct/use/destroy on the main
// thread. All calls are single-owner, not thread-safe. The private native
// owner synchronously retires GPU/audio work before destruction. Errors throw;
// pump returns close requests and freshly pressed compatibility action bits.
// Algorithm: pump native events and sample controller -> shared game update ->
// video Output operations. Input edges are consumed once per pump. Native
// callbacks cannot call game objects. Costs scale with events/pixels/samples;
// bounded audio buffering and one retained video texture. Design/implementation
// MCP: cpp-guidelines R.1 (#rr-raii) owns paired platform handles;
// cpp-performance COPY.5 (guidelines/copy-move/
// COPY.5-resource-owning-types-are-move-only.md) fixes callback-owner identity.
#pragma once
#include <cstdint>
#include <memory>

#include "bw/video/output.hpp"

namespace bw::platform {
struct StartupInput {
  std::uint32_t pressed_actions{};
  bool close_requested{};
};
class StartupHost final : public video::Output {
 public:
  StartupHost();
  ~StartupHost() override;
  StartupHost(const StartupHost&) = delete;
  StartupHost& operator=(const StartupHost&) = delete;
  StartupHost(StartupHost&&) = delete;
  StartupHost& operator=(StartupHost&&) = delete;
  StartupInput pump(bool window_close_enabled);
  void upload(const video::VideoFrame& frame) override;
  void clear_video() noexcept override;
  bool draw(float opacity) override;
  video::AudioOutput& audio() noexcept override;

 private:
  struct State;
  std::unique_ptr<State> state_;
};
}  // namespace bw::platform
