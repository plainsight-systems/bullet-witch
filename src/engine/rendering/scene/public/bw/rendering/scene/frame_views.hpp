#ifndef BW_RENDERING_SCENE_FRAME_VIEWS_HPP
#define BW_RENDERING_SCENE_FRAME_VIEWS_HPP

#include "bw/math/matrix/matrix4.hpp"
#include "bw/math/vector/vector3.hpp"
#include "bw/rendering/scene/instance.hpp"
#include "bw/rendering/shaders/parameter_block.hpp"

#include <array>
#include <cstdint>
#include <span>

// Responsibility: the views of one frame (docs/architecture/scene.md,
// "Views", invariants 5 and 11): every frustum the culling dispatch tests,
// each with a purpose, registered before the dispatch runs; the main view
// first, and the main view of the frame before kept beside the list for
// phase one. A view here is what culling and the CPU order read; the block
// a pass's shaders read is view.hpp's.
// Every view, main and shadow, is appended to one list and named by its
// index. There is one camera, and a shadow view is one frustum the lighting
// system registers. A camera cut or a first frame makes the frame before
// unusable; no other reset is applied, because phase two makes a frame
// correct whatever history phase one tested, so only the publisher's cut
// flag is read.
//
// Contract.
//   1. make_scene_view(description) fills a SceneView: world_to_clip as
//      given; planes = frustum_planes(world_to_clip); the eye, the
//      viewport size in pixels and the purpose as given; projection_scale
//      = max(view_to_clip[0][0], view_to_clip[1][1]) / 2; jitter zero; both
//      detail origins the eye and detail_transition 0.
//   2. frustum_planes(m) is the six planes of the clip volume of m (x and
//      y within -w and w, depth within 0 and w; matrix4.hpp), in the order
//      left, right, bottom, top, near, far, each (a, b, c, d) with (a, b,
//      c) unit length and pointing into the volume: a point p is inside
//      when a p.x + b p.y + c p.z + d >= 0 for all six.
//   3. A FrameViews holds at most kMaxViews views. begin_frame(main, cut,
//      seconds)
//      starts a frame: the list becomes the one view `main` at index
//      kMainView, and the previous main view becomes the main view of the
//      frame before, usable when there was a frame before, `cut` is false
//      and the two viewport sizes are equal; otherwise there is none.
//      The `cut` flag is the publisher's camera cut flag.
//      add(view, index) appends a view and gives its index; a full list is
//      ViewListFull and appends nothing; a view whose purpose is Main is
//      SecondMainView and appends nothing.
//   4. The main view's jitter is zero: nothing in the tree consumes one.
//   5. Detail origins. begin_frame(main, cut, seconds) keeps two samples of
//      the main view's eye, each with the time it was taken, and writes
//      them into the main view it stores: detail_origins[0] the older,
//      [1] the newer. With no frame before, or `cut` true, both samples
//      become (eye, seconds) and detail_transition is 0. Otherwise, when
//      the newer sample is older than seconds - kDetailLagSeconds, the
//      newer sample becomes the older and (eye, seconds) the newer; then
//      detail_transition = clamp((seconds - kDetailLagSeconds - older
//      time) / (newer time - older time), 0, 1), and 0 when the two times
//      are equal. `seconds` is the caller's clock and never read from the
//      system; it does not decrease between frames. Every other view takes
//      the main view's origins and transition when it is added.
//
// Core guidelines read: I.1, F.20, Con.5, Enum.3. Performance guidelines
// read: EMB.2 (a fixed list, no allocation per frame), CACHE.2.
namespace bw::scene {

inline constexpr std::uint32_t kMaxViews = 32;
inline constexpr std::uint32_t kMainView = 0;
inline constexpr std::uint32_t kFrustumPlanes = 6;
inline constexpr float kDetailLagSeconds = 0.5F;

enum class ViewPurpose : std::uint8_t { Main, SunCascade, AtlasSlot };

struct Plane {
  float a{};
  float b{};
  float c{};
  float d{};
};

[[nodiscard]] std::array<Plane, kFrustumPlanes>
frustum_planes(const math::Matrix4f& world_to_clip) noexcept;

struct SceneViewDescription {
  math::Matrix4f world_to_clip{};
  // The projection alone, for the projected size of a sphere.
  math::Matrix4f view_to_clip{};
  math::Vector3f eye{math::zero_init};
  float viewport_width{};
  float viewport_height{};
  ViewPurpose purpose{ViewPurpose::Main};
};

struct SceneView {
  math::Matrix4f world_to_clip{};
  std::array<Plane, kFrustumPlanes> planes{};
  math::Vector3f eye{math::zero_init};
  float viewport_width{};
  float viewport_height{};
  float projection_scale{};
  shaders::Float2 jitter{};
  // The two origins a detail level is chosen from, older then newer, and
  // the fade between the levels they choose (contract 5).
  std::array<math::Vector3f, 2> detail_origins{math::Vector3f{math::zero_init},
                                               math::Vector3f{math::zero_init}};
  float detail_transition{};
  ViewPurpose purpose{ViewPurpose::Main};
};

[[nodiscard]] SceneView make_scene_view(const SceneViewDescription& description) noexcept;

enum class ViewError : std::uint8_t {
  None,
  ViewListFull,
  SecondMainView,
};

class FrameViews {
public:
  void begin_frame(const SceneView& main, bool camera_cut, double seconds) noexcept;
  [[nodiscard]] ViewError add(const SceneView& view, std::uint32_t& index) noexcept;

  [[nodiscard]] std::span<const SceneView> views() const noexcept {
    return {views_.data(), count_};
  }
  // Precondition: begin_frame was called.
  [[nodiscard]] const SceneView& main() const noexcept { return views_[kMainView]; }
  // Null when the frame before gives no usable main view (contract 3).
  [[nodiscard]] const SceneView* previous_main() const noexcept {
    return has_previous_ ? &previous_main_ : nullptr;
  }

private:
  std::array<SceneView, kMaxViews> views_{};
  SceneView previous_main_{};
  std::uint32_t count_{};
  std::array<double, 2> detail_times_{};
  bool has_previous_{};
  bool had_frame_{};
};

} // namespace bw::scene

#endif
