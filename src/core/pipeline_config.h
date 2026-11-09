#ifndef TORUSTIQ_CORE_PIPELINE_CONFIG_H_
#define TORUSTIQ_CORE_PIPELINE_CONFIG_H_

#include <map>
#include <stdexcept>
#include <string>
#include <vector>

namespace torustiq {

class ConfigError : public std::runtime_error {
  public:
    using std::runtime_error::runtime_error;
};

struct ComponentConfig {
    std::string id;
    std::string type;
    // Upstream component ids. After parsing this is always fully resolved
    // (empty only for inputs).
    std::vector<std::string> inputs;
    // Plugin parameters. Nested YAML is flattened: maps to "a.b", sequences to "a.0".
    std::map<std::string, std::string> config;
};

struct PipelineConfig {
    std::string name;
    std::vector<ComponentConfig> inputs;
    std::vector<ComponentConfig> transforms;
    std::vector<ComponentConfig> outputs;
};

// Both functions throw ConfigError on malformed or inconsistent definitions.
//
// Wiring rules: components may only reference inputs or earlier transforms (so the graph is
// acyclic). When `inputs` is omitted a transform reads from the previous transform (or the sole
// input) and an output reads from the last transform (or the sole input).
PipelineConfig ParsePipelineConfig(const std::string& yaml);
PipelineConfig LoadPipelineConfig(const std::string& path);

} // namespace torustiq

#endif // TORUSTIQ_CORE_PIPELINE_CONFIG_H_
