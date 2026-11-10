#ifndef TORUSTIQ_CORE_PATHS_H_
#define TORUSTIQ_CORE_PATHS_H_

#include <functional>
#include <optional>
#include <string>

namespace torustiq {

// Looks up an environment variable; std::nullopt when unset. Injectable for tests.
using EnvLookup = std::function<std::optional<std::string>(const std::string& name)>;

// The real process environment.
std::optional<std::string> ProcessEnv(const std::string& name);

// TORUSTIQ_HOME: the environment variable, else `compiled_default` (set at build time; in
// development mode it is <project>/dev/torustiq_home), else <HOME>/.torustiq.
// Empty variables count as unset.
std::string ResolveHome(const std::string& compiled_default, const EnvLookup& env);

// Directory scanned for plugin libraries: TORUSTIQ_PLUGINS_DIR, which defaults to
// <TORUSTIQ_HOME>/plugins.
std::string ResolvePluginsDir(const std::string& compiled_default, const EnvLookup& env);

} // namespace torustiq

#endif // TORUSTIQ_CORE_PATHS_H_
