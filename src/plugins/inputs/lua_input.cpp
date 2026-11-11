#include "plugins/inputs/lua_input.h"

#include <chrono>
#include <iostream>
#include <string>
#include <thread>

#include "plugins/_common/lua/lua_plugin.h"

namespace torustiq::plugins {
namespace {

// Lua: emit(msg) -> true to continue, false if no more data is wanted.
int LuaEmit(lua_State* state) {
    auto* plugin = static_cast<LuaPlugin*>(lua_touserdata(state, lua_upvalueindex(1)));
    enum { kContinue, kStop, kRaise } outcome = kContinue;
    { // C++ objects must be gone before lua_error (it longjmps)
        LuaMessage message;
        std::string error;
        if (!LuaRuntime::ReadMessage(state, 1, plugin->emit_kind, message, error)) {
            lua_pushlstring(state, error.data(), error.size());
            outcome = kRaise;
        } else {
            LuaMessageView view(message);
            const int32_t status = plugin->emit(plugin->emit_user_data, &view.get());
            if (status == TORUSTIQ_STOPPED) {
                outcome = kStop;
            } else if (status != TORUSTIQ_OK) {
                lua_pushliteral(state, "emit failed");
                outcome = kRaise;
            }
        }
    }
    if (outcome == kRaise) {
        return lua_error(state);
    }
    plugin->runtime->ResetBudget(); // the budget applies between two emit() calls
    lua_pushboolean(state, outcome == kContinue && !plugin->runtime->stop_requested());
    return 1;
}

// Lua: sleep(seconds); fails with "stopped by host" when the host stops the input.
int LuaSleep(lua_State* state) {
    const double seconds = luaL_checknumber(state, 1);
    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                              std::chrono::duration<double>(seconds < 0 ? 0 : seconds));
    LuaRuntime* runtime = LuaRuntime::FromState(state);
    while (std::chrono::steady_clock::now() < deadline) {
        if (runtime->stop_requested()) {
            return luaL_error(state, "stopped by host");
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return 0;
}

int32_t
Create(const torustiq_config* config, torustiq_payload_spec* out_output, void** out_handle) {
    uint32_t kind = TORUSTIQ_PAYLOAD_BYTES;
    if (ParseLuaPayloadKind(config, "lua input", TORUSTIQ_PAYLOAD_BYTES, &kind) != TORUSTIQ_OK) {
        return TORUSTIQ_ERROR;
    }
    const int32_t status = CreateLuaPlugin(config, "lua input", "run", out_handle);
    if (status == TORUSTIQ_OK) {
        auto* plugin = static_cast<LuaPlugin*>(*out_handle);
        plugin->emit_kind = kind;
        out_output->kind = kind;
        lua_State* state = plugin->runtime->state();
        lua_pushcfunction(state, LuaSleep);
        lua_setglobal(state, "sleep");
    }
    return status;
}

int32_t Run(void* handle, torustiq_emit_fn emit, void* user_data) {
    auto* plugin = static_cast<LuaPlugin*>(handle);
    LuaRuntime& runtime = *plugin->runtime;
    plugin->emit = emit;
    plugin->emit_user_data = user_data;
    try {
        if (runtime.stop_requested() || !runtime.PushGlobalFunction("run")) {
            return TORUSTIQ_OK;
        }
        lua_pushlightuserdata(runtime.state(), plugin);
        lua_pushcclosure(runtime.state(), LuaEmit, 1);
        std::string error;
        if (!runtime.Call(1, 0, error)) {
            if (runtime.stop_requested()) {
                return TORUSTIQ_OK;
            }
            std::cerr << "lua input: " << error << "\n";
            return TORUSTIQ_ERROR;
        }
        return TORUSTIQ_OK;
    } catch (const std::exception& e) {
        std::cerr << "lua input: " << e.what() << "\n";
        return TORUSTIQ_ERROR;
    }
}

void Stop(void* handle) {
    static_cast<LuaPlugin*>(handle)->runtime->RequestStop();
}

} // namespace
} // namespace torustiq::plugins

extern "C" int32_t torustiq_register_lua_input(const torustiq_registrar* registrar) {
    using namespace torustiq::plugins;
    torustiq_input_vtable vtable{};
    vtable.abi_version = TORUSTIQ_SDK_ABI_VERSION;
    vtable.name = "lua";
    vtable.create = Create;
    vtable.run = Run;
    vtable.stop = Stop;
    vtable.destroy = DestroyLuaPlugin;
    return registrar->register_input(registrar->context, &vtable);
}
