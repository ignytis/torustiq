#include "plugins/secrets/env_secret.h"

#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>

#include "torustiq/sdk.hpp"

namespace {

struct EnvSecret {
    std::string key;
};

int32_t Create(const torustiq_config* config, void** out_handle) {
    try {
        std::string key = torustiq::ConfigGet(config, "key");
        if (key.empty()) {
            std::cerr << "env secret: 'key' is required\n";
            return TORUSTIQ_ERROR;
        }
        *out_handle = new EnvSecret{std::move(key)};
        return TORUSTIQ_OK;
    } catch (...) {
        return TORUSTIQ_ERROR;
    }
}

int32_t Resolve(void* handle, torustiq_secret_sink_fn sink, void* user_data) {
    try {
        const auto* secret = static_cast<const EnvSecret*>(handle);
        const char* value = std::getenv(secret->key.c_str());
        if (value == nullptr) {
            std::cerr << "env secret: variable '" << secret->key << "' is not set\n";
            return TORUSTIQ_ERROR;
        }
        const torustiq_bytes bytes{reinterpret_cast<const uint8_t*>(value), std::strlen(value)};
        return sink(user_data, &bytes);
    } catch (...) {
        return TORUSTIQ_ERROR;
    }
}

void Destroy(void* handle) {
    delete static_cast<EnvSecret*>(handle);
}

} // namespace

extern "C" int32_t torustiq_register_env_secret(const torustiq_registrar* registrar) {
    torustiq_secret_vtable vtable{};
    vtable.abi_version = TORUSTIQ_SDK_ABI_VERSION;
    vtable.name = "env";
    vtable.create = Create;
    vtable.resolve = Resolve;
    vtable.destroy = Destroy;
    return registrar->register_secret(registrar->context, &vtable);
}
