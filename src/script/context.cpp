// Memory extension: native Context owns a bounded reclaiming heap backed by its
// injected Domain. Lua state/global state/coroutines retain this context
// through destruction; close runs before heap release. Legacy default uses
// stock Lua. Wrapper impl/map keys/nodes/argument vector use Domain; public
// Value strings, Reply vectors and type-erased callback captures remain
// caller-owned boundary types. Copies across those APIs are not claimed
// allocator-complete. Native heap defaults to recovered root64KiB, with
// measured native occupancy exposed; no automatic growth. Constructor/load
// failures throw; pump failure is terminal. All allocating host Lua API
// operations use a protected C boundary, so Lua longjmp cannot bypass C++
// owners. Context stays single-threaded; independent contexts have independent
// callbacks, no global current allocator. Design R.1 (#rr-raii), SL.C.1
// (#rclib-jmp); performance EMB.4 bounded backing without claiming first-fit
// constant-time. Implementation F.6 (#rf-noexcept), MEM.6
// (guidelines/memory/MEM.6-inject-allocators-behind-one-interface.md) routes
// allocation through explicit context capability. See
// lua-memory-implementation.md. E2: owned Lua 5.0.2 context with
// immediate/waiting native bridges. Single thread, no reentrant calls. Binding
// callbacks own captures; borrowed services must outlive the context. Native
// argument spans last only through the call; a pending callback must copy any
// arguments it retains. Destroy cancels the retained poll and closes Lua.
// Moved-from contexts may only be destroyed/assigned. Script errors are
// terminal pump results; API misuse/load errors throw. No file/OS libraries are
// opened. Algorithm: load chunk, anchor coroutine, start named function,
// resume; on a bridge wait poll immediately, return while true, clear then
// resume when false. Cost follows executed Lua instructions; arguments/results
// copy strings once. Optimization: retained coroutine/poll avoids replaying
// completed script work. Safety budget: 100000 instructions per resume and 64
// bridge resumes per pump; these are host guards, not recovered game timing.
// Not an untrusted-code sandbox. Design/implementation MCP: I.30
// (#ri-encapsulate) confines C/runtime details; SL.con.3 (#rsl-bounds) checks
// byte/index access. cpp-performance COPY.5
// (guidelines/copy-move/COPY.5-resource-owning-types-are-move-only.md):
// unique runtime owners cannot be copied; moves preserve stable heap identity.

#include "bw/script/context.hpp"

#include "bw/script/chunk.hpp"
extern "C" {
#include <lauxlib.h>
#include <lua.h>
}
#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <map>
#include <optional>

#include "bw/memory/allocator.hpp"
namespace bw::script {
namespace {
using resources::Error;
using resources::ErrorCode;
constexpr int kInstructionBudget = 100000;
constexpr std::size_t kResumeBudget = 64;
constexpr std::size_t kDiagnosticBytes = 512;
struct Close {
  void operator()(lua_State* state) const { lua_close(state); }
};
Value read_value(lua_State* state, int index) {
  switch (lua_type(state, index)) {
    case LUA_TNIL:
      return {};
    case LUA_TBOOLEAN:
      return lua_toboolean(state, index) != 0;
    case LUA_TNUMBER:
      return lua_tonumber(state, index);
    case LUA_TSTRING:
      return std::string(lua_tostring(state, index), lua_strlen(state, index));
    default:
      throw Error(ErrorCode::unsupported, "Native argument type unsupported");
  }
}
void push_value(lua_State* state, const Value& value) {
  if (const auto* number = std::get_if<double>(&value)) {
    lua_pushnumber(state, *number);
  } else if (const auto* boolean = std::get_if<bool>(&value)) {
    lua_pushboolean(state, *boolean ? 1 : 0);
  } else if (const auto* text = std::get_if<std::string>(&value)) {
    lua_pushlstring(state, text->data(), text->size());
  } else {
    lua_pushnil(state);
  }
}
void budget(lua_State* state, lua_Debug*) {
  lua_pushliteral(state, "Lua instruction budget exceeded");
  lua_error(state);
}
}  // namespace
struct Context::Impl {
  struct Binding {
    Impl* owner;
    Native native;
  };
  std::shared_ptr<memory::Domain> domain;
  std::optional<memory::CoalescingHeap> heap;
  std::unique_ptr<lua_State, Close> state;
  lua_State*
      coroutine{};  // Borrowed; anchored by thread_ref in parent's registry.
  int thread_ref{LUA_NOREF};
  using Entry = std::pair<const memory::String, Binding>;
  std::map<memory::String, Binding, std::less<>, memory::Allocator<Entry>>
      bindings;
  Reply reply;
  std::function<bool()> pending;
  RunState phase{RunState::idle};
  std::array<char, kDiagnosticBytes> error{};
  template <typename T>
  memory::Allocator<T> allocator() const {
    return domain ? memory::Allocator<T>(domain) : memory::Allocator<T>{};
  }
  void set_error(std::string_view text) noexcept {
    const auto count = std::min(text.size(), error.size() - 1);
    std::copy_n(text.begin(), count, error.begin());
    std::span(error).subspan(count).front() = '\0';
  }
  static void* allocate_lua(void* userdata, void* pointer, std::size_t,
                            std::size_t bytes) noexcept {
    return static_cast<memory::CoalescingHeap*>(userdata)->reallocate(pointer,
                                                                      bytes);
  }
  explicit Impl(std::shared_ptr<memory::Domain> memory = {},
                std::size_t heap_bytes = kRootHeapBytes)
      : domain(std::move(memory)), bindings(std::less<>{}, allocator<Entry>()) {
    if (domain) {
      heap.emplace(domain, heap_bytes);
      state.reset(lua_open_with_allocator(allocate_lua, &*heap));
    } else {
      state.reset(lua_open());
    }
    if (!state) {
      throw std::bad_alloc();
    }
  }
  ~Impl() {
    pending = {};
    reply = {};
    state.reset();
  }
  Impl(const Impl&) = delete;
  Impl& operator=(const Impl&) = delete;
  Impl(Impl&&) = delete;
  Impl& operator=(Impl&&) = delete;
  struct BindRequest {
    Binding* binding;
    const char* name;
  };
  static void protected_bind(lua_State* active, void* userdata) {
    auto* request = static_cast<BindRequest*>(userdata);
    lua_pushlightuserdata(active, request->binding);
    lua_pushcclosure(active, bridge, 1);
    lua_setglobal(active, request->name);
  }
  struct StartRequest {
    Impl* owner;
    const char* name;
    bool found;
  };
  static void protected_start(lua_State* active, void* userdata) {
    auto* request = static_cast<StartRequest*>(userdata);
    auto* owner = request->owner;
    if (owner->thread_ref != LUA_NOREF) {
      luaL_unref(active, LUA_REGISTRYINDEX, owner->thread_ref);
      owner->thread_ref = LUA_NOREF;
    }
    owner->coroutine = lua_newthread(active);
    owner->thread_ref = luaL_ref(active, LUA_REGISTRYINDEX);
    lua_getglobal(active, request->name);
    request->found = lua_isfunction(active, -1) != 0;
    lua_xmove(active, owner->coroutine, 1);
    // Recovered reclaiming mode collects on preparing each global coroutine;
    // live registry references and pending frames remain owned by the VM.
    lua_setgcthreshold(active, 0);
  }
  struct GlobalRequest {
    const char* name;
  };
  static void protected_global(lua_State* active, void* userdata) {
    lua_getglobal(active, static_cast<GlobalRequest*>(userdata)->name);
  }
  void checked(int status) {
    if (status == 0) return;
    const char* message = lua_type(state.get(), -1) == LUA_TSTRING
                              ? lua_tostring(state.get(), -1)
                              : nullptr;
    set_error(message ? message : "Lua host operation failed");
    phase = RunState::failed;
    if (status == LUA_ERRMEM) throw std::bad_alloc();
    throw Error(ErrorCode::unsupported, error.data());
  }
  void capture(Binding& binding, lua_State* active) {
    memory::Vector<Value> args(allocator<Value>());
    const int count = lua_gettop(active);
    args.reserve(static_cast<std::size_t>(count));
    for (int i = 1; i <= count; ++i) {
      args.push_back(read_value(active, i));
    }
    reply = binding.native(args);
    if (reply.pending && !reply.values.empty()) {
      throw Error(ErrorCode::unsupported, "Wait bridges return no Lua values");
    }
    pending = std::move(reply.pending);
  }
  static int bridge(lua_State* active) {
    auto* binding =
        static_cast<Binding*>(lua_touserdata(active, lua_upvalueindex(1)));
    // No C++ objects with destructors survive across lua_error/lua_yield below.
    // Lua's longjmp stays inside its protected resume/load boundary.
    try {
      binding->owner->capture(*binding, active);
    } catch (const std::exception& failure) {
      binding->owner->set_error(failure.what());
    } catch (...) {
      binding->owner->set_error("Unknown native exception");
    }
    if (binding->owner->error.front() != '\0') {
      lua_pushstring(active, binding->owner->error.data());
      return lua_error(active);
    }
    if (binding->owner->pending) {
      return lua_yield(active, 0);
    }
    const auto result_count = binding->owner->reply.values.size();
    if (result_count >
            static_cast<std::size_t>(std::numeric_limits<int>::max()) ||
        !lua_checkstack(active, static_cast<int>(result_count))) {
      lua_pushliteral(active, "Native result stack limit");
      return lua_error(active);
    }
    for (const auto& value : binding->owner->reply.values) {
      push_value(active, value);
    }
    return static_cast<int>(binding->owner->reply.values.size());
  }
  void load(std::string_view source) {
    if (phase == RunState::running || phase == RunState::pending ||
        phase == RunState::failed) {
      throw Error(ErrorCode::unsupported, "Cannot load while executing");
    }
    lua_settop(state.get(), 0);
    if (luaL_loadbuffer(state.get(), source.data(), source.size(), "root") !=
        0) {
      const auto* message = lua_type(state.get(), -1) == LUA_TSTRING
                                ? lua_tostring(state.get(), -1)
                                : nullptr;
      throw Error(ErrorCode::malformed, message ? message : "Lua load failed");
    }
    lua_sethook(state.get(), budget, LUA_MASKCOUNT, kInstructionBudget);
    if (lua_pcall(state.get(), 0, 0, 0) != 0) {
      const auto* message = lua_type(state.get(), -1) == LUA_TSTRING
                                ? lua_tostring(state.get(), -1)
                                : nullptr;
      throw Error(ErrorCode::malformed,
                  message ? message : "Lua top-level failed");
    }
    phase = RunState::idle;
  }
};
Context::Context() : impl_(std::make_shared<Impl>()) {}
Context::Context(const std::shared_ptr<memory::Domain>& domain,
                 std::size_t heap_bytes)
    : impl_(std::allocate_shared<Impl>(memory::Allocator<Impl>(domain), domain,
                                       heap_bytes)) {}
memory::HeapSnapshot Context::memory_snapshot() const {
  return impl_->heap ? impl_->heap->snapshot() : memory::HeapSnapshot{};
}
Context::~Context() = default;
Context::Context(Context&&) noexcept = default;
Context& Context::operator=(Context&&) noexcept = default;
void Context::bind(std::string name, Native native) {
  if (!native || impl_->phase != RunState::idle) {
    throw Error(ErrorCode::unsupported, "Bind before execution");
  }
  auto [it, added] = impl_->bindings.emplace(
      memory::String(name.begin(), name.end(), impl_->allocator<char>()),
      Impl::Binding{impl_.get(), std::move(native)});
  if (!added) {
    throw Error(ErrorCode::ambiguous, "Duplicate native binding");
  }
  Impl::BindRequest request{&it->second, it->first.c_str()};
  impl_->checked(
      lua_bw_protected(impl_->state.get(), Impl::protected_bind, &request));
}
void Context::load_binary(std::span<const std::uint8_t> bytes) {
  if (impl_->domain) {
    const auto chunk = host_chunk(bytes, impl_->domain);
    impl_->load(std::string_view(chunk.data(), chunk.size()));
  } else {
    impl_->load(host_chunk(bytes));
  }
}
void Context::load_source(std::string_view source) { impl_->load(source); }
void Context::start(const std::string& function) {
  if (impl_->phase == RunState::running || impl_->phase == RunState::pending ||
      impl_->phase == RunState::failed) {
    throw Error(ErrorCode::unsupported, "Context is not ready to start");
  }
  Impl::StartRequest request{impl_.get(), function.c_str(), false};
  impl_->checked(
      lua_bw_protected(impl_->state.get(), Impl::protected_start, &request));
  if (!request.found) {
    throw Error(ErrorCode::not_found, "Lua function: " + function);
  }
  impl_->phase = RunState::running;
}
RunResult Context::pump() {
  if (impl_->phase != RunState::running && impl_->phase != RunState::pending) {
    return {impl_->phase, impl_->error.data()};
  }
  try {
    for (std::size_t i = 0; i < kResumeBudget; ++i) {
      if (impl_->pending) {
        if (impl_->pending()) {
          impl_->phase = RunState::pending;
          return {impl_->phase, {}};
        }
        impl_->pending = {};
      }
      lua_sethook(impl_->coroutine, budget, LUA_MASKCOUNT, kInstructionBudget);
      const int status = lua_resume(impl_->coroutine, 0);
      if (status != 0) {
        const auto* message = lua_type(impl_->coroutine, -1) == LUA_TSTRING
                                  ? lua_tostring(impl_->coroutine, -1)
                                  : nullptr;
        throw Error(ErrorCode::unsupported,
                    message ? message : "Lua execution failed");
      }
      if (!impl_->pending) {
        impl_->phase = RunState::complete;
        return {impl_->phase, {}};
      }
    }
    impl_->phase = RunState::running;
    return {impl_->phase, "Host resume budget reached"};
  } catch (const std::exception& failure) {
    impl_->pending = {};
    impl_->set_error(failure.what());
    impl_->phase = RunState::failed;
    return {impl_->phase, impl_->error.data()};
  } catch (...) {
    impl_->pending = {};
    impl_->set_error("Unknown pending callback exception");
    impl_->phase = RunState::failed;
    return {impl_->phase, impl_->error.data()};
  }
}
Value Context::global(const std::string& name) const {
  // The protected callback only borrows this name during the C operation.
  Impl::GlobalRequest request{name.c_str()};
  impl_->checked(
      lua_bw_protected(impl_->state.get(), Impl::protected_global, &request));
  try {
    auto value = read_value(impl_->state.get(), -1);
    lua_pop(impl_->state.get(), 1);
    return value;
  } catch (...) {
    lua_pop(impl_->state.get(), 1);
    throw;
  }
}
}  // namespace bw::script
