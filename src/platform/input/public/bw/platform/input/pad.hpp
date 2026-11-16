#ifndef BW_PLATFORM_INPUT_PAD_HPP
#define BW_PLATFORM_INPUT_PAD_HPP

#include "bw/platform/input/device_input.hpp"

#include <cstddef>
#include <span>

// Responsibility: the platform-neutral contract of one game pad
// (docs/architecture/input.md, Device: "a keyboard, a mouse or a pad,
// behind a platform adapter"). The host polls it once a frame, before the
// world's tick, and hands what it reports to key state. The macOS adapter
// is selected by bw/platform/os/macos/pad.hpp.
//
// Contract.
//   1. Threads: a pad is created, polled and destroyed on the process main
//      thread, where the host's loop runs.
//   2. poll(out) is called once a frame. It writes the samples of every
//      pad element (device_input.hpp) whose value differs from the value
//      it reported last, each element at most once, and returns how many
//      it wrote. An element it has never reported starts at 0. `out` holds
//      at least kPadElementCount samples, so nothing is dropped.
//   3. The pad is the first controller the OS reports that has a full
//      gamepad layout (two sticks, two triggers, four face buttons, two
//      shoulders); another connected after it is ignored until the first
//      disconnects. connected() says whether there is one.
//   4. When the pad disconnects, the next poll reports every element that
//      was not at 0 as 0, so nothing stays held.
//   5. Values are the OS's: a stick axis in -1 to 1 after the OS's own
//      dead zone, right and up positive; a trigger in 0 to 1; a button 1
//      or 0. The adapter applies no dead zone, scale or clamp of its own.
//   6. While the application is not the frontmost one the OS forwards no
//      pad input, and poll reports no change.
//
// Core guidelines read: C.121 and C.122 (an abstract class at the platform
// boundary), I.11 and R.1 (created into a unique_ptr by the OS header),
// F.24 (the output is a span). Performance guidelines read: MEM.9 (the
// adapter allocates at creation, not per poll), EMB.2 (a fixed bound on
// what one poll writes).
namespace bw::platform {

// The pad's elements of device_input.hpp: four stick axes, two triggers,
// four face buttons, two shoulders and two stick clicks.
inline constexpr std::size_t kPadElementCount = 14;

class Pad {
public:
  Pad(const Pad&) = delete;
  Pad& operator=(const Pad&) = delete;
  virtual ~Pad() = default;

  // Contract 2. Precondition: out.size() >= kPadElementCount.
  [[nodiscard]] virtual std::size_t poll(std::span<InputSample> out) = 0;
  // Contract 3.
  [[nodiscard]] virtual bool connected() const noexcept = 0;

protected:
  Pad() = default;
};

} // namespace bw::platform
#endif
