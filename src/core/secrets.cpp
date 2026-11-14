#include "core/secrets.h"

#include <map>
#include <stdexcept>
#include <string>

#include "core/message.h"

namespace torustiq {
namespace {

int32_t CollectValue(void* user_data, const torustiq_bytes* value) {
    if (user_data == nullptr || value == nullptr) {
        return TORUSTIQ_ERROR;
    }
    auto* out = static_cast<std::string*>(user_data);
    if (value->len > 0 && value->data == nullptr) {
        return TORUSTIQ_ERROR;
    }
    out->assign(reinterpret_cast<const char*>(value->data), value->len);
    return TORUSTIQ_OK;
}

std::string FetchSecret(const SecretConfig& secret, const PluginRegistry& registry) {
    const torustiq_secret_vtable* vtable = registry.FindSecret(secret.kind);
    if (vtable == nullptr) {
        throw ConfigError("secret '" + secret.id + "': unknown kind '" + secret.kind + "'");
    }
    const ConfigView params(secret.config);
    void* handle = nullptr;
    if (vtable->create(&params.get(), &handle) != TORUSTIQ_OK) {
        throw std::runtime_error("secret '" + secret.id + "': cannot create provider '" +
                                 secret.kind + "'");
    }
    std::string value;
    const int32_t status = vtable->resolve(handle, &CollectValue, &value);
    vtable->destroy(handle);
    if (status != TORUSTIQ_OK) {
        throw std::runtime_error("secret '" + secret.id + "': provider '" + secret.kind +
                                 "' failed to resolve the value");
    }
    return value;
}

} // namespace

void ResolveSecrets(PipelineConfig& config, const PluginRegistry& registry) {
    std::map<std::string, std::string> values;
    for (auto* section : {&config.inputs, &config.transforms, &config.outputs}) {
        for (ComponentConfig& component : *section) {
            for (const auto& [key, secret_id] : component.secret_refs) {
                auto it = values.find(secret_id);
                if (it == values.end()) {
                    const SecretConfig* found = nullptr;
                    for (const SecretConfig& secret : config.secrets) {
                        if (secret.id == secret_id) {
                            found = &secret;
                            break;
                        }
                    }
                    if (found == nullptr) {
                        throw ConfigError("'" + component.id + "': '" + key +
                                          "': unknown secret '" + secret_id + "'");
                    }
                    it = values.emplace(secret_id, FetchSecret(*found, registry)).first;
                }
                component.config[key] = it->second;
            }
            component.secret_refs.clear();
        }
    }
}

} // namespace torustiq
