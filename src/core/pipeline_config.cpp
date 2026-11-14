#include "core/pipeline_config.h"

#include <set>

#include <yaml-cpp/yaml.h>

namespace torustiq {
namespace {

constexpr const char* kKindKey = "_kind";
constexpr const char* kKindString = "string";
constexpr const char* kKindRefSecret = "ref_secret";

// Where flattened parameters go. `refs` is null where secret references are not allowed.
struct FlattenTarget {
    const std::string& owner; // component or secret id, for error messages
    std::map<std::string, std::string>& values;
    std::map<std::string, std::string>* refs;
};

std::string ScalarField(const FlattenTarget& target,
                        const std::string& key,
                        const YAML::Node& node,
                        const char* field) {
    const YAML::Node value = node[field];
    if (!value.IsDefined() || !value.IsScalar()) {
        throw ConfigError("'" + target.owner + "': '" + key + "': '" + field +
                          "' must be a scalar");
    }
    return value.Scalar();
}

// Handles a map with `_kind`: a typed value (`string`) or a secret reference (`ref_secret`).
void FlattenTyped(const std::string& key, const YAML::Node& node, FlattenTarget& target) {
    const YAML::Node kind_node = node[kKindKey];
    if (!kind_node.IsScalar()) {
        throw ConfigError("'" + target.owner + "': '" + key + "': '" + kKindKey +
                          "' must be a scalar");
    }
    const std::string kind = kind_node.Scalar();
    std::string value;
    if (kind == kKindString) {
        value = ScalarField(target, key, node, "value");
    } else if (kind == kKindRefSecret) {
        if (target.refs == nullptr) {
            throw ConfigError("'" + target.owner + "': '" + key +
                              "': secret references are not allowed here");
        }
        value = ScalarField(target, key, node, "id");
    } else {
        throw ConfigError("'" + target.owner + "': '" + key + "': unknown " + kKindKey + " '" +
                          kind + "'");
    }
    const char* field = kind == kKindString ? "value" : "id";
    for (const auto& kv : node) {
        const std::string name = kv.first.as<std::string>();
        if (name != kKindKey && name != field) {
            throw ConfigError("'" + target.owner + "': '" + key + "': unexpected field '" + name +
                              "' for " + kKindKey + " '" + kind + "'");
        }
    }
    if (kind == kKindString) {
        target.values[key] = std::move(value);
    } else {
        (*target.refs)[key] = std::move(value);
    }
}

void Flatten(const std::string& prefix, const YAML::Node& node, FlattenTarget& target) {
    switch (node.Type()) {
    case YAML::NodeType::Scalar:
        target.values[prefix] = node.Scalar();
        break;
    case YAML::NodeType::Null:
        target.values[prefix] = "";
        break;
    case YAML::NodeType::Map:
        if (node[kKindKey].IsDefined()) {
            FlattenTyped(prefix, node, target);
            break;
        }
        for (const auto& kv : node) {
            Flatten(prefix + "." + kv.first.as<std::string>(), kv.second, target);
        }
        break;
    case YAML::NodeType::Sequence: {
        size_t index = 0;
        for (const auto& item : node) {
            Flatten(prefix + "." + std::to_string(index++), item, target);
        }
        break;
    }
    default:
        break;
    }
}

// Flattens `config` of a component (or secret provider). Secret references go to `refs`
// unless it is null, in which case they are rejected.
std::map<std::string, std::string> ParseParams(const std::string& id,
                                               const YAML::Node& node,
                                               std::map<std::string, std::string>* refs) {
    std::map<std::string, std::string> result;
    if (!node.IsDefined() || node.IsNull()) {
        return result;
    }
    if (!node.IsMap()) {
        throw ConfigError("'" + id + "': 'config' must be a map");
    }
    FlattenTarget target{id, result, refs};
    for (const auto& kv : node) {
        Flatten(kv.first.as<std::string>(), kv.second, target);
    }
    return result;
}

std::vector<SecretConfig> ParseSecrets(const YAML::Node& section) {
    std::vector<SecretConfig> result;
    if (!section.IsDefined() || section.IsNull()) {
        return result;
    }
    if (!section.IsMap()) {
        throw ConfigError("'secrets' must be a map of id -> secret");
    }
    for (const auto& kv : section) {
        SecretConfig secret;
        secret.id = kv.first.as<std::string>();
        const YAML::Node& body = kv.second;
        if (!body.IsMap()) {
            throw ConfigError("secret '" + secret.id + "' must be a map");
        }
        if (!body["kind"].IsDefined() || !body["kind"].IsScalar() ||
            body["kind"].Scalar().empty()) {
            throw ConfigError("secret '" + secret.id + "': missing 'kind'");
        }
        secret.kind = body["kind"].Scalar();
        secret.config = ParseParams("secrets." + secret.id, body["config"], nullptr);
        result.push_back(std::move(secret));
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
        component.config = ParseParams(component.id, body["config"], &component.secret_refs);
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
    config.secrets = ParseSecrets(root["secrets"]);
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

    std::set<std::string> secret_ids;
    for (const SecretConfig& secret : config.secrets) {
        secret_ids.insert(secret.id);
    }
    for (const auto* section : {&config.inputs, &config.transforms, &config.outputs}) {
        for (const ComponentConfig& component : *section) {
            for (const auto& [key, secret_id] : component.secret_refs) {
                if (!secret_ids.contains(secret_id)) {
                    throw ConfigError("'" + component.id + "': '" + key + "': unknown secret '" +
                                      secret_id + "'");
                }
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
