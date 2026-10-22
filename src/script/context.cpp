// E2: owned Lua 5.0.2 context with immediate/waiting native bridges. Single
// thread, no reentrant calls. Binding callbacks own captures; borrowed services
// must outlive the context. Native argument spans last only through the call;
// a pending callback must copy any arguments it retains. Destroy cancels the
// retained poll and closes Lua. Moved-from contexts may only be
// destroyed/assigned. Script errors are terminal pump results; API misuse/load
// errors throw. No file/OS libraries are opened. Algorithm: load chunk, anchor
// coroutine, start named function, resume; on a bridge wait poll immediately,
// return while true, clear then resume when false. Cost follows executed Lua
// instructions; arguments/results copy strings once. Optimization: retained
// coroutine/poll avoids replaying completed script work. Safety budget: 100000
// instructions per resume and 64 bridge resumes per pump; these are host
// guards, not recovered game timing. Not an untrusted-code sandbox.
// Design/implementation MCP: I.30
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
#include <limits>
#include <map>
namespace bw::script {
namespace {
using resources::Error;
using resources::ErrorCode;
constexpr int kInstructionBudget = 100000;
constexpr std::size_t kResumeBudget = 64;
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
  std::unique_ptr<lua_State, Close> state{lua_open()};
  lua_State*
      coroutine{};  // Borrowed; anchored by thread_ref in parent's registry.
  int thread_ref{LUA_NOREF};
  std::map<std::string, Binding, std::less<>> bindings;
  Reply reply;
  std::function<bool()> pending;
  RunState phase{RunState::idle};
  std::string error;
  Impl() {
    if (!state) {
      throw std::bad_alloc();
    }
  }
  void capture(Binding& binding, lua_State* active) {
    std::vector<Value> args;
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
      binding->owner->error = failure.what();
    } catch (...) {
      binding->owner->error = "Unknown native exception";
    }
    if (!binding->owner->error.empty()) {
      lua_pushstring(active, binding->owner->error.c_str());
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
      const auto* message = lua_tostring(state.get(), -1);
      throw Error(ErrorCode::malformed, message ? message : "Lua load failed");
    }
    lua_sethook(state.get(), budget, LUA_MASKCOUNT, kInstructionBudget);
    if (lua_pcall(state.get(), 0, 0, 0) != 0) {
      const auto* message = lua_tostring(state.get(), -1);
      throw Error(ErrorCode::malformed,
                  message ? message : "Lua top-level failed");
    }
    phase = RunState::idle;
  }
};
Context::Context() : impl_(std::make_unique<Impl>()) {}
Context::~Context() = default;
Context::Context(Context&&) noexcept = default;
Context& Context::operator=(Context&&) noexcept = default;
void Context::bind(std::string name, Native native) {
  if (!native || impl_->phase != RunState::idle) {
    throw Error(ErrorCode::unsupported, "Bind before execution");
  }
  auto [it, added] = impl_->bindings.emplace(
      std::move(name), Impl::Binding{impl_.get(), std::move(native)});
  if (!added) {
    throw Error(ErrorCode::ambiguous, "Duplicate native binding");
  }
  lua_pushlightuserdata(impl_->state.get(), &it->second);
  lua_pushcclosure(impl_->state.get(), Impl::bridge, 1);
  lua_setglobal(impl_->state.get(), it->first.c_str());
}
void Context::load_binary(std::span<const std::uint8_t> bytes) {
  impl_->load(host_chunk(bytes));
}
void Context::load_source(std::string_view source) { impl_->load(source); }
void Context::start(const std::string& function) {
  if (impl_->phase == RunState::running || impl_->phase == RunState::pending ||
      impl_->phase == RunState::failed) {
    throw Error(ErrorCode::unsupported, "Context is not ready to start");
  }
  if (impl_->thread_ref != LUA_NOREF) {
    luaL_unref(impl_->state.get(), LUA_REGISTRYINDEX, impl_->thread_ref);
  }
  impl_->coroutine = lua_newthread(impl_->state.get());
  impl_->thread_ref = luaL_ref(impl_->state.get(), LUA_REGISTRYINDEX);
  lua_getglobal(impl_->coroutine, function.c_str());
  if (!lua_isfunction(impl_->coroutine, -1)) {
    throw Error(ErrorCode::not_found, "Lua function: " + function);
  }
  impl_->phase = RunState::running;
}
RunResult Context::pump() {
  if (impl_->phase != RunState::running && impl_->phase != RunState::pending) {
    return {impl_->phase, impl_->error};
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
        const auto* message = lua_tostring(impl_->coroutine, -1);
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
    impl_->error = failure.what();
    impl_->phase = RunState::failed;
    return {impl_->phase, impl_->error};
  } catch (...) {
    impl_->pending = {};
    impl_->error = "Unknown pending callback exception";
    impl_->phase = RunState::failed;
    return {impl_->phase, impl_->error};
  }
}
Value Context::global(const std::string& name) const {
  lua_getglobal(impl_->state.get(), name.c_str());
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
