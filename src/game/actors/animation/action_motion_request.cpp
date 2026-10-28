// Preserve the existing source action adapter's conversion in one shared
// implementation. Playback and parameter ownership remain actor decisions.
// Both-phase I.11/CDSA.32 retain prepared borrows and fixed per-request work.
#include "action_motion_request.hpp"

#include <cmath>
#include <stdexcept>

namespace bw::game {
std::optional<CharacterMotionRequest> resolve_action_motion(
    const CharacterAnimations& catalog, std::optional<std::size_t> clip,
    const ActionMotionRequest& request,
    const animation::ClipPlaybackState* current, bool motion_parameters) {
  constexpr std::uint32_t kMotionChannels = 32;
  if (!std::isfinite(request.rate) || request.rate < 0 ||
      !std::isfinite(request.blend_seconds) || request.blend_seconds < 0 ||
      !std::isfinite(request.start_seconds) || request.start_seconds < 0 ||
      !std::isfinite(request.start_frames) || request.start_frames < 0 ||
      request.channel >= kMotionChannels ||
      (request.start != ActionMotionStart::seconds &&
       request.start != ActionMotionStart::frames &&
       request.start != ActionMotionStart::preserve_phase)) {
    throw std::invalid_argument("Invalid source action timing/channel");
  }
  if (!clip) return std::nullopt;
  const auto& prepared = catalog.motions().clip(*clip);
  auto start = request.start == ActionMotionStart::frames
                   ? request.start_frames / prepared.frames_per_second()
                   : request.start_seconds;
  if (request.start == ActionMotionStart::preserve_phase) {
    start = current != nullptr && current->clip->duration_seconds() > 0
                ? current->seconds / current->clip->duration_seconds() *
                      prepared.duration_seconds()
                : 0;
  }
  return CharacterMotionRequest{
      .clip = *clip,
      .binding = request.slot,
      .parameter =
          motion_parameters ? catalog.parameter_for_clip(*clip) : std::nullopt,
      .channel = request.channel,
      .start_seconds = start,
      .blend_seconds = request.blend_seconds,
      .rate = request.rate,
      .fresh = request.fresh,
      .preserve_phase = request.start == ActionMotionStart::preserve_phase};
}
}  // namespace bw::game
