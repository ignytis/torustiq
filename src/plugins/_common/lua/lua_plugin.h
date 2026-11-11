#ifndef TORUSTIQ_PLUGINS_COMMON_LUA_LUA_PLUGIN_H_
#define TORUSTIQ_PLUGINS_COMMON_LUA_LUA_PLUGIN_H_

#include <memory>

#include "plugins/_common/lua/lua_runtime.h"
#include "torustiq/sdk.h"

namespace torustiq::plugins {

// Instance state shared by the Lua input, transform and output plugins.
struct LuaPlugin {
    std::unique_ptr<LuaRuntime> runtime;
    // Payload kind of the messages this step emits (input, transform); the script's returned or
    // emitted messages are read accordingly. Fixed when the pipeline is built.
    uint32_t emit_kind = TORUSTIQ_PAYLOAD_BYTES;
    // input only: the emit callback of the current run() call
    torustiq_emit_fn emit = nullptr;
    void* emit_user_data = nullptr;
};

// Creates a LuaPlugin from the plugin parameters (see LuaRuntime) and checks that the script
// defines the global function `required_function`. `kind` (e.g. "lua input") prefixes error
// messages, which are printed to stderr. Returns TORUSTIQ_OK or TORUSTIQ_ERROR; never throws.
int32_t CreateLuaPlugin(const torustiq_config* config,
                        const char* kind,
                        const char* required_function,
                        void** out_handle);

// Reads the optional parameter `payload` ("bytes" or "value"): the payload kind this step emits.
// Without it, `default_kind` is used. Returns TORUSTIQ_ERROR (after printing why, prefixed with
// `kind`) for any other value.
int32_t ParseLuaPayloadKind(const torustiq_config* config,
                            const char* kind,
                            uint32_t default_kind,
                            uint32_t* out_kind);

// The `destroy` function of all Lua plugins.
void DestroyLuaPlugin(void* handle);

} // namespace torustiq::plugins

#endif // TORUSTIQ_PLUGINS_COMMON_LUA_LUA_PLUGIN_H_
