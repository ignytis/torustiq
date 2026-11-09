#include <string>

#include <gtest/gtest.h>

#include "core/pipeline.h"
#include "core/pipeline_config.h"
#include "plugins/builtin_plugins.h"

namespace torustiq {

// Runs examples/lua_pipeline.yaml (input, transform and output are all Lua scripts).
TEST(LuaPipelineTest, ExamplePipelineRunsEndToEnd) {
    const PipelineConfig config =
        LoadPipelineConfig(std::string(TORUSTIQ_EXAMPLES_DIR) + "/lua_pipeline.yaml");
    PluginRegistry registry;
    RegisterBuiltinPlugins(registry);
    Pipeline pipeline(config, registry);

    testing::internal::CaptureStdout();
    const int result = pipeline.Run();
    const std::string printed = testing::internal::GetCapturedStdout();

    EXPECT_EQ(result, 0) << pipeline.error();
    EXPECT_EQ(printed,
              "#1 MESSAGE 1 [processed=yes, seq=1]\n"
              "#2 MESSAGE 3 [processed=yes, seq=3]\n"
              "#3 MESSAGE 5 [processed=yes, seq=5]\n"
              "total: 3\n");
}

} // namespace torustiq
