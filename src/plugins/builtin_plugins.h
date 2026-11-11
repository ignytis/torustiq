#ifndef TORUSTIQ_PLUGINS_BUILTIN_PLUGINS_H_
#define TORUSTIQ_PLUGINS_BUILTIN_PLUGINS_H_

#include "core/plugin_registry.h"

namespace torustiq {

// Registers all built-in plugins. Throws std::runtime_error on failure.
void RegisterBuiltinPlugins(PluginRegistry& registry);

} // namespace torustiq

#endif // TORUSTIQ_PLUGINS_BUILTIN_PLUGINS_H_
