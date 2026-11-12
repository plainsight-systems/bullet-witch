// Unit test: public contracts of bw/rendering/scene/frame_views.hpp. Pure
// values; no device.
#include "bw/rendering/scene/frame_views.hpp"

#include "bw/math/matrix/matrix4.hpp"
#include "bw/math/vector/vector3.hpp"
#include "bw/testing/harness.hpp"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <numbers>

namespace {

namespace s = bw::scene;
using bw::math::Matrix4f;
using bw::math::Vector3f;
using bw::testing::require;

float side(const s::Plane& p, const Vector3f& v) {
  return (p.a * v.x) + (p.b * v.y) + (p.c * v.z) + p.d;
}

s::SceneView view_at(const Vector3f& eye, float width) {
  s::SceneViewDescription d{};
  d.view_to_clip = Matrix4f::perspective(std::numbers::pi_v<float> / 2.0F, 1.0F, 1.0F, 100.0F);
  d.world_to_clip =
      Matrix4f::look_at(eye, eye - Vector3f{0.0F, 0.0F, 1.0F}, Vector3f{0.0F, 1.0F, 0.0F}) *
      d.view_to_clip;
  d.eye = eye;
  d.viewport_width = width;
  d.viewport_height = 100.0F;
  return s::make_scene_view(d);
}

// (frame_views.hpp, contracts 2, 3 and 5): each plane, in its order, is the
// one a point just outside that face alone is outside of, with a unit
// normal; the previous main view is the frame before's unless the cut flag
// or a size change drops it; the detail samples roll after the lag and the
// transition follows; a second main view and a full list are refused.
void planes_and_previous() {
  const Vector3f origin{0.0F, 0.0F, 0.0F};
  const s::SceneView view = view_at(origin, 100.0F);
  const std::array<Vector3f, s::kFrustumPlanes> outside{
      Vector3f{-20.0F, 0.0F, -10.0F}, Vector3f{20.0F, 0.0F, -10.0F}, Vector3f{0.0F, -20.0F, -10.0F},
      Vector3f{0.0F, 20.0F, -10.0F},  Vector3f{0.0F, 0.0F, -0.5F},   Vector3f{0.0F, 0.0F, -200.0F}};
  for (std::size_t i = 0; i < s::kFrustumPlanes; ++i) {
    const s::Plane& p = view.planes[i];
    require(std::fabs((p.a * p.a) + (p.b * p.b) + (p.c * p.c) - 1.0F) < 1.0e-5F, i, "unit normal");
    require(side(p, Vector3f{0.0F, 0.0F, -10.0F}) > 0.0F, i, "the centre is inside");
    for (std::size_t j = 0; j < s::kFrustumPlanes; ++j) {
      require((side(view.planes[j], outside[i]) < 0.0F) == (i == j), i,
              "outside this plane alone, in the stated order");
    }
  }

  s::FrameViews views;
  views.begin_frame(view_at(Vector3f{1.0F, 0.0F, 0.0F}, 100.0F), false, 0.0);
  require(views.previous_main() == nullptr, "no frame before");
  views.begin_frame(view_at(Vector3f{2.0F, 0.0F, 0.0F}, 100.0F), false, 0.3);
  require(views.previous_main() != nullptr && views.previous_main()->eye.x == 1.0F,
          "the frame before's main view is kept");
  views.begin_frame(view_at(Vector3f{3.0F, 0.0F, 0.0F}, 100.0F), true, 0.4);
  require(views.previous_main() == nullptr, "a cut drops it");
  views.begin_frame(view_at(Vector3f{4.0F, 0.0F, 0.0F}, 50.0F), false, 0.5);
  require(views.previous_main() == nullptr, "a size change drops it");

  // After the cut at 0.4 both samples are (eye 3, 0.4). Rows: time, eye x,
  // older and newer origin x, transition.
  struct Row {
    double seconds;
    float eye, older, newer, transition;
  };
  const std::array rows{Row{0.6, 5.0F, 3.0F, 3.0F, 0.0F}, Row{1.0, 6.0F, 3.0F, 6.0F, 1.0F / 6.0F},
                        Row{1.4, 7.0F, 3.0F, 6.0F, 5.0F / 6.0F},
                        Row{1.75, 8.0F, 6.0F, 8.0F, 1.0F / 3.0F}};
  for (std::size_t i = 0; i < rows.size(); ++i) {
    views.begin_frame(view_at(Vector3f{rows[i].eye, 0.0F, 0.0F}, 50.0F), false, rows[i].seconds);
    const s::SceneView& main = views.main();
    require(main.detail_origins[0].x == rows[i].older && main.detail_origins[1].x == rows[i].newer,
            i, "the two samples");
    require(std::fabs(main.detail_transition - rows[i].transition) < 1.0e-5F, i, "the transition");
  }

  s::SceneView shadow = view_at(origin, 50.0F);
  std::uint32_t index = 0;
  require(views.add(shadow, index) == s::ViewError::SecondMainView, "a second main view");
  shadow.purpose = s::ViewPurpose::SunCascade;
  for (std::uint32_t i = 1; i < s::kMaxViews; ++i) {
    require(views.add(shadow, index) == s::ViewError::None && index == i, i, "appended");
  }
  require(views.add(shadow, index) == s::ViewError::ViewListFull &&
              views.views().size() == s::kMaxViews,
          "a full list appends nothing");
}

using Scenario = bw::testing::Scenario<void>;
constexpr std::array kScenarios{Scenario{"planes_and_previous", &planes_and_previous}};

} // namespace

int main(int argc, char** argv) { return bw::testing::run_scenario(argc, argv, kScenarios); }
