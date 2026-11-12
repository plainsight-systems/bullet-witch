#include "bw/rendering/scene/frame_views.hpp"

#include "bw/math/matrix/matrix4.hpp"
#include "bw/platform/math/math.hpp"

#include <algorithm>
#include <array>
#include <cstdint>

// Responsibility: the out-of-line part of bw/rendering/scene/frame_views.hpp.
//
// Readings of the header that the code encodes:
//   A point p transforms as p * M (matrix4.hpp, row vectors), so clip
//   component j is p dotted with column j of world_to_clip; each plane is a
//   sum or difference of columns, normalized by the length of its (a, b, c).
//   add checks the purpose before the room: a Main view is SecondMainView
//   whether or not the list is full.
namespace bw::scene {

namespace {

std::array<float, 4> column(const math::Matrix4f& m, std::size_t j) noexcept {
  return {m.m[0][j], m.m[1][j], m.m[2][j], m.m[3][j]};
}

Plane normalized(const std::array<float, 4>& p) noexcept {
  const float length = platform::math::square_root((p[0] * p[0]) + (p[1] * p[1]) + (p[2] * p[2]));
  if (!(length > 0.0F)) {
    return Plane{p[0], p[1], p[2], p[3]};
  }
  return Plane{p[0] / length, p[1] / length, p[2] / length, p[3] / length};
}

std::array<float, 4> combine(const std::array<float, 4>& a, const std::array<float, 4>& b,
                             float sign) noexcept {
  return {a[0] + (sign * b[0]), a[1] + (sign * b[1]), a[2] + (sign * b[2]), a[3] + (sign * b[3])};
}

} // namespace

std::array<Plane, kFrustumPlanes> frustum_planes(const math::Matrix4f& world_to_clip) noexcept {
  const std::array<float, 4> x = column(world_to_clip, 0);
  const std::array<float, 4> y = column(world_to_clip, 1);
  const std::array<float, 4> z = column(world_to_clip, 2);
  const std::array<float, 4> w = column(world_to_clip, 3);
  return {normalized(combine(w, x, 1.0F)),
          normalized(combine(w, x, -1.0F)),
          normalized(combine(w, y, 1.0F)),
          normalized(combine(w, y, -1.0F)),
          normalized(z),
          normalized(combine(w, z, -1.0F))};
}

SceneView make_scene_view(const SceneViewDescription& description) noexcept {
  SceneView view{};
  view.world_to_clip = description.world_to_clip;
  view.planes = frustum_planes(description.world_to_clip);
  view.eye = description.eye;
  view.viewport_width = description.viewport_width;
  view.viewport_height = description.viewport_height;
  view.projection_scale =
      std::max(description.view_to_clip.m[0][0], description.view_to_clip.m[1][1]) / 2.0F;
  view.detail_origins = {description.eye, description.eye};
  view.detail_transition = 0.0F;
  view.purpose = description.purpose;
  return view;
}

// A camera cut or a first frame makes the frame before unusable. The newer
// detail sample rolls into the older whenever the lag has passed, and equal
// times give 0 (frame_views.hpp contract 5), since the clock is the
// caller's and does not decrease. The lag is the constant
// kDetailLagSeconds, and the reset gives both samples the eye, which
// chooses the same level.
void FrameViews::begin_frame(const SceneView& main, bool camera_cut, double seconds) noexcept {
  const SceneView before = views_[kMainView];
  const bool usable = had_frame_ && !camera_cut && before.viewport_width == main.viewport_width &&
                      before.viewport_height == main.viewport_height;
  has_previous_ = usable;
  if (usable) {
    previous_main_ = before;
  }
  SceneView stored = main;
  stored.jitter = shaders::Float2{};
  if (!had_frame_ || camera_cut) {
    stored.detail_origins = {main.eye, main.eye};
    detail_times_ = {seconds, seconds};
    stored.detail_transition = 0.0F;
  } else {
    stored.detail_origins = before.detail_origins;
    if (detail_times_[1] < seconds - kDetailLagSeconds) {
      stored.detail_origins[0] = stored.detail_origins[1];
      detail_times_[0] = detail_times_[1];
      stored.detail_origins[1] = main.eye;
      detail_times_[1] = seconds;
    }
    const double span = detail_times_[1] - detail_times_[0];
    stored.detail_transition =
        span == 0.0 ? 0.0F
                    : static_cast<float>(std::clamp(
                          (seconds - kDetailLagSeconds - detail_times_[0]) / span, 0.0, 1.0));
  }
  views_[kMainView] = stored;
  count_ = 1;
  had_frame_ = true;
}

// Every view is appended to one list and named by its index.
ViewError FrameViews::add(const SceneView& view, std::uint32_t& index) noexcept {
  if (view.purpose == ViewPurpose::Main) {
    return ViewError::SecondMainView;
  }
  if (count_ == kMaxViews) {
    return ViewError::ViewListFull;
  }
  SceneView stored = view;
  stored.detail_origins = views_[kMainView].detail_origins;
  stored.detail_transition = views_[kMainView].detail_transition;
  views_[count_] = stored;
  index = count_;
  ++count_;
  return ViewError::None;
}

} // namespace bw::scene
