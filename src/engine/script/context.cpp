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
#include <utility>

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
  struct Thread {
    // The registry owns the Lua object; this borrow never outlives that anchor.
    lua_State* coroutine{};
    int thread_ref{LUA_NOREF};
    CoroutineId id{};
    Reply reply;
    std::function<bool()> pending;
    RunState phase{RunState::idle};
    int initial_arguments{};
    std::array<char, kDiagnosticBytes> error{};
    void set_error(std::string_view text) noexcept {
      const auto count = std::min(text.size(), error.size() - 1);
      std::copy_n(text.begin(), count, error.begin());
      std::span(error).subspan(count).front() = '\0';
    }
  };
  using Entry = std::pair<const memory::String, Binding>;
  std::map<memory::String, Binding, std::less<>, memory::Allocator<Entry>>
      bindings;
  Thread primary;
  memory::Vector<Thread> children;
  Thread* active_thread{};
  std::uint64_t next_id{1};
  bool pumping{}, started{}, reserved{};
  template <typename T>
  memory::Allocator<T> allocator() const {
    return domain ? memory::Allocator<T>(domain) : memory::Allocator<T>{};
  }
  static void* allocate_lua(void* userdata, void* pointer, std::size_t,
                            std::size_t bytes) noexcept {
    return static_cast<memory::CoalescingHeap*>(userdata)->reallocate(pointer,
                                                                      bytes);
  }
  explicit Impl(std::shared_ptr<memory::Domain> memory = {},
                std::size_t heap_bytes = kRootHeapBytes)
      : domain(std::move(memory)),
        bindings(std::less<>{}, allocator<Entry>()),
        children(allocator<Thread>()) {
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
    // Cancel captured work and close the VM while its backing heap is still
    // alive.
    primary.pending = {};
    primary.reply = {};
    for (auto& child : children) {
      child.pending = {};
      child.reply = {};
    }
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
    Thread* thread;
    std::string_view name;
    bool found;
    std::span<const Value> arguments;
    int argument_status{};
  };
  static void protected_arguments(lua_State* active, void* userdata) {
    const auto* request = static_cast<StartRequest*>(userdata);
    if (!lua_checkstack(active, request->thread->initial_arguments)) {
      lua_pushliteral(active, "Lua argument stack limit");
      lua_error(active);
    }
    for (const auto& value : request->arguments) push_value(active, value);
  }
  static void protected_start(lua_State* active, void* userdata) {
    // Anchor the new coroutine before any execution can yield or trigger GC.
    auto* request = static_cast<StartRequest*>(userdata);
    auto* owner = request->thread;
    if (owner->thread_ref != LUA_NOREF) {
      luaL_unref(active, LUA_REGISTRYINDEX, owner->thread_ref);
      owner->thread_ref = LUA_NOREF;
    }
    owner->coroutine = lua_newthread(active);
    owner->thread_ref = luaL_ref(active, LUA_REGISTRYINDEX);
    lua_pushlstring(active, request->name.data(), request->name.size());
    lua_gettable(active, LUA_GLOBALSINDEX);
    request->found = lua_isfunction(active, -1) != 0;
    lua_xmove(active, owner->coroutine, 1);
    // Lua5.0 resume receives nargs only on the first entry. Keep all
    // potentially allocating pushes inside this protected C call, with no owned
    // C++ local. Each Lua thread has its own error jump chain: protect pushes
    // on the child itself, then propagate its error into the caller's protected
    // frame.
    request->argument_status =
        lua_bw_protected(owner->coroutine, protected_arguments, request);
    if (request->argument_status != 0) {
      lua_xmove(owner->coroutine, active, 1);
      lua_error(active);
    }
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
  void checked(int status, Thread& thread) {
    if (status == 0) return;
    const char* message = lua_type(state.get(), -1) == LUA_TSTRING
                              ? lua_tostring(state.get(), -1)
                              : nullptr;
    thread.set_error(message ? message : "Lua host operation failed");
    thread.phase = RunState::failed;
    lua_settop(state.get(), 0);
    if (status == LUA_ERRMEM) throw std::bad_alloc();
    throw Error(ErrorCode::unsupported, thread.error.data());
  }
  void require_not_pumping() const {
    if (pumping || active_thread) {
      throw Error(ErrorCode::unsupported, "Reentrant Lua task operation");
    }
  }
  bool children_live() const {
    return std::ranges::any_of(
        children, [](const Thread& child) { return child.id.value != 0; });
  }
  Thread& find(CoroutineId id) {
    if (id.value != 0) {
      const auto found = std::ranges::find(children, id, &Thread::id);
      if (found != children.end()) return *found;
    }
    throw Error(ErrorCode::not_found, "Stale Lua coroutine identity");
  }
  static void protected_retire(lua_State* active, void* userdata) {
    auto* thread = static_cast<Thread*>(userdata);
    if (thread->thread_ref != LUA_NOREF) {
      luaL_unref(active, LUA_REGISTRYINDEX, thread->thread_ref);
      thread->thread_ref = LUA_NOREF;
    }
    thread->coroutine = nullptr;
    // Release unreachable stacks now rather than allowing canceled tasks to
    // accumulate until a later task happens to trigger the collector.
    lua_setgcthreshold(active, 0);
  }
  void retire(Thread& thread) {
    thread.pending = {};
    thread.reply = {};
    checked(lua_bw_protected(state.get(), protected_retire, &thread), thread);
    thread = Thread{};
  }
  void start_thread(Thread& thread, std::string_view function,
                    std::span<const Value> arguments = {}) {
    require_not_pumping();
    if (thread.phase == RunState::running ||
        thread.phase == RunState::pending || thread.phase == RunState::failed) {
      throw Error(ErrorCode::unsupported, "Context task is not ready to start");
    }
    started = true;
    thread.reply = {};
    thread.error = {};
    if (arguments.size() >
        static_cast<std::size_t>(std::numeric_limits<int>::max())) {
      throw Error(ErrorCode::unsupported, "Lua argument count exceeds API");
    }
    thread.initial_arguments = static_cast<int>(arguments.size());
    StartRequest request{&thread, function, false, arguments};
    const int status = lua_bw_protected(state.get(), protected_start, &request);
    checked(request.argument_status == LUA_ERRMEM ? LUA_ERRMEM : status,
            thread);
    if (!request.found) {
      throw Error(ErrorCode::not_found,
                  "Lua function: " + std::string(function));
    }
    thread.phase = RunState::running;
  }
  RunResult pump_thread(Thread& thread);
  void capture(Binding& binding, lua_State* active) {
    if (!active_thread) {
      throw std::logic_error("Native call has no active task");
    }
    auto& thread = *active_thread;
    memory::Vector<Value> args(allocator<Value>());
    const int count = lua_gettop(active);
    args.reserve(static_cast<std::size_t>(count));
    for (int i = 1; i <= count; ++i) {
      args.push_back(read_value(active, i));
    }
    thread.reply = binding.native(args);
    if (thread.reply.pending && !thread.reply.values.empty()) {
      throw Error(ErrorCode::unsupported, "Wait bridges return no Lua values");
    }
    thread.pending = std::move(thread.reply.pending);
  }
  static int bridge(lua_State* active) {
    auto* binding =
        static_cast<Binding*>(lua_touserdata(active, lua_upvalueindex(1)));
    auto* thread = binding->owner->active_thread;
    if (thread == nullptr) {
      lua_pushliteral(active, "Native call outside protected task execution");
      return lua_error(active);
    }
    // No C++ objects with destructors survive across lua_error/lua_yield below.
    // Lua's longjmp stays inside its protected resume/load boundary.
    try {
      binding->owner->capture(*binding, active);
    } catch (const std::exception& failure) {
      thread->set_error(failure.what());
    } catch (...) {
      thread->set_error("Unknown native exception");
    }
    if (thread->error.front() != '\0') {
      lua_pushstring(active, thread->error.data());
      return lua_error(active);
    }
    if (thread->pending) {
      return lua_yield(active, 0);
    }
    const auto result_count = thread->reply.values.size();
    if (result_count >
            static_cast<std::size_t>(std::numeric_limits<int>::max()) ||
        !lua_checkstack(active, static_cast<int>(result_count))) {
      lua_pushliteral(active, "Native result stack limit");
      return lua_error(active);
    }
    for (const auto& value : thread->reply.values) {
      push_value(active, value);
    }
    return static_cast<int>(thread->reply.values.size());
  }
  void load(std::string_view source) {
    require_not_pumping();
    if (primary.phase == RunState::running ||
        primary.phase == RunState::pending ||
        primary.phase == RunState::failed || children_live()) {
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
    primary.error = {};
    primary.reply = {};
    primary.pending = {};
    active_thread = &primary;
    const int status = lua_pcall(state.get(), 0, 0, 0);
    active_thread = nullptr;
    if (status != 0) {
      primary.pending = {};
      primary.reply = {};
      const auto* message = lua_type(state.get(), -1) == LUA_TSTRING
                                ? lua_tostring(state.get(), -1)
                                : nullptr;
      throw Error(ErrorCode::malformed,
                  message ? message : "Lua top-level failed");
    }
    primary.phase = RunState::idle;
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
  impl_->require_not_pumping();
  if (!native || impl_->primary.phase != RunState::idle ||
      impl_->children_live()) {
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
      lua_bw_protected(impl_->state.get(), Impl::protected_bind, &request),
      impl_->primary);
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
void Context::start(std::string_view function,
                    std::span<const Value> arguments) {
  impl_->start_thread(impl_->primary, function, arguments);
}
Value Context::result() const {
  impl_->require_not_pumping();
  const auto& thread = impl_->primary;
  if (thread.phase != RunState::complete || !thread.coroutine) {
    throw Error(ErrorCode::unsupported, "Lua result requires completed task");
  }
  return lua_gettop(thread.coroutine) == 0 ? Value{}
                                           : read_value(thread.coroutine, 1);
}
std::optional<double> Context::numeric_result() const {
  impl_->require_not_pumping();
  const auto& thread = impl_->primary;
  if (thread.phase != RunState::complete || !thread.coroutine) {
    throw Error(ErrorCode::unsupported, "Lua result requires completed task");
  }
  return lua_type(thread.coroutine, 1) == LUA_TNUMBER
             ? std::optional<double>(lua_tonumber(thread.coroutine, 1))
             : std::nullopt;
}
RunResult Context::pump() { return impl_->pump_thread(impl_->primary); }
void Context::reserve_coroutines(std::size_t capacity) {
  impl_->require_not_pumping();
  if (impl_->started || impl_->reserved) {
    throw Error(ErrorCode::unsupported,
                "Reserve Lua callbacks once before start");
  }
  impl_->children.resize(capacity);
  impl_->reserved = true;
}
CoroutineId Context::start_coroutine(const std::string& function) {
  impl_->require_not_pumping();
  const auto free = std::ranges::find_if(
      impl_->children,
      [](const Impl::Thread& child) { return child.id.value == 0; });
  if (free == impl_->children.end() ||
      impl_->next_id == std::numeric_limits<std::uint64_t>::max()) {
    throw Error(ErrorCode::unsupported, "Lua callback capacity exhausted");
  }
  free->id = {impl_->next_id++};
  try {
    impl_->start_thread(*free, function);
    return free->id;
  } catch (...) {
    impl_->retire(*free);
    throw;
  }
}
RunResult Context::pump(CoroutineId coroutine) {
  return impl_->pump_thread(impl_->find(coroutine));
}
void Context::retire_coroutine(CoroutineId coroutine) {
  impl_->require_not_pumping();
  impl_->retire(impl_->find(coroutine));
}
RunResult Context::Impl::pump_thread(Thread& thread) {
  require_not_pumping();
  struct Guard {
    bool* value;
    explicit Guard(bool& flag) : value(&flag) { *value = true; }
    Guard(const Guard&) = delete;
    Guard& operator=(const Guard&) = delete;
    Guard(Guard&&) = delete;
    Guard& operator=(Guard&&) = delete;
    ~Guard() { *value = false; }
  } guard(pumping);
  if (thread.phase != RunState::running && thread.phase != RunState::pending) {
    return {thread.phase, thread.error.data()};
  }
  try {
    for (std::size_t i = 0; i < kResumeBudget; ++i) {
      // A waiting bridge owns continuation; resume Lua only after it clears.
      if (thread.pending) {
        if (thread.pending()) {
          thread.phase = RunState::pending;
          return {thread.phase, {}};
        }
        thread.pending = {};
      }
      lua_sethook(thread.coroutine, budget, LUA_MASKCOUNT, kInstructionBudget);
      active_thread = &thread;
      const int argument_count = std::exchange(thread.initial_arguments, 0);
      const int status = lua_resume(thread.coroutine, argument_count);
      active_thread = nullptr;
      if (status != 0) {
        const auto* message = lua_type(thread.coroutine, -1) == LUA_TSTRING
                                  ? lua_tostring(thread.coroutine, -1)
                                  : nullptr;
        throw Error(ErrorCode::unsupported,
                    message ? message : "Lua execution failed");
      }
      if (!thread.pending) {
        thread.phase = RunState::complete;
        return {thread.phase, {}};
      }
    }
    // Bound host work without treating an unfinished coroutine as complete.
    thread.phase = RunState::running;
    return {thread.phase, "Host resume budget reached"};
  } catch (const std::exception& failure) {
    thread.pending = {};
    thread.reply = {};
    thread.set_error(failure.what());
    thread.phase = RunState::failed;
    return {thread.phase, thread.error.data()};
  } catch (...) {
    thread.pending = {};
    thread.reply = {};
    thread.set_error("Unknown pending callback exception");
    thread.phase = RunState::failed;
    return {thread.phase, thread.error.data()};
  }
}
Value Context::global(const std::string& name) const {
  impl_->require_not_pumping();
  // The protected callback only borrows this name during the C operation.
  Impl::GlobalRequest request{name.c_str()};
  impl_->checked(
      lua_bw_protected(impl_->state.get(), Impl::protected_global, &request),
      impl_->primary);
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
