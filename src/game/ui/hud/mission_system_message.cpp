// Reconstruct 478560/4ac140 and text owner 464670: namespace selection,
// page attributes, serial page waits and linear opacity. Cancellation enters
// the original end-wait state; it does not acknowledge a modal or pause time.
// Implementation Core F.16 (#rf-in), performance MEM.9 (guidelines/memory/
// MEM.9-embedded-allocate-at-init.md) keep read-only borrows and fixed state.
#include "mission_system_message.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace bw::game {
namespace {
constexpr float kFadeRate = 3.F;
constexpr std::array<std::string_view, 6> kKeys{"0050", "0051", "0052",
                                                "0053", "0054", "0055"};
float attribute(const formats::TextTable& table, formats::TextLink properties,
                std::string_view name, float fallback) {
  for (std::size_t i = 0; i < properties.count; ++i) {
    const auto item = table.property(properties.first + i);
    if (item.name != name) continue;
    float value{};
    const auto result =
        std::from_chars(std::to_address(item.value.begin()),
                        std::to_address(item.value.end()), value);
    if (result.ec != std::errc{} ||
        result.ptr != std::to_address(item.value.end()) ||
        !std::isfinite(value)) {
      throw std::runtime_error("Invalid original system text timing");
    }
    return value;
  }
  return fallback;
}
}  // namespace
MissionSystemMessage::MissionSystemMessage(const formats::TextTable& table)
    : table_(table) {}
void MissionSystemMessage::start(std::uint32_t index) {
  const auto selected = index < kKeys.size() ? index : 0;
  formats::TextLink pages;
  for (std::size_t i = 0; i < table_.records(0); ++i) {
    const auto group = table_.node(0, i);
    if (group.text != "sys") continue;
    for (std::size_t row = 0; row < group.children.count; ++row) {
      const auto entry = table_.node(1, group.children.first + row);
      if (entry.text == kKeys.at(selected)) pages = entry.children;
    }
  }
  if (pages.count == 0) {
    throw std::runtime_error("Original mission system text missing");
  }
  pages_ = pages;
  page_ = 0;
  index_ = selected;
  elapsed_ = opacity_ = target_ = end_remaining_ = 0;
  phase_ = Phase::initial;
  (void)page_text();
}
std::string_view MissionSystemMessage::page_text() const {
  constexpr unsigned kEnglish = 200;
  const auto children = table_.node(2, pages_.first + page_).children;
  for (std::size_t i = 0; i < children.count; ++i) {
    const auto item = table_.node(3, children.first + i);
    if (item.identifier == kEnglish) return item.text;
  }
  throw std::runtime_error("Original system text English page missing");
}
void MissionSystemMessage::prepare_page() {
  constexpr unsigned kEnglish = 200;
  formats::TextLink properties;
  const auto languages = table_.node(2, pages_.first + page_).children;
  for (std::size_t i = 0; i < languages.count; ++i) {
    const auto language = table_.node(3, languages.first + i);
    if (language.identifier == kEnglish) properties = language.properties;
  }
  play_remaining_ = attribute(table_, properties, "playtime", 0);
  if (play_remaining_ == 0) {
    play_remaining_ = std::numeric_limits<float>::max();
  }
  start_time_ = attribute(table_, properties, "starttime", 0);
  begin_remaining_ = attribute(table_, properties, "beginwait", 0);
  end_remaining_ = attribute(table_, properties, "endwait", 0);
  (void)page_text();
}
void MissionSystemMessage::cancel() noexcept {
  if (phase_ != Phase::done) phase_ = Phase::end_wait;
}
void MissionSystemMessage::advance(float control_seconds) {
  if (!std::isfinite(control_seconds) || control_seconds < 0) {
    throw std::invalid_argument("Invalid system text control interval");
  }
  if (phase_ == Phase::done) return;
  // Only skipped/completed pages loop. Each ordinary state yields once, as
  // the source text receiver does before advancing its opacity controller.
  bool yielded = false;
  while (!yielded) {
    switch (phase_) {
      case Phase::initial:
        prepare_page();
        if (play_remaining_ < 0) {
          phase_ = ++page_ < pages_.count ? Phase::initial : Phase::done;
          break;
        }
        phase_ = Phase::start_wait;
        break;
      case Phase::start_wait:
        if (elapsed_ < start_time_) {
          yielded = true;
        } else {
          phase_ = Phase::begin_wait;
        }
        break;
      case Phase::begin_wait:
        if (begin_remaining_ > 0) {
          begin_remaining_ -= control_seconds;
          yielded = true;
        } else {
          target_ = 1;
          phase_ = Phase::playing;
        }
        break;
      case Phase::playing:
        if (play_remaining_ <= 1.F / kFadeRate) target_ = 0;
        if (play_remaining_ > 0) {
          play_remaining_ -= control_seconds;
          yielded = true;
        } else {
          target_ = opacity_ = 0;
          if (page_ + 1 < pages_.count) {
            ++page_;
            phase_ = Phase::initial;
          } else {
            phase_ = Phase::end_wait;
            play_remaining_ -= control_seconds;
            yielded = true;
          }
        }
        break;
      case Phase::end_wait:
        if (end_remaining_ < 0) phase_ = Phase::done;
        end_remaining_ -= control_seconds;
        yielded = true;
        break;
      case Phase::done:
        yielded = true;
        break;
    }
  }
  const auto movement = kFadeRate * control_seconds;
  opacity_ = target_ > opacity_ ? std::min(target_, opacity_ + movement)
                                : std::max(target_, opacity_ - movement);
  elapsed_ += control_seconds;
}
SystemMessageView MissionSystemMessage::view() const {
  const bool visible = phase_ == Phase::initial || phase_ == Phase::playing ||
                       phase_ == Phase::end_wait;
  return {visible, index_, opacity_,
          visible ? page_text() : std::string_view{}};
}
}  // namespace bw::game
