// Typed PHIT shape decoding applies both-phase I.1 (ri-explicit) to body/pose
// associations and MEM.9 (guidelines/memory/MEM.9-embedded-allocate-at-init.md)
// to load-only metadata. Convex float3 spans borrow the retained archive.
// Material animation refinement applies both-phase R.2 (#rr-use-ptr) to
// bounded borrowed labels/keys and MEM.9 (guidelines/memory/
// MEM.9-embedded-allocate-at-init.md) to setup-only metadata allocation.
// Background body associations apply both-phase C.32 (#rc-dtor-ptr) and
// MEM.9 (guidelines/memory/MEM.9-embedded-allocate-at-init.md) to load-owned
// mapping metadata; original body order and missing entries are preserved.
// Bounded load-time views implement stage_definitions.hpp. The original loaders
// patch 32-bit pointers in retained data; these constructors instead validate
// ranges and retain immutable spans. Later physics/sampling policy stays
// outside format decoding. Partial metadata unwinds through the explicit domain
// owner. Both MCP phases retrieved SL.con.3 (#rsl-bounds) and MEM.6
// (guidelines/memory/MEM.6-inject-allocators-behind-one-interface.md).
// Enabled local-light records copy finite authored parameters at load time.
// Both MCP phases I.1 (ri-explicit) and MEM.9 (guidelines/memory/
// MEM.9-embedded-allocate-at-init.md) keep runtime emission outside decoding.
#include "bw/content/formats/stage_definitions.hpp"

#include <bit>
#include <cmath>

#include "bw/content/formats/binary_reader.hpp"

namespace bw::formats {
namespace {
using resources::detail::malformed;
using resources::detail::Reader;
using resources::detail::unsupported;
constexpr std::uint64_t kWordBytes = 4;
constexpr std::uint64_t kCollisionHeaderBytes = 48;
constexpr std::uint64_t kPartHeaderBytes = 96;
constexpr std::uint64_t kPartCountOffset = 36;
constexpr std::uint64_t kPartTableOffset = 40;
constexpr std::uint64_t kPartTriangleCountOffset = 44;
constexpr std::uint64_t kPartTriangleOffset = 48;
constexpr std::uint64_t kPartFlagsOffset = 52;
constexpr std::uint64_t kPartNameOffset = 64;
constexpr std::uint64_t kPartNameBytes = 32;
constexpr std::uint64_t kVectorBytes = 12;
constexpr std::uint64_t kTriangleBytes = 8;
constexpr std::uint64_t kExtentOffset = 16;
constexpr std::uint64_t kRadiusOffset = 32;
constexpr std::uint64_t kAreaRecordBytes = 96;
constexpr std::uint64_t kAnimationHeaderBytes = 24;
constexpr std::uint64_t kAnimationCountOffset = 20;
constexpr std::uint64_t kTrackHeaderBytes = 32;
constexpr std::uint64_t kTrackFirstDataOffset = 24;
constexpr std::uint64_t kTrackSecondDataOffset = 28;
constexpr std::uint64_t kLabelRecordBytes = 40;
constexpr unsigned kByteBits = 8;
std::span<const std::uint8_t> span(const Reader& reader,
                                   resources::Range range) {
  reader.range(range.offset, range.size);
  return reader.b.subspan(static_cast<std::size_t>(range.offset),
                          static_cast<std::size_t>(range.size));
}
float number(const Reader& reader, std::uint64_t offset) {
  const auto result = std::bit_cast<float>(reader.u32(offset));
  if (!std::isfinite(result)) {
    malformed("Nonfinite collision bounds");
  }
  return result;
}
std::uint16_t half(std::span<const std::uint8_t> bytes, std::size_t offset) {
  return static_cast<std::uint16_t>(bytes[offset] |
                                    (bytes[offset + 1] << kByteBits));
}
}  // namespace
CollisionDefinition CollisionDefinition::parse(
    std::span<const std::uint8_t> bytes,
    const std::shared_ptr<memory::Domain>& domain) {
  const Reader reader{bytes};
  reader.range(0, kCollisionHeaderBytes);
  const auto count = reader.u32(kPartCountOffset);
  const auto directory = reader.u32(kPartTableOffset);
  reader.range(directory, count * kWordBytes);
  CollisionDefinition result(domain);
  result.parts_.reserve(count);
  for (std::uint32_t part = 0; part < count; ++part) {
    const auto offset = reader.u32(directory + part * kWordBytes);
    reader.range(offset, kPartHeaderBytes);
    CollisionPart record;
    record.name =
        reader.text_view({offset + kPartNameOffset, kPartNameBytes}, 0);
    for (std::size_t axis = 0; axis < record.center.size(); ++axis) {
      record.center.at(axis) = number(reader, offset + axis * kWordBytes);
      record.extent.at(axis) =
          number(reader, offset + kExtentOffset + axis * kWordBytes);
    }
    record.radius = number(reader, offset + kRadiusOffset);
    record.vertex_count = reader.u32(offset + kPartCountOffset);
    record.triangle_count = reader.u32(offset + kPartTriangleCountOffset);
    record.flags = reader.u32(offset + kPartFlagsOffset);
    record.vertices =
        span(reader, {offset + reader.u32(offset + kPartTableOffset),
                      record.vertex_count * kVectorBytes});
    record.triangles =
        span(reader, {offset + reader.u32(offset + kPartTriangleOffset),
                      record.triangle_count * kTriangleBytes});
    for (std::size_t face = 0; face < record.triangles.size();
         face += kTriangleBytes) {
      for (std::size_t corner = 0; corner < 3; ++corner) {
        if (half(record.triangles, face + corner * 2) >= record.vertex_count) {
          malformed("Collision triangle references missing vertex");
        }
      }
    }
    result.parts_.push_back(record);
  }
  return result;
}
PhysicsCollisionDefinition PhysicsCollisionDefinition::parse(
    std::span<const std::uint8_t> bytes,
    const std::shared_ptr<memory::Domain>& domain) {
  const Reader reader{bytes};
  PhysicsCollisionDefinition result(bytes, domain);
  constexpr std::uint64_t kHeaderBase = 4;
  constexpr std::uint64_t kVersionOffset = 8;
  constexpr std::uint64_t kFlagsOffset = 12;
  constexpr std::uint64_t kExtendedHeaderBytes = 56;
  constexpr std::uint32_t kExtendedVersion = 0x20060411;
  constexpr std::uint64_t kCompactHeaderBytes = 32;
  reader.range(0, kCompactHeaderBytes);
  const auto version = reader.u32(kVersionOffset);
  if (version != 0 && version != kExtendedVersion) {
    unsupported("Unsupported physics collision definition version");
  }
  if (version != 0) {
    reader.range(0, kExtendedHeaderBytes);
  }
  constexpr unsigned kCompactFlagShift = 16;
  const auto flags =
      reader.u32(kFlagsOffset) >> (version == 0 ? kCompactFlagShift : 0);
  if ((flags & 1U) != 0) {
    unsupported("Physics collision input is already pointer-patched");
  }
  auto reference = [&](std::uint64_t field,
                       std::uint64_t base = 0) -> std::uint64_t {
    const auto relative = reader.u32(field);
    if (relative == 0) {
      return 0;
    }
    const auto target = base + relative;
    reader.range(target, 1);
    if (target > UINT32_MAX) {
      malformed("Physics collision offset overflows source address width");
    }
    result.references_.push_back({static_cast<std::uint32_t>(field),
                                  static_cast<std::uint32_t>(target)});
    return target;
  };
  struct DirectoryFields {
    std::uint64_t count, pointer;
  };
  auto directory = [&](DirectoryFields fields, auto visit) {
    const auto table = reference(fields.pointer);
    if (table == 0) {
      return;
    }
    const auto count = reader.u32(fields.count);
    reader.range(table, count * kWordBytes);
    for (std::uint32_t i = 0; i < count; ++i) {
      const auto target = reference(table + i * kWordBytes);
      if (target != 0) {
        visit(target);
      }
    }
  };
  // Bind each shape to its optional transform and referenced geometry.
  constexpr std::uint64_t kShapeDataOffset = 4;
  constexpr std::uint64_t kShapeTransformOffset = 16;
  constexpr std::uint64_t kShapeCountOffset = 20;
  constexpr std::uint64_t kShapeTableOffset = 24;
  constexpr std::uint64_t kGeometryDataOffset = 64;
  constexpr std::uint32_t kFirstGeometryKind = 3, kLastGeometryKind = 5;
  reference(kHeaderBase);
  directory({kHeaderBase + 3 * kWordBytes, kHeaderBase + 4 * kWordBytes},
            [&](std::uint64_t shape) {
              reference(shape + kShapeDataOffset);
              reference(shape + kShapeTransformOffset);
              directory({shape + kShapeCountOffset, shape + kShapeTableOffset},
                        [&](std::uint64_t geometry) {
                          const auto kind = reader.u32(geometry);
                          if (kind >= kFirstGeometryKind &&
                              kind <= kLastGeometryKind) {
                            reference(geometry + kGeometryDataOffset);
                          }
                        });
            });
  // Preserve null entries and source order: placement matrix i belongs to
  // physics body i, whose +8 field selects the model joint.
  constexpr std::uint64_t kBodyCountOffset = 16, kBodyTableOffset = 20;
  constexpr std::uint64_t kBodyJointOffset = 8;
  const auto body_count = reader.u32(kBodyCountOffset);
  const auto body_table = reader.u32(kBodyTableOffset);
  if (body_table != 0) {
    reader.range(body_table, body_count * kWordBytes);
    result.body_joints_.reserve(body_count);
    for (std::uint32_t body = 0; body < body_count; ++body) {
      const auto record = reader.u32(body_table + body * kWordBytes);
      result.body_joints_.push_back(
          record == 0 ? -1
                      : std::bit_cast<std::int32_t>(
                            reader.u32(record + kBodyJointOffset)));
    }
  }
  constexpr std::uint64_t kSecondaryCountWord = 5, kSecondaryTableWord = 6;
  constexpr std::uint64_t kSecondaryDataOffset = 40;
  directory(
      {kHeaderBase + kSecondaryCountWord * kWordBytes,
       kHeaderBase + kSecondaryTableWord * kWordBytes},
      [&](std::uint64_t record) { reference(record + kSecondaryDataOffset); });
  if (version != 0) {
    constexpr std::uint64_t kGroupWord = 7, kBoundsTableWord = 8;
    constexpr std::uint64_t kLinksCountWord = 9, kLinksTableWord = 10;
    constexpr std::uint64_t kExtraCountWord = 11, kExtraTableWord = 12;
    constexpr std::uint64_t kGroupCountOffset = 12, kGroupTableOffset = 16;
    constexpr std::uint64_t kBoundsCountOffset = 28, kBoundsTableOffset = 32;
    constexpr std::uint64_t kLinksCountOffset = 8, kLinksTableOffset = 12;
    const auto group = reference(kHeaderBase + kGroupWord * kWordBytes);
    if (group != 0) {
      // The emitter bit admits an authored joint and two independent colors.
      constexpr std::uint32_t kLocalLightFlag = 0x200;
      constexpr std::uint64_t kLightJoint = 36, kLightDiffuse = 40;
      constexpr std::uint64_t kLightSpecular = 52, kLightAttenuation = 64;
      constexpr std::uint64_t kLightRecordBytes = 76;
      if ((reader.u32(group) & kLocalLightFlag) != 0) {
        reader.range(group, kLightRecordBytes);
        PhysicsLocalLight light;
        light.joint =
            std::bit_cast<std::int32_t>(reader.u32(group + kLightJoint));
        for (std::size_t channel = 0; channel < light.diffuse.size();
             ++channel) {
          auto scalar = [&](std::uint64_t field) {
            const auto value = std::bit_cast<float>(
                reader.u32(group + field + channel * kWordBytes));
            if (!std::isfinite(value)) {
              malformed("Nonfinite physics local light");
            }
            return value;
          };
          light.diffuse.at(channel) = scalar(kLightDiffuse);
          light.specular.at(channel) = scalar(kLightSpecular);
          light.attenuation.at(channel) = scalar(kLightAttenuation);
        }
        result.local_light_ = light;
      }
      directory({group + kGroupCountOffset, group + kGroupTableOffset},
                [](std::uint64_t) {});
    }
    directory({kHeaderBase + 3 * kWordBytes,
               kHeaderBase + kBoundsTableWord * kWordBytes},
              [&](std::uint64_t record) {
                directory(
                    {record + kBoundsCountOffset, record + kBoundsTableOffset},
                    [](std::uint64_t) {});
              });
    directory({kHeaderBase + kLinksCountWord * kWordBytes,
               kHeaderBase + kLinksTableWord * kWordBytes},
              [&](std::uint64_t record) {
                directory(
                    {record + kLinksCountOffset, record + kLinksTableOffset},
                    [](std::uint64_t) {});
              });
    directory({kHeaderBase + kExtraCountWord * kWordBytes,
               kHeaderBase + kExtraTableWord * kWordBytes},
              [](std::uint64_t) {});
  }
  // The outer root uses root-relative children, then child-relative payloads.
  const auto root = reference(0);
  if (root != 0) {
    const auto count = reader.u32(root);
    reader.range(root + kWordBytes, count * kWordBytes);
    for (std::uint32_t i = 0; i < count; ++i) {
      const auto child = reference(root + (i + 1ULL) * kWordBytes, root);
      if (child == 0) {
        malformed("Null physics collision root child");
      }
      reference(child + 2 * kWordBytes, child);
      reference(child + 3 * kWordBytes, child);
    }
  }
  // Resolve convex clouds in their separate SDK-cache order, then attach each
  // typed shape to its authored body and expanded row-vector local pose.
  memory::Vector<std::span<const std::uint8_t>> convex_clouds{
      memory::Allocator<std::span<const std::uint8_t>>(domain)};
  constexpr std::uint64_t kCloudKindOffset = 2, kCloudCountOffset = 4;
  constexpr std::uint64_t kCloudPointsOffset = 8;
  if (root != 0) {
    const auto count = reader.u32(root);
    for (std::uint32_t i = 0; i < count; ++i) {
      const auto child = root + reader.u32(root + (i + 1ULL) * kWordBytes);
      reader.range(child, kCloudPointsOffset + kWordBytes);
      if (half(bytes, static_cast<std::size_t>(child + kCloudKindOffset)) ==
          0) {
        const auto points =
            half(bytes, static_cast<std::size_t>(child + kCloudCountOffset));
        const auto data = reader.u32(child + kCloudPointsOffset);
        if (points != 0 && data == 0) {
          malformed("Null convex collision point cloud");
        }
        convex_clouds.push_back(
            span(reader, {child + data, points * kVectorBytes}));
        for (std::uint64_t point = 0; point < points; ++point) {
          for (std::uint64_t axis = 0; axis < 3; ++axis) {
            (void)number(reader, child + data + point * kVectorBytes +
                                     axis * kWordBytes);
          }
        }
      }
    }
  }
  constexpr std::uint64_t kShapePoseOffset = 16, kShapeGeometryIndex = 68;
  constexpr std::uint64_t kShapeDimensionsOffset = 64;
  constexpr std::size_t kRotationRows = 3, kTranslationRow = 3;
  if (body_table != 0) {
    for (std::uint32_t body = 0; body < body_count; ++body) {
      const auto record = reader.u32(body_table + body * kWordBytes);
      if (record == 0) {
        continue;
      }
      const auto count = reader.u32(record + kShapeCountOffset);
      const auto table = reader.u32(record + kShapeTableOffset);
      if (count != 0 && table == 0) {
        malformed("Null physics shape directory");
      }
      reader.range(table, count * kWordBytes);
      for (std::uint32_t shape = 0; shape < count; ++shape) {
        const auto at = reader.u32(table + shape * kWordBytes);
        if (at == 0) {
          continue;
        }
        const auto kind = reader.u32(at);
        if (kind >
            static_cast<std::uint32_t>(PhysicsShapeKind::triangle_mesh_alt)) {
          unsupported("Unsupported physics shape kind");
        }
        PhysicsShape value;
        value.kind = static_cast<PhysicsShapeKind>(kind);
        value.body = body;
        for (std::size_t row = 0; row < kRotationRows; ++row) {
          for (std::size_t column = 0; column < kRotationRows; ++column) {
            value.local_pose.at(row).at(column) =
                number(reader, at + kShapePoseOffset +
                                   (column * kRotationRows + row) * kWordBytes);
          }
          value.local_pose[kTranslationRow].at(row) = number(
              reader, at + kShapePoseOffset +
                          (kRotationRows * kRotationRows + row) * kWordBytes);
        }
        if (value.kind == PhysicsShapeKind::convex) {
          const auto index = reader.u32(at + kShapeGeometryIndex);
          if (index >= convex_clouds.size()) {
            malformed("Physics shape convex index exceeds cloud directory");
          }
          value.convex_points = convex_clouds.at(index);
        } else if (value.kind <= PhysicsShapeKind::capsule) {
          const std::size_t dimensions =
              value.kind == PhysicsShapeKind::box
                  ? 3
                  : (value.kind == PhysicsShapeKind::capsule ? 2 : 1);
          for (std::size_t axis = 0; axis < dimensions; ++axis) {
            value.dimensions.at(axis) =
                number(reader, at + kShapeDimensionsOffset + axis * kWordBytes);
            if (value.dimensions.at(axis) < 0) {
              malformed("Negative physics shape dimension");
            }
          }
        }
        result.shapes_.push_back(value);
      }
    }
  }
  return result;
}
AreaDefinition AreaDefinition::parse(
    std::span<const std::uint8_t> bytes,
    const std::shared_ptr<memory::Domain>& domain) {
  const Reader reader{bytes};
  if (reader.tag_view(0) != "AREA" || reader.u32(4) != 1) {
    unsupported("Unsupported area definition version");
  }
  const auto count = reader.u32(8);
  const auto directory = reader.u32(12);
  reader.range(directory, count * kWordBytes);
  AreaDefinition result(domain);
  result.records_.reserve(count);
  for (std::uint32_t i = 0; i < count; ++i) {
    result.records_.push_back(span(
        reader, {reader.u32(directory + i * kWordBytes), kAreaRecordBytes}));
  }
  return result;
}
ScalarAnimationDefinition ScalarAnimationDefinition::parse(
    std::span<const std::uint8_t> bytes) {
  const Reader reader{bytes};
  reader.range(0, kAnimationHeaderBytes);
  if (reader.tag_view(0) != "FLAN") {
    unsupported("Unsupported scalar animation signature");
  }
  const auto count = reader.u32(8);
  const auto times = reader.u32(12);
  const auto channels = reader.u32(16);
  const auto values = reader.u32(20);
  if (times < kAnimationHeaderBytes || channels < times || values < channels ||
      values > bytes.size()) {
    malformed("Invalid scalar animation section ordering");
  }
  reader.range(times, count * kWordBytes);
  if (count * kWordBytes > channels - times) {
    malformed("Scalar animation times overlap channel codes");
  }
  return {reader.u32(4), count, span(reader, {times, count * kWordBytes}),
          span(reader, {channels, values - channels}),
          span(reader, {values, bytes.size() - values})};
}
MaterialAnimationDefinition MaterialAnimationDefinition::parse(
    std::span<const std::uint8_t> bytes,
    const std::shared_ptr<memory::Domain>& domain) {
  const Reader reader{bytes};
  reader.range(0, kAnimationHeaderBytes);
  if (reader.tag_view(0) != "MTAB") {
    unsupported("Unsupported material animation signature");
  }
  const auto count = reader.u32(kAnimationCountOffset);
  reader.range(kAnimationHeaderBytes, count * kWordBytes);
  MaterialAnimationDefinition result(bytes, domain);
  result.tracks_.reserve(count);
  for (std::uint32_t i = 0; i < count; ++i) {
    const auto offset = reader.u32(kAnimationHeaderBytes + i * kWordBytes);
    if (offset == 0) {
      result.tracks_.push_back({});
      continue;
    }
    const auto header = span(reader, {offset, kTrackHeaderBytes});
    const auto first = reader.u32(offset + kTrackFirstDataOffset);
    const auto second = reader.u32(offset + kTrackSecondDataOffset);
    reader.range(first, kWordBytes);
    reader.range(second, kWordBytes);
    result.tracks_.push_back({header, first, second});
  }
  return result;
}
MaterialAnimationLabels MaterialAnimationLabels::parse(
    std::span<const std::uint8_t> bytes) {
  const Reader reader{bytes};
  if (reader.tag_view(0) != "CILB") {
    unsupported("Unsupported material animation labels");
  }
  const auto count = reader.u32(4);
  constexpr std::uint64_t kLabelHeaderBytes = 8;
  const auto records =
      span(reader, {kLabelHeaderBytes, count * kLabelRecordBytes});
  for (std::uint32_t i = 0; i < count; ++i) {
    (void)reader.text_view(
        {kLabelHeaderBytes + i * kLabelRecordBytes + kWordBytes,
         kPartNameBytes},
        0);
  }
  return {count, records};
}
std::string_view MaterialAnimationLabels::name(std::size_t index) const {
  if (index >= count) malformed("Material animation label index exceeds table");
  const Reader reader{records};
  return reader.text_view(
      {index * kLabelRecordBytes + kWordBytes, kPartNameBytes}, 0);
}
std::uint32_t MaterialAnimationLabels::track(std::size_t index) const {
  if (index >= count) malformed("Material animation label index exceeds table");
  const Reader reader{records};
  return reader.u32(index * kLabelRecordBytes + kWordBytes + kPartNameBytes);
}
}  // namespace bw::formats
