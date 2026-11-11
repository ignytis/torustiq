#include "plugins/transforms/lua_transform.h"

#include <iostream>
#include <string>
#include <vector>

#include "plugins/_common/lua/lua_plugin.h"

namespace torustiq::plugins {
namespace {

// The scripts handle both payload kinds; the emitted kind is the input kind unless `payload`
// says otherwise (e.g. a script that parses its bytes input into a structured value).
int32_t Create(const torustiq_config* config,
               const torustiq_payload_spec* input,
               torustiq_payload_spec* out_output,
               void** out_handle) {
    uint32_t kind = input->kind;
    if (ParseLuaPayloadKind(config, "lua transform", input->kind, &kind) != TORUSTIQ_OK) {
        return TORUSTIQ_ERROR;
    }
    const int32_t status = CreateLuaPlugin(config, "lua transform", "process", out_handle);
    if (status == TORUSTIQ_OK) {
        static_cast<LuaPlugin*>(*out_handle)->emit_kind = kind;
        out_output->kind = kind;
    }
    return status;
}

int32_t
Process(void* handle, const torustiq_message* message, torustiq_emit_fn emit, void* user_data) {
    auto* plugin = static_cast<LuaPlugin*>(handle);
    LuaRuntime& runtime = *plugin->runtime;
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
        const bool ok = LuaRuntime::ReadMessages(state, -1, plugin->emit_kind, results, error);
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

} // namespace
} // namespace torustiq::plugins

extern "C" int32_t torustiq_register_lua_transform(const torustiq_registrar* registrar) {
    using namespace torustiq::plugins;
    torustiq_transform_vtable vtable{};
    vtable.abi_version = TORUSTIQ_SDK_ABI_VERSION;
    vtable.name = "lua";
    vtable.create = Create;
    vtable.process = Process;
    vtable.destroy = DestroyLuaPlugin;
    return registrar->register_transform(registrar->context, &vtable);
}
