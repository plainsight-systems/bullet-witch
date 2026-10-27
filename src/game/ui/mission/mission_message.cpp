// Resolve the retained original text tree once per message and reuse the shared
// acknowledgment state machine. Page turns retain the open panel and original
// 0.2-second input gate; final acknowledgment closes it. Core SL.con.3
// (#rsl-bounds) validates every child range; performance MEM.9 (guidelines/
// memory/MEM.9-embedded-allocate-at-init.md) keeps updates allocation-free.
#include "mission_message.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <stdexcept>
namespace bw::game {
namespace {
constexpr std::uint32_t kConfirm = 0x1010;
constexpr float kOpenGate = .3F, kPageGate = .2F;
std::string_view english(const formats::TextTable& table, std::size_t page) {
  constexpr unsigned kEnglish = 200;
  const auto children = table.node(2, page).children;
  for (std::size_t i = 0; i < children.count; ++i) {
    const auto value = table.node(3, children.first + i);
    if (value.identifier == kEnglish) {
      return value.text;
    }
  }
  throw std::runtime_error("Original mission instruction English text missing");
}
}  // namespace
MissionMessage::MissionMessage(const formats::TextTable& table,
                               std::string_view name_space,
                               MissionMessageOutput& output,
                               audio::CueOutput& audio)
    : table_(table), namespace_(name_space), output_(output), audio_(audio) {}
MissionMessage::~MissionMessage() { cancel(); }
void MissionMessage::cancel() noexcept {
  if (pending_) {
    output_.hide();
  }
  pending_ = false;
  pages_ = {};
}
std::string_view MissionMessage::page(std::size_t index) const {
  if (index >= pages_.count) {
    throw std::out_of_range("Mission page index");
  }
  return english(table_, pages_.first + index);
}
void MissionMessage::start(std::uint32_t index) {
  if (pending_) {
    throw std::logic_error("Mission instruction is already active");
  }
  constexpr std::size_t kDigits = 10, kMinimumDigits = 4;
  std::array<char, kDigits> digits{};
  const auto converted =
      std::to_chars(digits.data(), std::to_address(digits.end()), index);
  if (converted.ec != std::errc{}) {
    throw std::length_error("Mission message index");
  }
  const auto count = static_cast<std::size_t>(converted.ptr - digits.data());
  std::array<char, kDigits> padded{};
  const auto size = std::max(count, kMinimumDigits);
  std::fill_n(padded.begin(), size - count, '0');
  std::copy_n(digits.begin(), count,
              std::span(padded).subspan(size - count).begin());
  const std::string_view key(padded.data(), size);
  formats::TextLink candidate;
  for (std::size_t group = 0; group < table_.records(0); ++group) {
    const auto root = table_.node(0, group);
    if (root.text != namespace_) {
      continue;
    }
    for (std::size_t row = 0; row < root.children.count; ++row) {
      const auto entry = table_.node(1, root.children.first + row);
      if (entry.text == key) {
        candidate = entry.children;
      }
    }
  }
  if (candidate.count == 0) {
    throw std::runtime_error("Original mission instruction missing");
  }
  // Validate every page before publishing any modal state.
  for (std::size_t i = 0; i < candidate.count; ++i) {
    (void)english(table_, candidate.first + i);
  }
  pages_ = candidate;
  page_index_ = 0;
  gate_ = kOpenGate;
  dialog_.start(true, MenuDialogMode::acknowledgment);
  pending_ = true;
  audio_.submit(dialog_.take_sounds());
}
void MissionMessage::update(MenuInput input, float seconds) {
  if (!std::isfinite(seconds) || seconds < 0) {
    throw std::invalid_argument("Invalid mission dialog interval");
  }
  audio_.advance(seconds);
  if (!pending_) {
    return;
  }
  gate_ = std::max(0.F, gate_ - seconds);
  if (gate_ > 0) {
    input.pressed &= ~kConfirm;
  }
  if (!dialog_.presentation().closing && (input.pressed & kConfirm) != 0 &&
      page_index_ + 1 < pages_.count) {
    ++page_index_;
    gate_ = kPageGate;
    input.pressed &= ~kConfirm;
    MenuSoundRequests sounds;
    sounds.add(MenuSound::dialog_page);
    audio_.submit(sounds);
  }
  dialog_.update(input, seconds);
  audio_.submit(dialog_.take_sounds());
  if (dialog_.presentation().complete) {
    output_.hide();
    pending_ = false;
  }
}
void MissionMessage::present() {
  if (pending_) {
    output_.show(page(page_index_), dialog_.presentation());
  }
}
}  // namespace bw::game
