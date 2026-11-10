#include <map>

#include <gtest/gtest.h>

#include "core/paths.h"

namespace torustiq {
namespace {

EnvLookup Env(std::map<std::string, std::string> vars) {
    return [vars](const std::string& name) -> std::optional<std::string> {
        auto it = vars.find(name);
        if (it == vars.end()) {
            return std::nullopt;
        }
        return it->second;
    };
}

} // namespace

TEST(PathsTest, HomeFromEnvironmentWins) {
    EXPECT_EQ(ResolveHome("/dev/home", Env({{"TORUSTIQ_HOME", "/env/home"}})), "/env/home");
}

TEST(PathsTest, HomeFallsBackToCompiledDefault) {
    EXPECT_EQ(ResolveHome("/dev/home", Env({{"HOME", "/home/u"}})), "/dev/home");
    EXPECT_EQ(ResolveHome("/dev/home", Env({{"TORUSTIQ_HOME", ""}})), "/dev/home");
}

TEST(PathsTest, HomeFallsBackToUserHome) {
    EXPECT_EQ(ResolveHome("", Env({{"HOME", "/home/u"}})), "/home/u/.torustiq");
    EXPECT_EQ(ResolveHome("", Env({{"USERPROFILE", "/users/u"}})), "/users/u/.torustiq");
}

TEST(PathsTest, PluginsDirDefaultsToHome) {
    EXPECT_EQ(ResolvePluginsDir("/dev/home", Env({})), "/dev/home/plugins");
    EXPECT_EQ(ResolvePluginsDir("", Env({{"TORUSTIQ_HOME", "/h"}})), "/h/plugins");
}

TEST(PathsTest, PluginsDirEnvironmentOverridesHome) {
    EXPECT_EQ(ResolvePluginsDir("/dev/home",
                                Env({{"TORUSTIQ_HOME", "/h"}, {"TORUSTIQ_PLUGINS_DIR", "/p"}})),
              "/p");
}

} // namespace torustiq
