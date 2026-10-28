// Shared actor animation requests. Source policy returns copied bounded plans;
// each actor resolves its own catalog/banks and owns playback. No allocation,
// clock, actor mutation or resource ownership crosses this value boundary.
// Core I.11 (#ri-raw) and performance CDSA.32 (transform-static-data-into-the-
// consumer-layout), read in both phases, separate prepared policy from owners.
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
namespace bw::game {
enum class ActionMotionBank : std::uint8_t {
  body,
  upper_body,
  additive,
  weapon
};
enum class ActionMotionStart : std::uint8_t { seconds, frames, preserve_phase };

inline constexpr double kSourceActionBlendSeconds = .3;
struct ActionMotionRequest {
  ActionMotionBank bank{ActionMotionBank::body};
  std::uint32_t slot{}, channel{};
  ActionMotionStart start{ActionMotionStart::seconds};
  double start_seconds{}, start_frames{};
  double blend_seconds{kSourceActionBlendSeconds};
  float rate{1};
  bool fresh{};
};

struct ActionPresentationAdmission {
  std::size_t admitted{}, unavailable{};
};

struct ActionMotionPlan {
  // An accepted shot submits upper, weapon and additive, the largest covered
  // receiver. Clear-upper represents source bank retirement before requests,
  // including raise admission and source ordinary-state exit.
  std::array<ActionMotionRequest, 3> requests{};
  std::size_t count{};
  bool clear_upper{};
  std::optional<std::uint32_t> weapon_rest_slot;
  std::span<const ActionMotionRequest> motions() const {
    return std::span(requests).first(count);
  }
};

}  // namespace bw::game
