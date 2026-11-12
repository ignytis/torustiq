#include "plugins/outputs/lua_output.h"

#include <iostream>
#include <string>

#include "plugins/_common/lua/lua_plugin.h"

namespace torustiq::plugins {
namespace {

// Accepts all payload kinds: the script sees `msg.body` (bytes), `msg.text` (text) or
// `msg.value` (structured).
int32_t Create(const torustiq_config* config, const torustiq_payload_spec*, void** out_handle) {
    return CreateLuaPlugin(config, "lua output", "write", out_handle);
}

int32_t Write(void* handle, const torustiq_message* message) {
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

int32_t Flush(void* handle) {
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

extern "C" int32_t torustiq_register_lua_output(const torustiq_registrar* registrar) {
    using namespace torustiq::plugins;
    torustiq_output_vtable vtable{};
    vtable.abi_version = TORUSTIQ_SDK_ABI_VERSION;
    vtable.name = "lua";
    vtable.create = Create;
    vtable.write = Write;
    vtable.flush = Flush;
    vtable.destroy = DestroyLuaPlugin;
    return registrar->register_output(registrar->context, &vtable);
}
