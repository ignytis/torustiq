#include "plugins/lua_plugins.h"

#include <chrono>
#include <iostream>
#include <memory>
#include <string>
#include <thread>

#include "plugins/lua_runtime.h"

namespace torustiq::plugins {
namespace {

struct LuaPlugin {
    std::unique_ptr<LuaRuntime> runtime;
    // input only: the emit callback of the current run() call
    torustiq_emit_fn emit = nullptr;
    void* emit_user_data = nullptr;
};

// Creates the plugin; `required` is the global function the script must define.
int32_t CreatePlugin(const torustiq_config* config,
                     const char* kind,
                     const char* required,
                     void** out_handle) {
    try {
        auto plugin = std::make_unique<LuaPlugin>();
        plugin->runtime = std::make_unique<LuaRuntime>(config, kind);
        if (!plugin->runtime->PushGlobalFunction(required)) {
            std::cerr << kind << ": the script must define a global function '" << required
                      << "'\n";
            return TORUSTIQ_ERROR;
        }
        lua_pop(plugin->runtime->state(), 1);
        *out_handle = plugin.release();
        return TORUSTIQ_OK;
    } catch (const std::exception& e) {
        std::cerr << e.what() << "\n";
        return TORUSTIQ_ERROR;
    }
}

void DestroyPlugin(void* handle) {
    delete static_cast<LuaPlugin*>(handle);
}

// ---- input ----

// Lua: emit(msg) -> true to continue, false if no more data is wanted.
int LuaEmit(lua_State* state) {
    auto* plugin = static_cast<LuaPlugin*>(lua_touserdata(state, lua_upvalueindex(1)));
    enum { kContinue, kStop, kRaise } outcome = kContinue;
    { // C++ objects must be gone before lua_error (it longjmps)
        LuaMessage message;
        std::string error;
        if (!LuaRuntime::ReadMessage(state, 1, message, error)) {
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

// Lua: sleep(seconds); wakes up early (with an error) when the host stops the input.
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

int32_t InputCreate(const torustiq_config* config, void** out_handle) {
    int32_t status = CreatePlugin(config, "lua input", "run", out_handle);
    if (status == TORUSTIQ_OK) {
        auto* plugin = static_cast<LuaPlugin*>(*out_handle);
        lua_pushcfunction(plugin->runtime->state(), LuaSleep);
        lua_setglobal(plugin->runtime->state(), "sleep");
    }
    return status;
}

int32_t InputRun(void* handle, torustiq_emit_fn emit, void* user_data) {
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

void InputStop(void* handle) {
    static_cast<LuaPlugin*>(handle)->runtime->RequestStop();
}

// ---- transform ----

int32_t TransformCreate(const torustiq_config* config, void** out_handle) {
    return CreatePlugin(config, "lua transform", "process", out_handle);
}

int32_t TransformProcess(void* handle,
                         const torustiq_message* message,
                         torustiq_emit_fn emit,
                         void* user_data) {
    LuaRuntime& runtime = *static_cast<LuaPlugin*>(handle)->runtime;
    try {
        lua_State* state = runtime.state();
        std::string error;
        if (!runtime.PushGlobalFunction("process")) {
            return TORUSTIQ_ERROR;
        }
        runtime.PushMessage(*message);
        if (!runtime.Call(1, 1, error)) {
            std::cerr << "lua transform: " << error << "\n";
            return TORUSTIQ_ERROR;
        }
        std::vector<LuaMessage> results;
        const bool ok = LuaRuntime::ReadMessages(state, -1, results, error);
        lua_pop(state, 1);
        if (!ok) {
            std::cerr << "lua transform: invalid result: " << error << "\n";
            return TORUSTIQ_ERROR;
        }
        for (const LuaMessage& result : results) {
            LuaMessageView view(result);
            const int32_t status = emit(user_data, &view.get());
            if (status != TORUSTIQ_OK) {
                return status;
            }
        }
        return TORUSTIQ_OK;
    } catch (const std::exception& e) {
        std::cerr << "lua transform: " << e.what() << "\n";
        return TORUSTIQ_ERROR;
    }
}

// ---- output ----

int32_t OutputCreate(const torustiq_config* config, void** out_handle) {
    return CreatePlugin(config, "lua output", "write", out_handle);
}

int32_t OutputWrite(void* handle, const torustiq_message* message) {
    LuaRuntime& runtime = *static_cast<LuaPlugin*>(handle)->runtime;
    try {
        std::string error;
        if (!runtime.PushGlobalFunction("write")) {
            return TORUSTIQ_ERROR;
        }
        runtime.PushMessage(*message);
        if (!runtime.Call(1, 0, error)) {
            std::cerr << "lua output: " << error << "\n";
            return TORUSTIQ_ERROR;
        }
        return TORUSTIQ_OK;
    } catch (const std::exception& e) {
        std::cerr << "lua output: " << e.what() << "\n";
        return TORUSTIQ_ERROR;
    }
}

int32_t OutputFlush(void* handle) {
    LuaRuntime& runtime = *static_cast<LuaPlugin*>(handle)->runtime;
    try {
        if (!runtime.PushGlobalFunction("flush")) {
            return TORUSTIQ_OK; // flush is optional
        }
        std::string error;
        if (!runtime.Call(0, 0, error)) {
            std::cerr << "lua output: " << error << "\n";
            return TORUSTIQ_ERROR;
        }
        return TORUSTIQ_OK;
    } catch (const std::exception& e) {
        std::cerr << "lua output: " << e.what() << "\n";
        return TORUSTIQ_ERROR;
    }
}

} // namespace
} // namespace torustiq::plugins

extern "C" int32_t torustiq_register_lua_plugins(const torustiq_registrar* registrar) {
    using namespace torustiq::plugins;

    torustiq_input_vtable input{};
    input.abi_version = TORUSTIQ_SDK_ABI_VERSION;
    input.name = "lua";
    input.create = InputCreate;
    input.run = InputRun;
    input.stop = InputStop;
    input.destroy = DestroyPlugin;

    torustiq_transform_vtable transform{};
    transform.abi_version = TORUSTIQ_SDK_ABI_VERSION;
    transform.name = "lua";
    transform.create = TransformCreate;
    transform.process = TransformProcess;
    transform.destroy = DestroyPlugin;

    torustiq_output_vtable output{};
    output.abi_version = TORUSTIQ_SDK_ABI_VERSION;
    output.name = "lua";
    output.create = OutputCreate;
    output.write = OutputWrite;
    output.flush = OutputFlush;
    output.destroy = DestroyPlugin;

    if (registrar->register_input(registrar->context, &input) != TORUSTIQ_OK ||
        registrar->register_transform(registrar->context, &transform) != TORUSTIQ_OK ||
        registrar->register_output(registrar->context, &output) != TORUSTIQ_OK) {
        return TORUSTIQ_ERROR;
    }
    return TORUSTIQ_OK;
}
