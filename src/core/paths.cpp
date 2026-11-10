#include "core/paths.h"

#include <cstdlib>
#include <filesystem>

namespace torustiq {
namespace {

std::optional<std::string> NonEmpty(const EnvLookup& env, const std::string& name) {
    std::optional<std::string> value = env(name);
    if (value.has_value() && value->empty()) {
        return std::nullopt;
    }
    return value;
}

} // namespace

std::optional<std::string> ProcessEnv(const std::string& name) {
    const char* value = std::getenv(name.c_str());
    if (value == nullptr) {
        return std::nullopt;
    }
    return std::string(value);
}

std::string ResolveHome(const std::string& compiled_default, const EnvLookup& env) {
    if (auto home = NonEmpty(env, "TORUSTIQ_HOME")) {
        return *home;
    }
    if (!compiled_default.empty()) {
        return compiled_default;
    }
    std::optional<std::string> user_home = NonEmpty(env, "HOME");
    if (!user_home.has_value()) {
        user_home = NonEmpty(env, "USERPROFILE");
    }
    return ((std::filesystem::path(user_home.value_or(".")) / ".torustiq")).string();
}

std::string ResolvePluginsDir(const std::string& compiled_default, const EnvLookup& env) {
    if (auto dir = NonEmpty(env, "TORUSTIQ_PLUGINS_DIR")) {
        return *dir;
    }
    return (std::filesystem::path(ResolveHome(compiled_default, env)) / "plugins").string();
}

} // namespace torustiq
