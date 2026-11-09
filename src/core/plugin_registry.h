#ifndef TORUSTIQ_CORE_PLUGIN_REGISTRY_H_
#define TORUSTIQ_CORE_PLUGIN_REGISTRY_H_

#include <map>
#include <string>

#include "torustiq/sdk.h"

namespace torustiq {

// Maps component type names to plugin vtables. Today plugins are registered by built-in code;
// a loader for shared objects can call torustiq_plugin_register with registrar() later.
class PluginRegistry {
  public:
    PluginRegistry() = default;
    PluginRegistry(const PluginRegistry&) = delete;
    PluginRegistry& operator=(const PluginRegistry&) = delete;

    // Returns TORUSTIQ_OK, or TORUSTIQ_ERROR for a bad ABI version, missing fields or duplicates.
    int32_t RegisterInput(const torustiq_input_vtable& vtable);
    int32_t RegisterTransform(const torustiq_transform_vtable& vtable);
    int32_t RegisterOutput(const torustiq_output_vtable& vtable);

    const torustiq_input_vtable* FindInput(const std::string& name) const;
    const torustiq_transform_vtable* FindTransform(const std::string& name) const;
    const torustiq_output_vtable* FindOutput(const std::string& name) const;

    // Registrar to pass to plugin registration functions. Valid while this registry lives.
    torustiq_registrar registrar();

  private:
    std::map<std::string, torustiq_input_vtable> inputs_;
    std::map<std::string, torustiq_transform_vtable> transforms_;
    std::map<std::string, torustiq_output_vtable> outputs_;
};

} // namespace torustiq

#endif // TORUSTIQ_CORE_PLUGIN_REGISTRY_H_
