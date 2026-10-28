// Jolt global callbacks retain one explicit engine allocation owner. R.1
// (#rr-raii) binds registration cleanup; MEM.6 injects allocation at this
// subsystem boundary. Registration is serialized; active world operations
// retain Runtime, so callbacks cannot be replaced while any world is alive.
// Upstream allocation exhaustion is fatal: Jolt internal constructors do not
// promise recoverable OOM. Engine capacities/descriptor failures are
// recoverable before provider admission. Never return null to unchecked
// upstream allocation.
#include "engine/physics/jolt/runtime.hpp"

#include <Jolt/Core/Memory.h>
#include <Jolt/RegisterTypes.h>

#include <algorithm>
#include <cstring>
#include <exception>
#include <mutex>
#include <stdexcept>
#include <utility>

#include "bw/memory/allocator.hpp"
#include "bw/physics/world.hpp"

namespace bw::physics {
namespace {
struct Registration {
  std::mutex mutex;
  memory::Domain* domain{};
};
Registration& registration() {
  static Registration value;
  return value;
}

void* allocate_aligned(std::size_t bytes, std::size_t alignment) {
  if (registration().domain == nullptr) std::terminate();
  void* result = registration().domain->try_allocate_zeroed(bytes, alignment);
  if (result == nullptr) std::terminate();
  return result;
}
void* allocate(std::size_t bytes) {
  return allocate_aligned(bytes, alignof(std::max_align_t));
}
void release(void* block) { memory::Domain::release(block); }
void* reallocate(void* block, std::size_t old_bytes, std::size_t new_bytes) {
  void* result = allocate(new_bytes);
  if (block != nullptr) {
    std::memcpy(result, block, std::min(old_bytes, new_bytes));
  }
  release(block);
  return result;
}
}  // namespace
namespace detail {
Runtime::Runtime(std::shared_ptr<memory::Domain> domain)
    : domain_(std::move(domain)) {
  if (!domain_) throw std::invalid_argument("Physics requires a memory domain");
  const std::lock_guard lock(registration().mutex);
  if (registration().domain != nullptr || JPH::Factory::sInstance != nullptr) {
    throw std::logic_error("A physics runtime is already registered");
  }
  registration().domain = domain_.get();
  JPH::Allocate = allocate;
  JPH::Reallocate = reallocate;
  JPH::Free = release;
  JPH::AlignedAllocate = allocate_aligned;
  JPH::AlignedFree = release;
  factory_.emplace();
  JPH::Factory::sInstance = &*factory_;
  JPH::RegisterTypes();
}
Runtime::~Runtime() {
  const std::lock_guard lock(registration().mutex);
  JPH::UnregisterTypes();
  JPH::Factory::sInstance = nullptr;
  factory_.reset();
  registration().domain = nullptr;
  // Keep owner-routed release callbacks installed. No live upstream allocations
  // survive this point; a new Runtime installs the same functions with its
  // owner.
}
}  // namespace detail
Runtime::Runtime(std::shared_ptr<memory::Domain> domain) {
  if (!domain) throw std::invalid_argument("Physics requires a memory domain");
  runtime_ = std::allocate_shared<detail::Runtime>(
      memory::Allocator<detail::Runtime>(domain), std::move(domain));
}
Runtime::~Runtime() = default;
}  // namespace bw::physics
