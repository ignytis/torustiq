#include <gtest/gtest.h>

#include "core/pipeline_config.h"

namespace torustiq {

TEST(PipelineConfigTest, ParsesExampleFromTaskWithExplicitWiring) {
    const auto c = ParsePipelineConfig(R"(
name: My simple pipeline
inputs:
  my_input_id:
    type: stdin
transforms:
  my_transform:
    type: lua
outputs:
  my_output_id:
    inputs:
      - my_transform
    type: stdout
    config:
      format: "%BODY% / %HEADERS[some_header]%"
)");
    EXPECT_EQ(c.name, "My simple pipeline");
    ASSERT_EQ(c.inputs.size(), 1u);
    EXPECT_EQ(c.inputs[0].type, "stdin");
    ASSERT_EQ(c.transforms.size(), 1u);
    EXPECT_EQ(c.transforms[0].inputs, std::vector<std::string>{"my_input_id"});
    ASSERT_EQ(c.outputs.size(), 1u);
    EXPECT_EQ(c.outputs[0].inputs, std::vector<std::string>{"my_transform"});
    EXPECT_EQ(c.outputs[0].config.at("format"), "%BODY% / %HEADERS[some_header]%");
}

TEST(PipelineConfigTest, DefaultsWireInChain) {
    const auto c = ParsePipelineConfig(R"(
inputs: {a: {type: stdin}}
transforms: {t1: {type: x}, t2: {type: x}}
outputs: {o: {type: stdout}}
)");
    EXPECT_EQ(c.transforms[0].inputs, std::vector<std::string>{"a"});
    EXPECT_EQ(c.transforms[1].inputs, std::vector<std::string>{"t1"});
    EXPECT_EQ(c.outputs[0].inputs, std::vector<std::string>{"t2"});
}

TEST(PipelineConfigTest, FlattensNestedConfig) {
    const auto c = ParsePipelineConfig(R"(
inputs:
  a:
    type: kafka
    config:
      brokers: [h1, h2]
      auth: {user: bob, port: 9092}
outputs: {o: {type: stdout}}
)");
    const auto& cfg = c.inputs[0].config;
    EXPECT_EQ(cfg.at("brokers.0"), "h1");
    EXPECT_EQ(cfg.at("brokers.1"), "h2");
    EXPECT_EQ(cfg.at("auth.user"), "bob");
    EXPECT_EQ(cfg.at("auth.port"), "9092");
}

TEST(PipelineConfigTest, Errors) {
    EXPECT_THROW(ParsePipelineConfig("outputs: {o: {type: stdout}}"), ConfigError);
    EXPECT_THROW(ParsePipelineConfig("inputs: {a: {type: stdin}}"), ConfigError);
    EXPECT_THROW(ParsePipelineConfig("inputs: {a: {}}\noutputs: {o: {type: s}}"), ConfigError);
    EXPECT_THROW(ParsePipelineConfig("inputs: [unclosed"), ConfigError);
    EXPECT_THROW(ParsePipelineConfig("just a string"), ConfigError);
    // Unknown reference
    EXPECT_THROW(
        ParsePipelineConfig("inputs: {a: {type: s}}\noutputs: {o: {type: s, inputs: [nope]}}"),
        ConfigError);
    // Duplicate ids across sections
    EXPECT_THROW(ParsePipelineConfig("inputs: {a: {type: s}}\noutputs: {a: {type: s}}"),
                 ConfigError);
    // Output cannot be an upstream; transform cannot reference a later transform.
    EXPECT_THROW(ParsePipelineConfig("inputs: {a: {type: s}}\n"
                                     "transforms: {t1: {type: x, inputs: [t2]}, t2: {type: x}}\n"
                                     "outputs: {o: {type: s}}"),
                 ConfigError);
    // Input with 'inputs'
    EXPECT_THROW(ParsePipelineConfig("inputs: {a: {type: s, inputs: [b]}}\n"
                                     "outputs: {o: {type: s}}"),
                 ConfigError);
}

TEST(PipelineConfigTest, MultipleInputsRequireExplicitWiring) {
    EXPECT_THROW(ParsePipelineConfig("inputs: {a: {type: s}, b: {type: s}}\n"
                                     "outputs: {o: {type: s}}"),
                 ConfigError);
    EXPECT_NO_THROW(ParsePipelineConfig("inputs: {a: {type: s}, b: {type: s}}\n"
                                        "outputs: {o: {type: s, inputs: [a, b]}}"));
}

TEST(PipelineConfigTest, LoadMissingFileThrowsConfigError) {
    EXPECT_THROW(LoadPipelineConfig("/nonexistent/pipeline.yaml"), ConfigError);
}

} // namespace torustiq
