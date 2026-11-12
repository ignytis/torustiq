#include "plugins/_common/lua/lua_plugin.h"

#include <iostream>
#include <string_view>

namespace torustiq::plugins {

int32_t CreateLuaPlugin(const torustiq_config* config,
                        const char* kind,
                        const char* required_function,
                        void** out_handle) {
    try {
        auto plugin = std::make_unique<LuaPlugin>();
        plugin->runtime = std::make_unique<LuaRuntime>(config, kind);
        if (!plugin->runtime->PushGlobalFunction(required_function)) {
            std::cerr << kind << ": the script must define a global function '" << required_function
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

int32_t ParseLuaPayloadKind(const torustiq_config* config,
                            const char* kind,
                            uint32_t default_kind,
                            uint32_t* out_kind) {
    const char* value = torustiq_config_get(config, "payload");
    if (value == nullptr) {
        *out_kind = default_kind;
    } else if (std::string_view(value) == "bytes") {
        *out_kind = TORUSTIQ_PAYLOAD_BYTES;
    } else if (std::string_view(value) == "value") {
        *out_kind = TORUSTIQ_PAYLOAD_VALUE;
    } else if (std::string_view(value) == "text") {
        *out_kind = TORUSTIQ_PAYLOAD_TEXT;
    } else {
        std::cerr << kind << ": 'payload' must be 'bytes', 'text' or 'value', got '" << value
                  << "'\n";
        return TORUSTIQ_ERROR;
    }
    return TORUSTIQ_OK;
}

void DestroyLuaPlugin(void* handle) {
    delete static_cast<LuaPlugin*>(handle);
}

} // namespace torustiq::plugins
