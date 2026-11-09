#include "plugins/builtin_plugins.h"

#include <stdexcept>

#include "plugins/directory_input.h"
#include "plugins/stdin_input.h"
#include "plugins/stdout_output.h"

namespace torustiq {

void RegisterBuiltinPlugins(PluginRegistry& registry) {
    const torustiq_registrar registrar = registry.registrar();
    for (torustiq_plugin_register_fn fn : {torustiq_register_stdin_input,
                                           torustiq_register_directory_input,
                                           torustiq_register_stdout_output}) {
        if (fn(&registrar) != TORUSTIQ_OK) {
            throw std::runtime_error("failed to register a built-in plugin");
        }
    }
}

} // namespace torustiq
