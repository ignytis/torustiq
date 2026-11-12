#include "plugins/builtin_plugins.h"

#include <stdexcept>

#include "plugins/inputs/directory_input.h"
#include "plugins/inputs/stdin_input.h"
#include "plugins/outputs/stdout_output.h"
#include "plugins/transforms/json_transforms.h"
#include "plugins/transforms/text_transforms.h"
#ifdef TORUSTIQ_WITH_LUA
#include "plugins/inputs/lua_input.h"
#include "plugins/outputs/lua_output.h"
#include "plugins/transforms/lua_transform.h"
#endif

namespace torustiq {

void RegisterBuiltinPlugins(PluginRegistry& registry) {
    const torustiq_registrar registrar = registry.registrar();
    for (torustiq_plugin_register_fn fn : {
             torustiq_register_stdin_input,
             torustiq_register_directory_input,
             torustiq_register_stdout_output,
             torustiq_register_json_transforms,
             torustiq_register_text_transforms,
#ifdef TORUSTIQ_WITH_LUA
             torustiq_register_lua_input,
             torustiq_register_lua_transform,
             torustiq_register_lua_output,
#endif
         }) {
        if (fn(&registrar) != TORUSTIQ_OK) {
            throw std::runtime_error("failed to register a built-in plugin");
        }
    }
}

} // namespace torustiq
