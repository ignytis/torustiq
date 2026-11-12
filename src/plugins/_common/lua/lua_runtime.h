#ifndef TORUSTIQ_PLUGINS_COMMON_LUA_LUA_RUNTIME_H_
#define TORUSTIQ_PLUGINS_COMMON_LUA_LUA_RUNTIME_H_

#include <atomic>
#include <cstdint>
#include <deque>
#include <string>
#include <utility>
#include <vector>

#include "torustiq/sdk.h"

extern "C" {
#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>
}

namespace torustiq::plugins {

// An owning payload value as handled by Lua plugins; `kind` is a torustiq_value_kind.
struct LuaValue {
    uint32_t kind = TORUSTIQ_VALUE_NULL;
    bool b = false;
    int64_t i = 0;
    double d = 0;
    std::string text;                                      // STRING and BYTES
    std::vector<LuaValue> items;                           // ARRAY
    std::vector<std::pair<std::string, LuaValue>> entries; // MAP
};

// A message as handled by Lua plugins. The default payload is empty bytes.
struct LuaMessage {
    LuaMessage() {
        payload.kind = TORUSTIQ_VALUE_BYTES;
    }
    LuaValue payload;
    std::vector<std::pair<std::string, std::string>> headers;
};

// Exposes a LuaMessage as an SDK message. The LuaMessage must outlive the view.
class LuaMessageView {
  public:
    explicit LuaMessageView(const LuaMessage& message);
    LuaMessageView(const LuaMessageView&) = delete;
    LuaMessageView& operator=(const LuaMessageView&) = delete;

    const torustiq_message& get() const {
        return message_;
    }

  private:
    torustiq_value Build(const LuaValue& value);

    std::vector<torustiq_header> headers_;
    std::deque<std::vector<torustiq_value>> arrays_;
    std::deque<std::vector<torustiq_map_entry>> maps_;
    torustiq_message message_{};
};

// A sandboxed Lua interpreter configured from plugin parameters:
//   script            inline Lua code            (exactly one of `script` / `file` is required)
//   file              path to a Lua file
//   max_instructions  instruction budget per call (default 10000000; 0 = unlimited). Rounded up
//                     to a multiple of kHookStep. Script code that exceeds it fails with an error.
//
// Only the base (minus dofile/loadfile/load), string, table, math, utf8 and coroutine libraries
// are available: no io, os, package or debug. The plugin parameters are exposed to the script as
// the global table `config`. The global `null` is a sentinel for a null member of a structured
// payload (a nil table member is simply absent).
//
// Not thread-safe, except RequestStop(). Lua errors are never allowed to propagate as C++
// exceptions or longjmp across C++ frames holding objects with destructors.
class LuaRuntime {
  public:
    static constexpr int64_t kDefaultMaxInstructions = 10'000'000;
    static constexpr int kHookStep = 1000;

    // Compiles and runs the script's top-level code. Throws std::runtime_error on failure.
    LuaRuntime(const torustiq_config* config, const std::string& kind);
    ~LuaRuntime();
    LuaRuntime(const LuaRuntime&) = delete;
    LuaRuntime& operator=(const LuaRuntime&) = delete;

    lua_State* state() const {
        return state_;
    }

    // Returns the runtime owning `state` (or one of its coroutines).
    static LuaRuntime* FromState(lua_State* state);

    // Makes running script code fail with "stopped by host". Thread-safe.
    void RequestStop() {
        stop_.store(true);
    }
    bool stop_requested() const {
        return stop_.load();
    }

    void ResetBudget() {
        executed_ = 0;
    }

    // Pushes global function `name` and returns true, or pushes nothing and returns false.
    bool PushGlobalFunction(const char* name);

    // Calls the function on the stack (below `nargs` arguments) with a fresh instruction budget.
    // On failure pops the error into `error` and returns false.
    bool Call(int nargs, int nresults, std::string& error);

    // Pushes {body = <string>, headers = {...}} for a bytes payload, {text = <string>, headers =
    // {...}} for a text one, or {value = <table/scalar>, headers = {...}} for a structured one.
    void PushMessage(const torustiq_message& message);

    // Reads a message whose payload has kind `payload_kind` (TORUSTIQ_PAYLOAD_BYTES: a string/
    // number (body only) or a table {body=, headers=}; TORUSTIQ_PAYLOAD_TEXT: the same with
    // `text=`; TORUSTIQ_PAYLOAD_VALUE: a table {value=, headers=}). Uses raw accesses only, so it
    // never runs Lua code.
    static bool ReadMessage(
        lua_State* state, int index, uint32_t payload_kind, LuaMessage& out, std::string& error);

    // Reads a function result: nil/false -> no messages; a table with array part -> one message
    // per element; anything else -> a single message.
    static bool ReadMessages(lua_State* state,
                             int index,
                             uint32_t payload_kind,
                             std::vector<LuaMessage>& out,
                             std::string& error);

  private:
    void PushValue(const torustiq_value& value, int depth);
    static void Hook(lua_State* state, lua_Debug* debug);
    const char* OnHook(); // returns an error message, or nullptr to continue

    lua_State* state_ = nullptr;
    int64_t max_instructions_ = kDefaultMaxInstructions;
    int64_t executed_ = 0;
    std::atomic<bool> stop_{false};
};

} // namespace torustiq::plugins

#endif // TORUSTIQ_PLUGINS_COMMON_LUA_LUA_RUNTIME_H_
