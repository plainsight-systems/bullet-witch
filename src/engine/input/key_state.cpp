// One value per key per frame (bw/input/key_state.hpp).
#include "bw/input/key_state.hpp"

#include "bw/input/keys.hpp"
#include "bw/platform/input/device_input.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>

namespace bw::input {

void KeyState::accumulate(const platform::InputSample& sample) noexcept {
  if (sample.element == platform::InputElement::MouseWheel) {
    if (sample.value == 0.0F) {
      return;
    }
    Entry& wheel = entries_[static_cast<std::size_t>(sample.value > 0.0F ? Key::MouseScrollUp
                                                                         : Key::MouseScrollDown)];
    ++wheel.pending_presses;
    ++wheel.pending_releases;
    return;
  }
  const std::optional<Key> key = key_for_element(sample.element);
  if (!key.has_value()) {
    return;
  }
  Entry& entry = entries_[static_cast<std::size_t>(*key)];
  ++entry.samples;
  if (platform::element_is_axis(sample.element)) {
    entry.accumulator += sample.value;
    return;
  }
  entry.accumulator = sample.value;
  if (sample.value != 0.0F) {
    ++entry.pending_presses;
  } else {
    ++entry.pending_releases;
  }
}

void KeyState::begin_frame() noexcept {
  for (std::size_t index = 0; index < kKeyCount; ++index) {
    Entry& entry = entries_[index];
    const Key key = static_cast<Key>(index);
    entry.was_down = entry.down;
    switch (key_kind(key)) {
    case KeyKind::Axis1D:
      if (entry.samples > 0U) {
        entry.value = entry.accumulator;
      } else if (reads_zero_without_samples(key)) {
        entry.value = 0.0F;
      }
      entry.down = entry.value != 0.0F;
      entry.presses = 0U;
      entry.releases = 0U;
      break;
    case KeyKind::Button:
      entry.presses = entry.pending_presses;
      entry.releases = entry.pending_releases;
      if (entry.presses > entry.releases) {
        entry.down = true;
      } else if (entry.releases > entry.presses) {
        entry.down = false;
      }
      entry.value = entry.down || entry.presses > 0U ? 1.0F : 0.0F;
      break;
    case KeyKind::Axis2D:
      // Read from its pair (invariant 5); it keeps nothing of its own.
      break;
    }
    entry.accumulator = 0.0F;
    entry.samples = 0U;
    entry.pending_presses = 0U;
    entry.pending_releases = 0U;
  }
}

KeyValue KeyState::value(Key key) const noexcept {
  const std::optional<KeyPair> pair = pair_axes(key);
  if (pair.has_value()) {
    return KeyValue{entries_[static_cast<std::size_t>(pair->x)].value,
                    entries_[static_cast<std::size_t>(pair->y)].value};
  }
  return KeyValue{entries_[static_cast<std::size_t>(key)].value, 0.0F};
}

bool KeyState::down(Key key) const noexcept {
  const std::optional<KeyPair> pair = pair_axes(key);
  if (pair.has_value()) {
    return entries_[static_cast<std::size_t>(pair->x)].down ||
           entries_[static_cast<std::size_t>(pair->y)].down;
  }
  return entries_[static_cast<std::size_t>(key)].down;
}

bool KeyState::was_down(Key key) const noexcept {
  const std::optional<KeyPair> pair = pair_axes(key);
  if (pair.has_value()) {
    return entries_[static_cast<std::size_t>(pair->x)].was_down ||
           entries_[static_cast<std::size_t>(pair->y)].was_down;
  }
  return entries_[static_cast<std::size_t>(key)].was_down;
}

std::uint32_t KeyState::presses(Key key) const noexcept {
  return entries_[static_cast<std::size_t>(key)].presses;
}

std::uint32_t KeyState::releases(Key key) const noexcept {
  return entries_[static_cast<std::size_t>(key)].releases;
}

} // namespace bw::input
