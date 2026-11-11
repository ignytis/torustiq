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

// Runs examples/json_pipeline.yaml: JSON bytes are decoded once, two Lua transforms edit the
// structured value, and it is encoded once.
TEST(LuaPipelineTest, JsonExamplePipelineEditsAttributesWithoutRecasting) {
    const PipelineConfig config =
        LoadPipelineConfig(std::string(TORUSTIQ_EXAMPLES_DIR) + "/json_pipeline.yaml");
    PluginRegistry registry;
    RegisterBuiltinPlugins(registry);
    Pipeline pipeline(config, registry);

    testing::internal::CaptureStdout();
    const int result = pipeline.Run();
    const std::string printed = testing::internal::GetCapturedStdout();

    EXPECT_EQ(result, 0) << pipeline.error();
    EXPECT_EQ(printed,
              R"({"id":1,"seen":10,"tags":["checked","v1"],"user":{"name":"USER1"}})"
              "\n"
              R"({"id":2,"seen":20,"tags":["checked","v2"],"user":{"name":"USER2"}})"
              "\n"
              R"({"id":3,"seen":30,"tags":["checked","v3"],"user":{"name":"USER3"}})"
              "\n");
}

// A bytes-only step after a structured one is rejected when the pipeline is built.
TEST(LuaPipelineTest, KindMismatchFailsAtStartup) {
    const PipelineConfig config = ParsePipelineConfig(R"(
inputs:
  i: {type: lua, config: {script: "function run(emit) end"}}
transforms:
  d: {type: decode_json, inputs: [i]}
outputs:
  o: {type: stdout, inputs: [d]}
)");
    PluginRegistry registry;
    RegisterBuiltinPlugins(registry);
    try {
        Pipeline pipeline(config, registry);
        FAIL() << "expected an error";
    } catch (const std::runtime_error& e) {
        EXPECT_NE(std::string(e.what()).find("'o'"), std::string::npos) << e.what();
        EXPECT_NE(std::string(e.what()).find("encode_json"), std::string::npos) << e.what();
    }
}

} // namespace torustiq
