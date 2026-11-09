#include "plugins/builtin_plugins.h"

#include <stdexcept>

#include "plugins/directory_input.h"
#ifdef TORUSTIQ_WITH_LUA
#include "plugins/lua_plugins.h"
#endif
#include "plugins/stdin_input.h"
#include "plugins/stdout_output.h"

namespace torustiq {

void RegisterBuiltinPlugins(PluginRegistry& registry) {
    const torustiq_registrar registrar = registry.registrar();
    for (torustiq_plugin_register_fn fn : {
             torustiq_register_stdin_input,
             torustiq_register_directory_input,
             torustiq_register_stdout_output,
#ifdef TORUSTIQ_WITH_LUA
             torustiq_register_lua_plugins,
#endif
         }) {
        if (fn(&registrar) != TORUSTIQ_OK) {
            throw std::runtime_error("failed to register a built-in plugin");
        }
    }
}

} // namespace torustiq
