#include "plugins/inputs/directory_input.h"

#include <algorithm>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

#include "torustiq/sdk.hpp"

namespace fs = std::filesystem;

namespace {

struct DirectoryInput {
    std::string path;
    std::atomic<bool> stopped{false};
};

int32_t Create(const torustiq_config* config, void** out_handle) {
    try {
        const std::string path = torustiq::ConfigGet(config, "path");
        if (path.empty()) {
            std::cerr << "directory input: 'path' is required\n";
            return TORUSTIQ_ERROR;
        }
        auto* input = new DirectoryInput();
        input->path = path;
        *out_handle = input;
        return TORUSTIQ_OK;
    } catch (...) {
        return TORUSTIQ_ERROR;
    }
}

int32_t Run(void* handle, torustiq_emit_fn emit, void* user_data) {
    auto* self = static_cast<DirectoryInput*>(handle);
    try {
        std::error_code ec;
        std::vector<fs::path> files;
        for (fs::directory_iterator it(self->path, ec), end; !ec && it != end; it.increment(ec)) {
            if (it->is_regular_file()) {
                files.push_back(it->path());
            }
        }
        if (ec) {
            std::cerr << "directory input: cannot read '" << self->path << "': " << ec.message()
                      << "\n";
            return TORUSTIQ_ERROR;
        }
        std::sort(files.begin(), files.end());

        for (const fs::path& file : files) {
            if (self->stopped.load()) {
                break;
            }
            std::ifstream in(file, std::ios::binary);
            if (!in) {
                std::cerr << "directory input: cannot open '" << file.string() << "'\n";
                return TORUSTIQ_ERROR;
            }
            const std::vector<char> content((std::istreambuf_iterator<char>(in)),
                                            std::istreambuf_iterator<char>());
            const std::string filename = file.filename().string();
            const std::string full_path = file.string();
            const torustiq_header headers[] = {{"filename", filename.c_str()},
                                               {"path", full_path.c_str()}};
            torustiq_message message{};
            message.body.data = reinterpret_cast<const uint8_t*>(content.data());
            message.body.len = content.size();
            message.headers = headers;
            message.header_count = 2;
            const int32_t status = emit(user_data, &message);
            if (status != TORUSTIQ_OK) {
                return status == TORUSTIQ_STOPPED ? TORUSTIQ_OK : status;
            }
        }
        return TORUSTIQ_OK;
    } catch (...) {
        return TORUSTIQ_ERROR;
    }
}

void Stop(void* handle) {
    static_cast<DirectoryInput*>(handle)->stopped.store(true);
}

void Destroy(void* handle) {
    delete static_cast<DirectoryInput*>(handle);
}

} // namespace

extern "C" int32_t torustiq_register_directory_input(const torustiq_registrar* registrar) {
    torustiq_input_vtable vtable{};
    vtable.abi_version = TORUSTIQ_SDK_ABI_VERSION;
    vtable.name = "directory";
    vtable.create = Create;
    vtable.run = Run;
    vtable.stop = Stop;
    vtable.destroy = Destroy;
    return registrar->register_input(registrar->context, &vtable);
}
