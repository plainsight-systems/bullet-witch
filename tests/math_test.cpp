// Turning witnesses retain original-machine-code numeric outputs and explicit
// policy boundary cases (both-phase I.1/I.7/MEM.9); no asset or emulator
// runtime. Verify shared numerical contracts in process: scalar
// precision/ordering, handedness, affine row convention, singular rejection and
// promoted point accumulation. Fixed independent values exercise behavior, not
// private layout. Both phases: Core I.9 (#ri-concepts) makes float/double
// explicit; performance MEM.9
// (guidelines/memory/MEM.9-embedded-allocate-at-init.md) uses fixed storage.
#include <cmath>
#include <limits>
#include <stdexcept>

#include "bw/math/direction.hpp"
#include "bw/math/matrix.hpp"
#include "bw/math/two_bone.hpp"

namespace {
void require(bool value, const char* message) {
  if (!value) throw std::runtime_error(message);
}
}  // namespace
void math_tests() {
  namespace math = bw::math;
  using Float = math::Vector3<float>;
  using Double = math::Vector3<double>;
  require(math::dot(Float{16777216, 1, -16777216}, Float{1, 1, 1}) == 0 &&
              math::dot(Double{16777216, 1, -16777216}, Double{1, 1, 1}) == 1,
          "Vector operations preserve scalar precision and addition order");
  require(std::signbit(math::dot(Float{-0.0F, -0.0F, -0.0F}, Float{1, 1, 1})) &&
              !std::signbit(math::accumulated_dot(Float{-0.0F, -0.0F, -0.0F},
                                                  Float{1, 1, 1})),
          "Zero-seeded accumulation remains an explicit distinct operation");
  require(math::cross(Float{1, 0, 0}, Float{0, 1, 0}) == Float{0, 0, 1} &&
              math::cross(Double{0, 1, 0}, Double{1, 0, 0}) == Double{0, 0, -1},
          "Cross product preserves handedness for both precisions");
  require(math::length(Double{0, 3, 4}) == 5 &&
              math::divide(Double{0, 3, 4}, 5.0) == Double{0, .6, .8},
          "Length and division do not choose a caller normalization policy");
  require(math::normalize_or(Double{0, 3, 4}, Double{7, 8, 9}) ==
                  Double{0, .6, .8} &&
              math::normalize_or(Double{0, 0, 0}, Double{7, 8, 9}) ==
                  Double{7, 8, 9} &&
              math::normalize_or(Double{0, 3, 4}, Double{7, 8, 9}, 5.0) ==
                  Double{7, 8, 9},
          "Normalization retains caller threshold and unmodified fallback");
  require(
      !math::all_finite(Float{0, std::numeric_limits<float>::infinity(), 0}),
      "Finite validation rejects nonfinite vector lanes");
  // Independent x86 execution witnesses: 24-degree step toward +Z. Both
  // antipodal signs and ordinary quadrants retain original branch behavior.
  constexpr math::DirectionTurnPolicy<float> turn_policy{
      {0, 1, 0}, .9990000128746033F, 1.0000001111620804e-6F};
  struct TurnWitness {
    Float current;
    Float expected;
  };
  const std::array turn_witnesses{
      TurnWitness{{-8.7422776573475858e-08F, 0.0F, -1.0F},
                  {-0.40673676133155823F, 0.0F, -0.91354537010192871F}},
      TurnWitness{{8.7422776573475858e-08F, 0.0F, -1.0F},
                  {-0.4067365825176239F, 0.0F, -0.91354548931121826F}},
      TurnWitness{{0.017452457919716835F, 0.0F, -0.99984771013259888F},
                  {-0.39073112607002258F, 0.0F, -0.9205048680305481F}},
      TurnWitness{{-0.017452457919716835F, 0.0F, -0.99984771013259888F},
                  {-0.42261835932731628F, 0.0F, -0.90630775690078735F}},
      TurnWitness{{0.05233604833483696F, 0.0F, -0.99862951040267944F},
                  {0.453990638256073F, 0.0F, -0.8910064697265625F}},
      TurnWitness{{-0.05233604833483696F, 0.0F, -0.99862951040267944F},
                  {-0.453990638256073F, 0.0F, -0.8910064697265625F}},
      TurnWitness{{1.0F, 0.0F, -4.3711388286737929e-08F},
                  {0.91354542970657349F, 0.0F, 0.40673664212226868F}},
      TurnWitness{{-1.0F, 0.0F, -4.3711388286737929e-08F},
                  {-0.91354542970657349F, 0.0F, 0.40673664212226868F}},
      TurnWitness{{0.1736481785774231F, 0.0F, 0.98480772972106934F},
                  {0.0F, 0.0F, 1.0F}},
      TurnWitness{{-0.1736481785774231F, 0.0F, 0.98480772972106934F},
                  {0.0F, 0.0F, 1.0F}},
      TurnWitness{{0.0F, 0.0F, 1.0F}, {0.0F, 0.0F, 1.0F}},
  };
  constexpr float turn_step = .41887906193733215F;
  for (const auto& witness : turn_witnesses) {
    const auto turned = math::turn_toward(witness.current, Float{0, 0, 1},
                                          turn_step, turn_policy);
    require(math::length(math::subtract(turned, witness.expected)) < .000001F,
            "Direction step matches original machine-code witness");
  }
  const float edge = -turn_policy.collinear_cosine;
  const auto toward_negative_x = [](float z) {
    return Float{-std::sqrt(1 - z * z), 0, z};
  };
  const auto at_edge = math::turn_toward(
      Float{0, 0, 1}, toward_negative_x(edge), turn_step, turn_policy);
  const auto outside = math::turn_toward(
      Float{0, 0, 1}, toward_negative_x(std::nextafter(edge, -1.0F)), turn_step,
      turn_policy);
  require(at_edge[0] < 0 && outside[0] > 0,
          "Inclusive collinear threshold uses cross axis; next float uses "
          "caller fallback");
  const math::DirectionTurnPolicy<double> alternate_turn{
      {0, -1, 0}, .999, .000001};
  require(math::turn_toward(Double{0, 0, -1}, Double{0, 0, 1}, .25,
                            alternate_turn)[0] > 0,
          "Antipodal fallback belongs to caller policy and preserves double "
          "precision");
  const auto matrix = math::affine_transform(
      math::TransformComponents<float>{{0, 0, 0, 2}, {10, 20, 30}, {2, 3, 4}});
  const auto point = math::transform_point<float>(Float{1, 2, 3}, matrix);
  require(point == Float{18, 44, 78},
          "Row-vector affine transform retains nonunit quaternion magnitude");
  require(math::transform_direction<float>(Float{1, 2, 3}, matrix) ==
              Float{8, 24, 48},
          "Direction transformation excludes translation");
  const auto inverse = math::inverse_affine(matrix);
  if (!inverse) {
    throw std::runtime_error("Nonsingular affine transform has inverse");
  }
  const auto restored = math::transform_point<float>(point, *inverse);
  require(math::length(math::subtract(restored, Float{1, 2, 3})) < .000001F,
          "Affine inverse restores a transformed point");
  require(!math::inverse_affine(math::Affine3<float>{}),
          "Singular matrix has no inverse");
  const auto homogeneous = math::homogeneous(matrix);
  const auto homogeneous_inverse = math::inverse_affine(homogeneous);
  require(math::affine_part(homogeneous) == matrix && homogeneous_inverse &&
              math::affine_part(*homogeneous_inverse) == *inverse,
          "Affine storage conversions retain identical inverse arithmetic");
  auto perspective = homogeneous;
  perspective[1][3] = .5F;
  require(!math::inverse_affine(perspective),
          "Affine inverse rejects a perspective matrix");
  auto moved = homogeneous;
  math::set_translation(moved, Float{9, 8, 7});
  require(math::translation(moved) == Float{9, 8, 7} &&
              moved[0] == homogeneous[0] && moved[3][3] == 1,
          "Translation access preserves basis and homogeneous lanes");
  const math::Affine3<float> promoted{
      {{1, 0, 0}, {1, 1, 0}, {1, 0, 1}, {0, 0, 0}}};
  const Float cancellation{16777216, 1, -16777216};
  require(math::transform_point<float>(cancellation, promoted)[0] == 0 &&
              math::transform_point<double>(cancellation, promoted)[0] == 1,
          "Point transformation preserves explicit accumulator precision");
  const auto first = math::homogeneous_transform(
      math::TransformComponents<float>{{0, 0, 0, 1}, {1, 2, 3}, {2, 2, 2}});
  const auto second = math::homogeneous_transform(
      math::TransformComponents<float>{{0, 0, 0, 1}, {4, 5, 6}, {1, 1, 1}});
  const auto combined = math::multiply(first, second);
  require(
      math::transform_point<float>(Float{1, 1, 1}, combined) == Float{7, 9, 11},
      "Matrix product preserves ordered row-vector composition");
  // Independently known triangles exercise the reach contract; rotation deltas
  // must carry the old segment directions into the solved segments as well.
  const auto solve = [](const math::TwoBoneRequest<double>& input) {
    const auto result = math::solve_two_bone(input);
    require(result.has_value(), "Finite valid two-bone input is solvable");
    require(
        std::abs(math::length(math::subtract(result->joint, input.root)) -
                 input.upper_length) < 1e-10 &&
            std::abs(math::length(math::subtract(result->end, result->joint)) -
                     input.lower_length) < 1e-10,
        "Two-bone solution preserves both lengths including unreachable goals");
    const auto upper_matrix =
        math::homogeneous_transform(math::TransformComponents<double>{
            result->upper_rotation, {}, {1, 1, 1}});
    const auto lower_matrix =
        math::homogeneous_transform(math::TransformComponents<double>{
            result->lower_rotation, {}, {1, 1, 1}});
    const auto old_upper = math::subtract(input.joint, input.root);
    const auto old_lower = math::subtract(input.end, input.joint);
    const auto upper_direction = math::transform_direction<double>(
        math::divide(old_upper, math::length(old_upper)), upper_matrix);
    const auto lower_direction = math::transform_direction<double>(
        math::divide(old_lower, math::length(old_lower)), lower_matrix);
    require(math::length(math::subtract(
                upper_direction,
                math::divide(math::subtract(result->joint, input.root),
                             input.upper_length))) < 1e-10 &&
                math::length(math::subtract(
                    lower_direction,
                    math::divide(math::subtract(result->end, result->joint),
                                 input.lower_length))) < 1e-10,
            "Two-bone rotations preserve row-vector handedness and target "
            "directions");
    return *result;
  };
  math::TwoBoneRequest<double> limb{{0, 0, 0}, {1, 0, 0}, {2, 0, 0}, {1, 1, 0},
                                    {0, 1, 0}, 1,         1};
  const auto bent = solve(limb);
  require(!bent.reach_clamped &&
              math::length(math::subtract(bent.end, limb.target)) < 1e-12 &&
              math::length(math::subtract(bent.joint, Double{0, 1, 0})) < 1e-12,
          "A right triangle follows the authored pole side");
  limb.pole = {1, -1, 0};
  require(
      math::length(math::subtract(solve(limb).joint, Double{1, 0, 0})) < 1e-12,
      "Mirroring the pole selects the other elbow half-plane");
  limb.target = {4, 0, 0};
  require(solve(limb).reach_clamped && solve(limb).end == Double{2, 0, 0},
          "Beyond-reach target extends without stretching");
  limb.target = {0, 0, 0};
  const auto folded = solve(limb);
  require(!folded.reach_clamped && folded.end == Double{},
          "Equal-length chain can fold to a coincident root target");
  limb.upper_length = 2;
  const auto inner = solve(limb);
  require(inner.reach_clamped && inner.end == Double{1, 0, 0},
          "Unequal chain clamps a too-close endpoint to its inner reachable "
          "radius");
  limb.upper_length = 1;
  limb.lower_length = 2;
  require(solve(limb).joint == Double{-1, 0, 0},
          "Longer lower segment folds the upper segment in reverse");
  limb.target = {1, 0, 0};
  limb.pole = {8, 0, 0};
  limb.lower_length = 1;
  const auto collinear = solve(limb);
  require(math::all_finite(collinear.upper_rotation) &&
              math::all_finite(collinear.lower_rotation),
          "Collinear pole and straight current chain retain finite "
          "deterministic rotations");
  limb.target = {-2, 0, 0};
  (void)solve(limb);
  limb.joint = limb.root;
  require(!math::solve_two_bone(limb),
          "A zero current segment is rejected atomically");
  limb.joint = {1, 0, 0};
  limb.upper_length = 0;
  require(!math::solve_two_bone(limb),
          "A nonpositive authored length is rejected");
  limb.upper_length = 1;
  limb.target[0] = std::numeric_limits<double>::infinity();
  require(!math::solve_two_bone(limb),
          "Nonfinite target cannot mutate the pose");
  const auto float_solution = math::solve_two_bone(math::TwoBoneRequest<float>{
      {3, 4, 5}, {4, 4, 5}, {5, 4, 5}, {4, 5, 5}, {3, 5, 5}, 1, 1});
  require(float_solution && math::length(math::subtract(
                                float_solution->joint, Float{3, 5, 5})) < 1e-5F,
          "Float solver retains translated geometry without hidden origin "
          "assumptions");
}
