// Prepare shape keys/deltas once, then sample and deform retained instance
// arrays. Original source arithmetic, named binding and ownership are in
// shape_pose.hpp/facial-animation.md. Core SL.con.3 (#rsl-bounds) validates
// ranges before conversion; Performance CDSA.32 and MEM.9 (guidelines/cpu-dsa/
// CDSA.32-transform-static-data-into-the-consumer-layout.md, guidelines/memory/
// MEM.9-embedded-allocate-at-init.md) keep updates typed/allocation-free.
// Both implementation corpora consulted. Shared math owns half/vector
// arithmetic.
#include "bw/animation/shape_pose.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>

#include "../formats/reader.hpp"
#include "bw/formats/model_shapes.hpp"
#include "bw/math/half.hpp"
#include "bw/math/scalar.hpp"
namespace bw::animation {
namespace {
constexpr std::size_t kFloatBytes = 4, kPositionBytes = 12, kHalfBytes = 2,
                      kHalf4Bytes = 8;
float scalar(const resources::detail::Reader& reader, std::uint64_t offset) {
  const auto value = std::bit_cast<float>(reader.u32(offset));
  if (!std::isfinite(value)) {
    resources::detail::malformed("Nonfinite shape value");
  }
  return value;
}
}  // namespace
PreparedModelShapes::PreparedModelShapes(
    const formats::ModelDefinition& model,
    const std::shared_ptr<memory::Domain>& domain)
    : buffers_(memory::Allocator<Buffer>(domain)),
      names_(memory::Allocator<std::string_view>(domain)) {
  if (model.shape_buffers().size() != model.shape_offsets().size()) {
    resources::detail::malformed("Shape position/offset publications differ");
  }
  buffers_.reserve(model.shape_buffers().size());
  for (std::size_t index = 0; index < model.shape_buffers().size(); ++index) {
    const auto offsets = formats::ModelShapeOffsets::parse(
        model.shape_offset_bytes(index), domain);
    const auto rest = model.shape_bytes(index);
    if (rest.size() !=
        static_cast<std::uint64_t>(offsets.vertex_count()) * kPositionBytes) {
      resources::detail::malformed(
          "Shape rest position count differs from offsets");
    }
    Buffer buffer(domain);
    for (const auto name : offsets.names()) {
      const auto found = std::ranges::find(names_, name);
      const auto target = static_cast<std::size_t>(found - names_.begin());
      if (std::ranges::find(buffer.channels, target) != buffer.channels.end()) {
        resources::detail::malformed("Duplicate shape channel name");
      }
      if (found == names_.end()) names_.push_back(name);
      buffer.channels.push_back(target);
    }
    buffer.rest.reserve(offsets.vertex_count());
    buffer.deltas.reserve(offsets.deltas().size() / kHalf4Bytes);
    const resources::detail::Reader reader{rest};
    for (std::size_t vertex = 0; vertex < offsets.vertex_count(); ++vertex) {
      const auto position = vertex * kPositionBytes;
      buffer.rest.push_back({scalar(reader, position),
                             scalar(reader, position + kFloatBytes),
                             scalar(reader, position + 2 * kFloatBytes)});
      for (std::size_t channel = 0; channel < buffer.channels.size();
           ++channel) {
        math::Vector3<float> delta{};
        for (std::size_t component = 0; component < delta.size(); ++component) {
          const auto offset =
              (vertex * buffer.channels.size() + channel) * kHalf4Bytes +
              component * kHalfBytes;
          const auto pair = offsets.deltas().subspan(offset, kHalfBytes);
          constexpr unsigned kByteShift = 8;
          const auto bits = static_cast<std::uint16_t>(
              static_cast<std::uint16_t>(pair[0]) |
              (static_cast<std::uint16_t>(pair[1]) << kByteShift));
          delta.at(component) = math::finite_half_to_float(bits);
        }
        buffer.deltas.push_back(delta);
      }
    }
    buffers_.push_back(std::move(buffer));
  }
}
ShapePose::ShapePose(const PreparedModelShapes& prepared,
                     const std::shared_ptr<memory::Domain>& domain)
    : prepared_(&prepared),
      positions_(memory::Allocator<math::Vector3<float>>(domain)),
      pending_(memory::Allocator<math::Vector3<float>>(domain)),
      offsets_(memory::Allocator<std::size_t>(domain)) {
  std::size_t total = 0;
  for (const auto& buffer : prepared.buffers()) {
    offsets_.push_back(total);
    total += buffer.rest.size();
  }
  offsets_.push_back(total);
  positions_.reserve(total);
  pending_.resize(total);
  for (const auto& buffer : prepared.buffers()) {
    positions_.insert(positions_.end(), buffer.rest.begin(), buffer.rest.end());
  }
}
void ShapePose::evaluate(std::span<const float> weights) {
  if (weights.size() != prepared_->names().size() ||
      !std::ranges::all_of(weights,
                           [](float value) { return std::isfinite(value); })) {
    throw std::invalid_argument("Invalid model shape weights");
  }
  // Every output is staged before any buffer becomes visible to the renderer.
  std::size_t output = 0;
  for (const auto& buffer : prepared_->buffers()) {
    for (std::size_t vertex = 0; vertex < buffer.rest.size(); ++vertex) {
      auto position = buffer.rest.at(vertex);
      for (std::size_t channel = 0; channel < buffer.channels.size();
           ++channel) {
        position = math::add(
            position,
            math::scale(
                buffer.deltas.at(vertex * buffer.channels.size() + channel),
                weights[buffer.channels.at(channel)]));
      }
      if (!math::all_finite(position)) {
        throw std::invalid_argument("Nonfinite deformed shape position");
      }
      pending_.at(output++) = position;
    }
  }
  positions_.swap(pending_);
}
std::span<const math::Vector3<float>> ShapePose::positions(
    std::size_t buffer) const {
  if (buffer >= prepared_->buffers().size()) {
    throw std::out_of_range("Unknown shape buffer");
  }
  return std::span{positions_}.subspan(
      offsets_.at(buffer), offsets_.at(buffer + 1) - offsets_.at(buffer));
}
PreparedShapeClip::PreparedShapeClip(
    const formats::ShapeAnimationDefinition& definition,
    const PreparedModelShapes& model,
    const std::shared_ptr<memory::Domain>& domain)
    : channels_(memory::Allocator<Channel>(domain)),
      times_(memory::Allocator<float>(domain)),
      values_(memory::Allocator<float>(domain)),
      output_count_(model.names().size()) {
  const resources::detail::Reader header{definition.header()};
  constexpr std::uint64_t kDurationOffset = 8, kRateOffset = 12,
                          kRepeatOffset = 16;
  duration_ = scalar(header, kDurationOffset);
  rate_ = scalar(header, kRateOffset);
  repeating_ = header.u32(kRepeatOffset) != 0;
  if (duration_ <= 0 || rate_ <= 0) {
    resources::detail::malformed("Invalid shape clip clock");
  }
  channels_.reserve(definition.channels().size());
  for (const auto& source : definition.channels()) {
    const auto found = std::ranges::find(model.names(), source.name);
    if (found == model.names().end()) continue;
    if (!source.channel.has_value()) continue;
    const auto& channel = *source.channel;
    if (channel.key_count == 0 || channel.value_stride != kFloatBytes ||
        source.end_frame < 0) {
      resources::detail::malformed("Invalid scalar shape channel");
    }
    const auto first = times_.size();
    const resources::detail::Reader times{channel.times},
        values{channel.values};
    float previous = -std::numeric_limits<float>::infinity();
    for (std::size_t key = 0; key < channel.key_count; ++key) {
      const auto time = scalar(times, key * kFloatBytes);
      if (time <= previous) {
        resources::detail::malformed("Shape key times must increase");
      }
      times_.push_back(time);
      values_.push_back(scalar(values, key * kFloatBytes));
      previous = time;
    }
    channels_.push_back(
        {static_cast<std::size_t>(found - model.names().begin()), first,
         channel.key_count, source.end_frame});
  }
}
void PreparedShapeClip::accumulate(float frame, float weight,
                                   std::span<float> output) const {
  if (!std::isfinite(frame) || frame < 0 || !std::isfinite(weight) ||
      output.size() != output_count_) {
    throw std::invalid_argument("Invalid shape sample request");
  }
  for (const auto& channel : channels_) {
    const auto sample_frame =
        channel.end < duration_ ? std::min(frame, channel.end) : frame;
    const auto times = std::span{times_}.subspan(channel.first, channel.count);
    const auto upper = std::ranges::upper_bound(times, sample_frame);
    const auto end = static_cast<std::size_t>(upper - times.begin());
    const auto lower = end == 0 ? 0 : end - 1,
               next = std::min(end, channel.count - 1);
    const auto alpha = lower == next
                           ? 0.0F
                           : std::clamp((sample_frame - times[lower]) /
                                            (times[next] - times[lower]),
                                        0.0F, 1.0F);
    const auto value =
        math::linear_interpolate(values_.at(channel.first + lower),
                                 values_.at(channel.first + next), alpha);
    output[channel.target] += value * weight;
  }
}
}  // namespace bw::animation
