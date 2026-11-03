#ifndef BW_TESTS_UNIT_RENDERING_RHI_TEST_OBJECTS_HPP
#define BW_TESTS_UNIT_RENDERING_RHI_TEST_OBJECTS_HPP
// Test-local concrete RHI objects, built from the public types only: the
// public headers give buffers, textures, views and plain resources protected
// constructors so a backend can derive them, and a test derives them the
// same way. Each object records its destruction in a DestructionLog so a
// test can observe when and in which order the delete queue destroyed it.
#include "bw/rendering/rhi/descriptor_allocator.hpp"
#include "bw/rendering/rhi/lifetime.hpp"
#include "bw/rendering/rhi/resources.hpp"

#include "test_main.hpp"

#include <array>
#include <cstddef>
#include <string_view>
#include <utility>

namespace bw_rhi_test {

// Destruction order by object id; fixed capacity so a destructor never
// allocates.
struct DestructionLog {
  std::array<int, 32> ids{};
  std::size_t count{};
  void record(int id) noexcept {
    if (count < ids.size()) {
      ids[count] = id;
    }
    ++count;
  }
  [[nodiscard]] bool destroyed(int id) const noexcept {
    for (std::size_t i = 0; i < count && i < ids.size(); ++i) {
      if (ids[i] == id) {
        return true;
      }
    }
    return false;
  }
};

class TestResource final : public bw::rhi::Resource {
public:
  TestResource(bw::rhi::DeferredDeleteQueue& queue, DestructionLog& log, int id) noexcept
      : Resource(queue), log_(&log), id_(id) {}

private:
  ~TestResource() override { log_->record(id_); }
  DestructionLog* log_;
  int id_;
};

class TestBuffer final : public bw::rhi::Buffer {
public:
  TestBuffer(bw::rhi::DeferredDeleteQueue& queue, const bw::rhi::BufferDescription& description,
             DestructionLog* log = nullptr, int id = 0) noexcept
      : Buffer(queue, description), log_(log), id_(id) {}

private:
  ~TestBuffer() override {
    if (log_ != nullptr) {
      log_->record(id_);
    }
  }
  DestructionLog* log_;
  int id_;
};

class TestTexture final : public bw::rhi::Texture {
public:
  TestTexture(bw::rhi::DeferredDeleteQueue& queue, const bw::rhi::TextureDescription& description,
              DestructionLog* log = nullptr, int id = 0) noexcept
      : Texture(queue, description), log_(log), id_(id) {}

private:
  ~TestTexture() override {
    if (log_ != nullptr) {
      log_->record(id_);
    }
  }
  DestructionLog* log_;
  int id_;
};

class TestShaderResourceView final : public bw::rhi::ShaderResourceView {
public:
  TestShaderResourceView(bw::rhi::DeferredDeleteQueue& queue,
                         bw::rhi::Ref<bw::rhi::Viewable> resource, bw::rhi::DescriptorSlot slot,
                         bw::rhi::DescriptorAllocators& pools, DestructionLog& log, int id) noexcept
      : ShaderResourceView(queue, std::move(resource), slot, pools), log_(&log), id_(id) {}

private:
  ~TestShaderResourceView() override { log_->record(id_); }
  DestructionLog* log_;
  int id_;
};

// A new object owned by the returned Ref: its count is one.
template <class T, class... Arguments> bw::rhi::Ref<T> make(Arguments&&... arguments) {
  return bw::rhi::Ref<T>(new T(std::forward<Arguments>(arguments)...));
}

inline bw::rhi::BufferDescription shader_buffer_description(std::string_view name) {
  return bw::rhi::BufferDescription{
      .size = 256,
      .stride = 0,
      .usage = bw::rhi::BufferUsage::ShaderResource,
      .initial_access = bw::rhi::Access::ShaderResourcePixel,
      .debug_name = name,
  };
}

inline bw::rhi::TextureDescription render_target_description(std::string_view name) {
  bw::rhi::TextureDescription description{};
  description.extent = bw::rhi::Extent3{64, 32, 1};
  description.format = bw::rhi::Format::R8G8B8A8Unorm;
  description.usage = bw::rhi::TextureUsage::RenderTarget;
  description.initial_access = bw::rhi::Access::RenderTarget;
  description.debug_name = name;
  return description;
}

} // namespace bw_rhi_test

#endif
