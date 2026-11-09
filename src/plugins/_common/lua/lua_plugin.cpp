#include "plugins/_common/lua/lua_plugin.h"

#include <iostream>

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

void DestroyLuaPlugin(void* handle) {
    delete static_cast<LuaPlugin*>(handle);
}

} // namespace torustiq::plugins
