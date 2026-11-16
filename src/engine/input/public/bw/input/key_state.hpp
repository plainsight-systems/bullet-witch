#ifndef BW_INPUT_KEY_STATE_HPP
#define BW_INPUT_KEY_STATE_HPP

#include "bw/input/keys.hpp"
#include "bw/platform/input/device_input.hpp"

#include <array>
#include <cstdint>

// Responsibility: one value per key per frame, made from the samples the
// devices reported since the frame before (docs/architecture/input.md,
// invariants 1 and 2). The host hands every device sample to accumulate,
// calls begin_frame once before the world's tick, and the player's input
// (bw/input/player_input.hpp) reads the frame's values inside the tick.
//
// Invariants, each a unit test row.
//   1. accumulate(sample) of an axis element adds the sample to its key's
//      sum for the coming frame; of a button element it records the
//      button's last value and counts one press (value not 0) or one
//      release.
//   2. begin_frame() ends the accumulation and makes the frame's values.
//      An axis key with a sample since the last begin_frame reads the sum
//      of its samples. With none, MouseX and MouseY read 0 and every other
//      axis key keeps the value it read the frame before.
//   3. A MouseWheel sample above 0 counts one press and one release of
//      MouseScrollUp, below 0 of MouseScrollDown, and a sample of 0
//      nothing.
//   4. A button key is down after begin_frame when it had more presses
//      than releases, up when more releases than presses, and as it was
//      the frame before when they are equal. Its value's x reads 1 when
//      it is down or was pressed at all in the frame, else 0: a press and
//      a release inside one frame read as 1 for that frame and 0 the
//      next.
//   5. An Axis2D key reads its pair: x the value of its x key, y of its y
//      key (keys.hpp, pair_axes). An axis key is down when its value is
//      not 0.
//   6. was_down(key) is down(key) as of the begin_frame before the last;
//      presses(key) and releases(key) are the counts of the frame that
//      the last begin_frame ended.
//   7. A new KeyState reads 0 and up for every key.
//
// Performance guidelines read: MEM.9 and EMB.2 (fixed arrays over the key
// set; nothing allocates), CACHE.2 (a few hundred bytes read once a
// frame).
namespace bw::input {

struct KeyValue {
  float x{};
  float y{};
};

class KeyState {
public:
  void accumulate(const platform::InputSample& sample) noexcept;
  void begin_frame() noexcept;

  [[nodiscard]] KeyValue value(Key key) const noexcept;
  [[nodiscard]] bool down(Key key) const noexcept;
  [[nodiscard]] bool was_down(Key key) const noexcept;
  [[nodiscard]] std::uint32_t presses(Key key) const noexcept;
  [[nodiscard]] std::uint32_t releases(Key key) const noexcept;

private:
  struct Entry {
    float value{};
    float accumulator{};
    std::uint32_t samples{};
    std::uint32_t pending_presses{};
    std::uint32_t pending_releases{};
    std::uint32_t presses{};
    std::uint32_t releases{};
    bool down{};
    bool was_down{};
  };
  std::array<Entry, kKeyCount> entries_{};
};

} // namespace bw::input
#endif
