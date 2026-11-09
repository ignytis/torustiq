#include "core/pipeline_config.h"

#include <set>

#include <yaml-cpp/yaml.h>

namespace torustiq {
namespace {

void Flatten(const std::string& prefix,
             const YAML::Node& node,
             std::map<std::string, std::string>& out) {
    switch (node.Type()) {
    case YAML::NodeType::Scalar:
        out[prefix] = node.Scalar();
        break;
    case YAML::NodeType::Null:
        out[prefix] = "";
        break;
    case YAML::NodeType::Map:
        for (const auto& kv : node) {
            Flatten(prefix + "." + kv.first.as<std::string>(), kv.second, out);
        }
        break;
    case YAML::NodeType::Sequence: {
        size_t index = 0;
        for (const auto& item : node) {
            Flatten(prefix + "." + std::to_string(index++), item, out);
        }
        break;
    }
    default:
        break;
    }
}

std::map<std::string, std::string> ParseParams(const std::string& id, const YAML::Node& node) {
    std::map<std::string, std::string> result;
    if (!node.IsDefined() || node.IsNull()) {
        return result;
    }
    if (!node.IsMap()) {
        throw ConfigError("'" + id + "': 'config' must be a map");
    }
    for (const auto& kv : node) {
        Flatten(kv.first.as<std::string>(), kv.second, result);
    }
    return result;
}

std::vector<std::string> ParseInputRefs(const std::string& id, const YAML::Node& node) {
    std::vector<std::string> result;
    if (!node.IsDefined() || node.IsNull()) {
        return result;
    }
    if (node.IsScalar()) {
        result.push_back(node.Scalar());
    } else if (node.IsSequence()) {
        for (const auto& item : node) {
            if (!item.IsScalar()) {
                throw ConfigError("'" + id + "': 'inputs' must contain ids");
            }
            result.push_back(item.Scalar());
        }
    } else {
        throw ConfigError("'" + id + "': 'inputs' must be a list of ids");
    }
    return result;
}

std::vector<ComponentConfig>
ParseSection(const YAML::Node& section, const std::string& section_name, bool allow_inputs) {
    std::vector<ComponentConfig> result;
    if (!section.IsDefined() || section.IsNull()) {
        return result;
    }
    if (!section.IsMap()) {
        throw ConfigError("'" + section_name + "' must be a map of id -> component");
    }
    for (const auto& kv : section) {
        ComponentConfig component;
        component.id = kv.first.as<std::string>();
        const YAML::Node& body = kv.second;
        if (!body.IsMap()) {
            throw ConfigError("'" + component.id + "' must be a map");
        }
        if (!body["type"].IsDefined() || !body["type"].IsScalar() ||
            body["type"].Scalar().empty()) {
            throw ConfigError("'" + component.id + "': missing 'type'");
        }
        component.type = body["type"].Scalar();
        if (body["inputs"].IsDefined() && !allow_inputs) {
            throw ConfigError("'" + component.id + "': inputs cannot have 'inputs'");
        }
        component.inputs = ParseInputRefs(component.id, body["inputs"]);
        component.config = ParseParams(component.id, body["config"]);
        result.push_back(std::move(component));
    }
    return result;
}

// Fills the default upstream or checks explicit references against `allowed`.
void ResolveInputs(ComponentConfig& component,
                   const std::set<std::string>& allowed,
                   const std::string& default_upstream) {
    if (component.inputs.empty()) {
        if (default_upstream.empty()) {
            throw ConfigError("'" + component.id + "': 'inputs' must be specified explicitly");
        }
        component.inputs.push_back(default_upstream);
        return;
    }
    for (const std::string& ref : component.inputs) {
        if (!allowed.contains(ref)) {
            throw ConfigError("'" + component.id + "': unknown or later-defined input '" + ref +
                              "'");
        }
    }
}

PipelineConfig Build(const YAML::Node& root) {
    if (!root.IsMap()) {
        throw ConfigError("pipeline definition must be a map");
    }
    PipelineConfig config;
    if (root["name"].IsDefined()) {
        config.name = root["name"].Scalar();
    }
    config.inputs = ParseSection(root["inputs"], "inputs", false);
    config.transforms = ParseSection(root["transforms"], "transforms", true);
    config.outputs = ParseSection(root["outputs"], "outputs", true);
    if (config.inputs.empty()) {
        throw ConfigError("pipeline needs at least one input");
    }
    if (config.outputs.empty()) {
        throw ConfigError("pipeline needs at least one output");
    }

    std::set<std::string> ids;
    for (const auto* section : {&config.inputs, &config.transforms, &config.outputs}) {
        for (const ComponentConfig& component : *section) {
            if (!ids.insert(component.id).second) {
                throw ConfigError("duplicate component id '" + component.id + "'");
            }
        }
    }

    std::set<std::string> allowed;
    for (const ComponentConfig& input : config.inputs) {
        allowed.insert(input.id);
    }
    const std::string sole_input = config.inputs.size() == 1 ? config.inputs[0].id : "";
    std::string previous = sole_input;
    for (ComponentConfig& transform : config.transforms) {
        ResolveInputs(transform, allowed, previous);
        allowed.insert(transform.id);
        previous = transform.id;
    }
    for (ComponentConfig& output : config.outputs) {
        ResolveInputs(output, allowed, previous);
    }
    return config;
}

} // namespace

PipelineConfig ParsePipelineConfig(const std::string& yaml) {
    try {
        return Build(YAML::Load(yaml));
    } catch (const YAML::Exception& e) {
        throw ConfigError(std::string("invalid YAML: ") + e.what());
    }
}

PipelineConfig LoadPipelineConfig(const std::string& path) {
    try {
        return Build(YAML::LoadFile(path));
    } catch (const YAML::Exception& e) {
        throw ConfigError(path + ": " + e.what());
    }
}

} // namespace torustiq
