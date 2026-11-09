#include "core/plugin_registry.h"

#include <iostream>

namespace torustiq {
namespace {

template <typename VTable> bool Validate(const VTable& vtable) {
    if (vtable.abi_version != TORUSTIQ_SDK_ABI_VERSION) {
        std::cerr << "plugin ABI version mismatch: " << vtable.abi_version << "\n";
        return false;
    }
    return vtable.name != nullptr && vtable.name[0] != '\0' && vtable.create != nullptr &&
           vtable.destroy != nullptr;
}

template <typename VTable> int32_t Add(std::map<std::string, VTable>& map, const VTable& vtable) {
    if (!Validate(vtable)) {
        return TORUSTIQ_ERROR;
    }
    return map.emplace(vtable.name, vtable).second ? TORUSTIQ_OK : TORUSTIQ_ERROR;
}

template <typename VTable>
const VTable* Find(const std::map<std::string, VTable>& map, const std::string& name) {
    auto it = map.find(name);
    return it == map.end() ? nullptr : &it->second;
}

} // namespace

int32_t PluginRegistry::RegisterInput(const torustiq_input_vtable& vtable) {
    if (vtable.run == nullptr) {
        return TORUSTIQ_ERROR;
    }
    return Add(inputs_, vtable);
}

int32_t PluginRegistry::RegisterTransform(const torustiq_transform_vtable& vtable) {
    if (vtable.process == nullptr) {
        return TORUSTIQ_ERROR;
    }
    return Add(transforms_, vtable);
}

int32_t PluginRegistry::RegisterOutput(const torustiq_output_vtable& vtable) {
    if (vtable.write == nullptr) {
        return TORUSTIQ_ERROR;
    }
    return Add(outputs_, vtable);
}

const torustiq_input_vtable* PluginRegistry::FindInput(const std::string& name) const {
    return Find(inputs_, name);
}

const torustiq_transform_vtable* PluginRegistry::FindTransform(const std::string& name) const {
    return Find(transforms_, name);
}

const torustiq_output_vtable* PluginRegistry::FindOutput(const std::string& name) const {
    return Find(outputs_, name);
}

torustiq_registrar PluginRegistry::registrar() {
    torustiq_registrar registrar{};
    registrar.context = this;
    registrar.register_input = [](void* context, const torustiq_input_vtable* vtable) {
        return static_cast<PluginRegistry*>(context)->RegisterInput(*vtable);
    };
    registrar.register_transform = [](void* context, const torustiq_transform_vtable* vtable) {
        return static_cast<PluginRegistry*>(context)->RegisterTransform(*vtable);
    };
    registrar.register_output = [](void* context, const torustiq_output_vtable* vtable) {
        return static_cast<PluginRegistry*>(context)->RegisterOutput(*vtable);
    };
    return registrar;
}

} // namespace torustiq
