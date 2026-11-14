#include "plugins/secrets/file_secret.h"

#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

#include "torustiq/sdk.hpp"

namespace {

struct FileSecret {
    std::string path;
};

int32_t Create(const torustiq_config* config, void** out_handle) {
    try {
        std::string path = torustiq::ConfigGet(config, "path");
        if (path.empty()) {
            std::cerr << "file secret: 'path' is required\n";
            return TORUSTIQ_ERROR;
        }
        *out_handle = new FileSecret{std::move(path)};
        return TORUSTIQ_OK;
    } catch (...) {
        return TORUSTIQ_ERROR;
    }
}

int32_t Resolve(void* handle, torustiq_secret_sink_fn sink, void* user_data) {
    try {
        const auto* secret = static_cast<const FileSecret*>(handle);
        std::ifstream file(secret->path, std::ios::binary);
        if (!file) {
            std::cerr << "file secret: cannot read '" << secret->path << "'\n";
            return TORUSTIQ_ERROR;
        }
        std::string content((std::istreambuf_iterator<char>(file)),
                            std::istreambuf_iterator<char>());
        if (content.ends_with('\n')) {
            content.pop_back();
            if (content.ends_with('\r')) {
                content.pop_back();
            }
        }
        const torustiq_bytes bytes{reinterpret_cast<const uint8_t*>(content.data()),
                                   content.size()};
        return sink(user_data, &bytes);
    } catch (...) {
        return TORUSTIQ_ERROR;
    }
}

void Destroy(void* handle) {
    delete static_cast<FileSecret*>(handle);
}

} // namespace

extern "C" int32_t torustiq_register_file_secret(const torustiq_registrar* registrar) {
    torustiq_secret_vtable vtable{};
    vtable.abi_version = TORUSTIQ_SDK_ABI_VERSION;
    vtable.name = "file";
    vtable.create = Create;
    vtable.resolve = Resolve;
    vtable.destroy = Destroy;
    return registrar->register_secret(registrar->context, &vtable);
}
