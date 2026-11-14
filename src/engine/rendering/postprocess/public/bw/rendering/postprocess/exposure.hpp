#ifndef BW_RENDERING_POSTPROCESS_EXPOSURE_HPP
#define BW_RENDERING_POSTPROCESS_EXPOSURE_HPP

#include "bw/rendering/graph/builder.hpp"
#include "bw/rendering/graph/declaration.hpp"
#include "bw/rendering/postprocess/post_shaders.hpp"
#include "bw/rendering/rhi/command_list.hpp"
#include "bw/rendering/rhi/device.hpp"
#include "bw/rendering/rhi/format.hpp"
#include "bw/rendering/rhi/resources.hpp"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>
#include <string>

// Responsibility: exposure (docs/architecture/post-processing.md,
// invariants 1 and 2, "Exposure"): scene colour's format and unit, the
// camera relation between EV100 and the exposure factor, the meter (a
// histogram of the log of scene colour's channel mean), the adaptation over
// an injected time step, the two texels the factor lives in, and the two
// passes that meter a frame and write its adapted value.
//
// Contract.
//   1. Scene colour is one texture of kSceneColorFormat holding linear
//      Rec. 709 light in cd/m2 multiplied by the frame's factor f
//      (post_shaders.hpp, contract 1). Nothing in it is encoded or tone
//      mapped.
//   2. The camera. exposure_from_ev100(ev) is 1 / (kExposureCalibration *
//      2^ev): the factor that brings the luminance kExposureCalibration *
//      2^ev to 1. ev100_from_exposure is its inverse. EV100 names a factor
//      and changes no pixel.
//   3. The meter. A texel's metered value is the mean of its three
//      channels with f divided out, so a neutral colour meters as its
//      luminance. The metered image is scene colour reduced by two: sample
//      (x, y) is the mean of texels (2x, 2y) to (2x + 1, 2y + 1), each
//      coordinate clamped to the last column and row, for x below
//      ceil(width / 2) and y below ceil(height / 2). histogram_buckets(L)
//      places one sample: with p = saturate((log2(max(L, 2^kHistogramLogMin))
//      - kHistogramLogMin) / (kHistogramLogMax - kHistogramLogMin)) and
//      b = p * (kHistogramBuckets - 1), `first` is floor(b), `second` is
//      min(first + 1, kHistogramBuckets - 1), second_weight is frac(b) and
//      first_weight is 1 - frac(b), except that first_weight is 0 when
//      `first` is 0: a black sample meters nothing. The histogram of a
//      frame is the sum of every sample's two weights; its scale is not
//      part of the contract. bucket_log_luminance(i) is kHistogramLogMin +
//      i / (kHistogramBuckets - 1) * (kHistogramLogMax - kHistogramLogMin).
//   4. The window. windowed_average_luminance(h, min_ev100), with S the
//      sum of h: the first kMeterLowFraction * S of the mass, counted from
//      bucket 0, and everything past kMeterHighFraction * S are discarded;
//      the result is 2 to the mean of bucket_log_luminance over what
//      remains, weighted by the remaining mass of each bucket. When
//      nothing remains (S is 0) the result is
//      average_luminance_at_ev100(min_ev100) / kMiddleGrey.
//   5. Range, target and adaptation. The exposure follows the metered
//      average only inside a range the caller gives in EV100, the scene's
//      setting: average_luminance_at_ev100(ev) is kMiddleGrey *
//      kExposureCalibration * 2^ev, the average that exposure_from_ev100(ev)
//      brings to middle grey, and with Lo and Hi that function of
//      min_ev100 and max_ev100, target_exposure(L, c, min_ev100, max_ev100)
//      is kMiddleGrey * 2^c / clamp(L, Lo, Hi): the factor that brings the
//      average L to middle grey, raised c stops, held inside the range.
//      adapt_exposure(in), with T = clamp(in.average_luminance, Lo, Hi),
//      O = kMiddleGrey * 2^in.compensation_stops / in.previous_exposure,
//      d = log2(T) - log2(O) and the speed s = kAdaptationSpeedUp when
//      d > 0 (the scene got brighter) and kAdaptationSpeedDown otherwise,
//      in stops per second: when abs(d) > kAdaptationLinearDistance the
//      step is linear, log2(A) = log2(O) + sign(d) * min(abs(d),
//      in.delta_seconds * s); otherwise it is exponential, log2(A) =
//      log2(O) + d * (1 - 2^(-in.delta_seconds * s)) * M, M being
//      kAdaptationUpM or kAdaptationDownM by the same sign. With in.reset,
//      A is T. The result is kMiddleGrey * 2^in.compensation_stops /
//      clamp(A, Lo, Hi). So with reset the result is
//      target_exposure(in.average_luminance, in.compensation_stops,
//      in.min_ev100, in.max_ev100) whatever the previous exposure; with a
//      delta of 0 and no reset it is the previous exposure (clamped); and
//      a range whose ends are equal pins the exposure: the result is
//      2^in.compensation_stops * exposure_from_ev100(in.min_ev100)
//      whatever the frame holds, the time step or the reset. That is how
//      a scene with nothing for the meter to average, one subject on an
//      empty background, is exposed: by its camera, not by its subject's
//      albedo. Preconditions: previous_exposure > 0, delta_seconds >= 0,
//      min_ev100 <= max_ev100.
//   6. The texels. An Exposure owns two 1 by 1 textures of kExposureFormat,
//      each made holding kInitialExposure. Frame N reads one as its factor
//      f_N, in every pass that writes light and in the chain, and its
//      adaptation pass writes the other, e_N; frame N + 1 reads the texel
//      frame N wrote, so f_(N+1) = e_N. e_N is the value tone mapping
//      applies in frame N, as e_N / f_N, and the factor that pre-multiplies
//      frame N + 1. begin_exposure_frame imports both into the frame, marks
//      the written one as leaving it, and gives the frame's handles and the
//      buffer holding a PreExposure block that names the read texel's
//      index; the two blocks are made once with the texels, so a frame
//      makes none. bind_pre_exposure records set_static_block(
//      kPreExposureSlot) with that buffer.
//   7. The passes. declare_exposure(exposure, graph, frame, scene_color,
//      size, inputs) declares the compute work after which the written
//      texel holds adapt_exposure({f_N, windowed_average_luminance(h,
//      inputs.min_ev100), inputs.compensation_stops, inputs.delta_seconds,
//      inputs.reset, inputs.min_ev100, inputs.max_ev100}), h the histogram
//      of contract 3 over `scene_color` of `size`, whatever the size, as
//      the device computes it in 32-bit floats. It reads scene colour and the
//      read texel and writes neither; the number of passes is not part of
//      the contract. The histogram's sums are integers, so they do not
//      depend on the order the device's threads add in. The time step and
//      the reset are inputs: nothing here reads a clock, and a caller
//      passes reset on its first frame and on a camera cut.
//   8. A frame not executed. A frame whose graph the caller does not
//      execute is followed by frame_dropped(): the texels are not swapped
//      for it, so the next frame reads the factor the dropped frame read.
//      A pass reads the frame's scene colour through a view made while the
//      frame is recorded; when the device refuses that view the work
//      records nothing and the written texel keeps what it held;
//      record_failure() is then ViewFailed until the next
//      begin_exposure_frame, which returns it before anything else. A
//      caller reads record_failure() after the frame's graph is recorded
//      and does not execute a frame that has one (post_chain.hpp,
//      end_post_frame).
//   9. The ratio rule, for a pass that reuses light across frames: a buffer
//      that holds light past its frame is stored with the factor of the
//      frame that wrote it, and a later frame reusing it multiplies by its
//      own factor over the writer's. Nothing in this module keeps such a
//      buffer.
//   10. create_exposure reads the two kernels' artifacts from
//      `shader_directory` and makes the texels, their views and blocks and
//      the pipelines; a failure is returned by name with a detail text and
//      `out` is left empty.
//
// Notes. The factor is a texture, not a constant read back (invariant 1).
// The calibration is 1.2; it places the EV100 scale and the range's ends
// and changes no pixel inside the range. The half resolution image is taken
// inside the histogram kernel as a two by two mean. kInitialExposure is a
// sunlit scene's factor because at a factor of 1 the fixture's light would
// not fit a 16-bit float on a frame that is not reset. Resets beyond the
// first frame and a camera cut, such as a large camera turn or move between
// two frames, are the caller's to pass as `reset`; no caller has a moving
// game camera yet.
//
// Core guidelines read: I.1 (time step, reset and compensation are
// arguments), I.5 and I.7 (preconditions and results stated), E.27 (a
// named error with a detail), C.21, R.3 (the frame's buffer is observed),
// Con.5, ES.45. Performance guidelines read: GPU.1 (no readback), MEM.4
// (two texels swapped each frame), GDSA.2 (the histogram is an integer
// reduction, the same run to run), GPU.9 (the texels and blocks are made
// once).
namespace bw::postprocess {

inline constexpr rhi::Format kSceneColorFormat = rhi::Format::R16G16B16A16Float;
inline constexpr rhi::Format kExposureFormat = rhi::Format::R32Float;

inline constexpr float kMiddleGrey = 0.18F;
// 78 / (100 * 0.65): the saturation-based sensitivity constant over ISO 100
// and a lens that passes 0.65.
inline constexpr float kExposureCalibration = 1.2F;

[[nodiscard]] float exposure_from_ev100(float ev100) noexcept;
[[nodiscard]] float ev100_from_exposure(float exposure) noexcept;

// The meter's range, EV100 -10 to 20: log2 of luminance in cd/m2 from
// -10 + log2(kExposureCalibration) to 20 + log2(kExposureCalibration).
inline constexpr std::uint32_t kHistogramBuckets = 64;
inline constexpr float kHistogramLogMin = -9.7369656F;
inline constexpr float kHistogramLogMax = 20.2630344F;
inline constexpr float kMeterLowFraction = 0.10F;
inline constexpr float kMeterHighFraction = 0.90F;
// The widest range the exposure may follow the meter in, the whole of the
// meter's: the default of a scene that sets none (contract 5).
inline constexpr float kDefaultMinEv100 = -10.0F;
inline constexpr float kDefaultMaxEv100 = 20.0F;
[[nodiscard]] float average_luminance_at_ev100(float ev100) noexcept;

// Stops per second, and the distance in stops past which the step is
// linear. M makes the exponential step's slope meet the linear step's at
// that distance: (1 / 60) / ((1 - 2^(-speed / 60)) * (distance / speed)).
inline constexpr float kAdaptationSpeedUp = 3.0F;
inline constexpr float kAdaptationSpeedDown = 1.0F;
inline constexpr float kAdaptationLinearDistance = 1.5F;
inline constexpr float kAdaptationUpM = 0.97855963F;
inline constexpr float kAdaptationDownM = 0.96736295F;

// The factor both texels hold before any frame: exposure_from_ev100(15), a
// sunlit scene.
inline constexpr float kInitialEv100 = 15.0F;
inline constexpr float kInitialExposure = 2.5431315e-5F;

struct BucketSplit {
  std::uint32_t first{};
  std::uint32_t second{};
  float first_weight{};
  float second_weight{};
};
[[nodiscard]] BucketSplit histogram_buckets(float luminance) noexcept;
[[nodiscard]] float bucket_log_luminance(std::uint32_t bucket) noexcept;
[[nodiscard]] float windowed_average_luminance(std::span<const float, kHistogramBuckets> histogram,
                                               float min_ev100) noexcept;

[[nodiscard]] float target_exposure(float average_luminance, float compensation_stops,
                                    float min_ev100, float max_ev100) noexcept;

struct AdaptationInputs {
  float previous_exposure{kInitialExposure};
  float average_luminance{};
  float compensation_stops{};
  float delta_seconds{};
  bool reset{};
  float min_ev100{kDefaultMinEv100};
  float max_ev100{kDefaultMaxEv100};
};
[[nodiscard]] float adapt_exposure(const AdaptationInputs& inputs) noexcept;

enum class ExposureError : std::uint8_t {
  None,
  ArtifactFailed,
  PipelineFailed,
  TextureFailed,
  // A PreExposure block's buffer, or the histogram's.
  BufferFailed,
  ViewFailed,
};

struct ExposureFailure {
  ExposureError error{ExposureError::None};
  std::string detail{};
  [[nodiscard]] bool ok() const noexcept { return error == ExposureError::None; }
};

// What a pass that writes light takes of a frame: the texel it declares
// Sampled, and the buffer it binds at kPreExposureSlot. Observers; the
// Exposure keeps both alive through the frame's execution.
struct FramePreExposure {
  graph::TextureHandle factor{};
  rhi::Buffer* block{};
};

// The frame's two imports: the texel read as f and the texel written.
struct ExposureFrame {
  FramePreExposure pre_exposure{};
  graph::TextureHandle adapted{};
};

// What the adaptation of one frame takes beside scene colour.
struct ExposureInputs {
  float compensation_stops{};
  float delta_seconds{};
  bool reset{};
  // The range of contract 5; equal ends pin the exposure.
  float min_ev100{kDefaultMinEv100};
  float max_ev100{kDefaultMaxEv100};
};

class Exposure {
public:
  Exposure();
  Exposure(const Exposure&) = delete;
  Exposure& operator=(const Exposure&) = delete;
  Exposure(Exposure&&) noexcept;
  Exposure& operator=(Exposure&&) noexcept;
  ~Exposure();

  // Contract 8.
  void frame_dropped() noexcept;
  [[nodiscard]] ExposureFailure record_failure() const;

private:
  friend struct ExposureAccess;
  struct State;
  std::unique_ptr<State> state_;
};

[[nodiscard]] ExposureFailure
create_exposure(rhi::Device& device, const std::filesystem::path& shader_directory, Exposure& out);

// Contract 6. On a failure nothing is imported and `out` is unchanged.
// Precondition: `exposure` was made by a create_exposure that returned no
// failure.
[[nodiscard]] ExposureFailure begin_exposure_frame(Exposure& exposure, rhi::Device& device,
                                                   graph::FrameGraph& graph, ExposureFrame& out);

// Contract 7.
void declare_exposure(Exposure& exposure, graph::FrameGraph& graph, const ExposureFrame& frame,
                      graph::TextureHandle scene_color, rhi::Extent2 size,
                      const ExposureInputs& inputs);

// Precondition: `pre_exposure.block` holds a PreExposure block that names
// a 1 by 1 texel the pass declared Sampled: the frame's, from
// begin_exposure_frame, for a frame on the chain.
void bind_pre_exposure(rhi::CommandList& list, const FramePreExposure& pre_exposure);

} // namespace bw::postprocess

#endif
