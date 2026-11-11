#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

#include <gtest/gtest.h>

#include "core/plugin_loader.h"

namespace torustiq {
namespace {

namespace fs = std::filesystem;

class PluginLoaderTest : public testing::Test {
  protected:
    void SetUp() override {
        dir_ = fs::temp_directory_path() /
               ("torustiq_loader_test_" +
                std::to_string(::testing::UnitTest::GetInstance()->random_seed()) + "_" +
                testing::UnitTest::GetInstance()->current_test_info()->name());
        fs::remove_all(dir_);
        fs::create_directories(dir_ / "plugins");
        log_ = dir_ / "log.txt";
        setenv("FAKE_PLUGIN_LOG", log_.string().c_str(), 1);
    }
    void TearDown() override {
        unsetenv("FAKE_PLUGIN_LOG");
        fs::remove_all(dir_);
    }

    std::string PluginsDir() const {
        return (dir_ / "plugins").string();
    }
    void Link(const std::string& lib, const std::string& name) {
        fs::create_symlink(lib, dir_ / "plugins" / name);
    }
    std::string Log() const {
        std::ifstream in(log_);
        std::stringstream ss;
        ss << in.rdbuf();
        return ss.str();
    }

    fs::path dir_;
    fs::path log_;
};

PipelineConfig ConfigWithOutputs(const std::vector<std::string>& types) {
    PipelineConfig config;
    for (const std::string& type : types) {
        ComponentConfig c;
        c.id = "out_" + std::to_string(config.outputs.size());
        c.type = type;
        config.outputs.push_back(c);
    }
    return config;
}

} // namespace

TEST_F(PluginLoaderTest, ScanReadsIdsWithoutInitialising) {
    Link(FAKE_PLUGIN_A, "libfake_a.so");
    Link(FAKE_PLUGIN_B, "libfake_b.so");
    PluginLoader loader;
    loader.Scan(PluginsDir());
    EXPECT_TRUE(loader.HasPlugin("fake_a"));
    EXPECT_TRUE(loader.HasPlugin("fake_b"));
    EXPECT_FALSE(loader.IsLoaded("fake_a"));
    EXPECT_EQ(Log(), "");
}

TEST_F(PluginLoaderTest, MissingDirectoryIsNotAnError) {
    PluginLoader loader;
    loader.Scan((dir_ / "nope").string());
    EXPECT_FALSE(loader.HasPlugin("fake_a"));
}

TEST_F(PluginLoaderTest, SkipsWrongAbiAndNonLibraries) {
    Link(FAKE_PLUGIN_BAD_ABI, "libfake_bad.so");
    std::ofstream(dir_ / "plugins" / "libgarbage.so") << "not a library";
    std::ofstream(dir_ / "plugins" / "readme.txt") << "x";
    PluginLoader loader;
    testing::internal::CaptureStderr();
    loader.Scan(PluginsDir());
    testing::internal::GetCapturedStderr();
    EXPECT_FALSE(loader.HasPlugin("fake_bad"));
}

TEST_F(PluginLoaderTest, LoadInitialisesOnceAndRegistersComponent) {
    Link(FAKE_PLUGIN_A, "libfake_a.so");
    PluginLoader loader;
    PluginRegistry registry;
    loader.Scan(PluginsDir());
    loader.Load("fake_a", registry);
    loader.Load("fake_a", registry); // second call is a no-op
    EXPECT_TRUE(loader.IsLoaded("fake_a"));
    EXPECT_EQ(Log(), "init fake_a\n");
    ASSERT_NE(registry.FindOutput("fake_a"), nullptr);

    // Step-level init: every `create` call gives a distinct handle.
    const torustiq_output_vtable* vtable = registry.FindOutput("fake_a");
    const torustiq_config config{nullptr, 0};
    const torustiq_payload_spec input{TORUSTIQ_PAYLOAD_BYTES};
    void* first = nullptr;
    void* second = nullptr;
    ASSERT_EQ(vtable->create(&config, &input, &first), TORUSTIQ_OK);
    ASSERT_EQ(vtable->create(&config, &input, &second), TORUSTIQ_OK);
    EXPECT_NE(first, second);
    vtable->destroy(first);
    vtable->destroy(second);
}

TEST_F(PluginLoaderTest, UnknownPluginThrows) {
    PluginLoader loader;
    PluginRegistry registry;
    EXPECT_THROW(loader.Load("nope", registry), std::runtime_error);
}

TEST_F(PluginLoaderTest, ShutdownRunsOnDestruction) {
    Link(FAKE_PLUGIN_A, "libfake_a.so");
    {
        PluginLoader loader;
        PluginRegistry registry;
        loader.Scan(PluginsDir());
        loader.Load("fake_a", registry);
    }
    EXPECT_EQ(Log(), "init fake_a\nshutdown fake_a\n");
}

TEST_F(PluginLoaderTest, OnlyReferencedPluginsAreInitialised) {
    Link(FAKE_PLUGIN_A, "libfake_a.so");
    Link(FAKE_PLUGIN_B, "libfake_b.so");
    PluginLoader loader;
    PluginRegistry registry;
    loader.Scan(PluginsDir());
    // fake_a is used by two steps; fake_b is not referenced; "stdout" is not a plugin.
    LoadReferencedPlugins(ConfigWithOutputs({"fake_a", "fake_a", "stdout"}), loader, registry);
    EXPECT_TRUE(loader.IsLoaded("fake_a"));
    EXPECT_FALSE(loader.IsLoaded("fake_b"));
    EXPECT_EQ(Log(), "init fake_a\n");
}

} // namespace torustiq
