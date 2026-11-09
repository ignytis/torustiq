#include "plugins/outputs/stdout_output.h"

#include <iostream>

#include "torustiq/sdk.hpp"

namespace torustiq::plugins {

std::string FormatMessage(const std::string& format, const torustiq_message& message) {
    static const std::string kBody = "%BODY%";
    static const std::string kHeaderPrefix = "%HEADERS[";
    static const std::string kHeaderSuffix = "]%";

    std::string result;
    size_t i = 0;
    while (i < format.size()) {
        if (format.compare(i, kBody.size(), kBody) == 0) {
            if (message.body.data != nullptr) {
                result.append(reinterpret_cast<const char*>(message.body.data), message.body.len);
            }
            i += kBody.size();
        } else if (format.compare(i, kHeaderPrefix.size(), kHeaderPrefix) == 0) {
            const size_t name_start = i + kHeaderPrefix.size();
            const size_t end = format.find(kHeaderSuffix, name_start);
            if (end == std::string::npos) {
                result.push_back(format[i++]);
                continue;
            }
            const std::string name = format.substr(name_start, end - name_start);
            for (size_t h = 0; h < message.header_count; ++h) {
                if (name == message.headers[h].key) {
                    result += message.headers[h].value;
                    break;
                }
            }
            i = end + kHeaderSuffix.size();
        } else {
            result.push_back(format[i++]);
        }
    }
    return result;
}

} // namespace torustiq::plugins

namespace {

struct StdoutOutput {
    std::string format;
};

int32_t Create(const torustiq_config* config, void** out_handle) {
    try {
        auto* output = new StdoutOutput();
        output->format = torustiq::ConfigGet(config, "format", "%BODY%");
        *out_handle = output;
        return TORUSTIQ_OK;
    } catch (...) {
        return TORUSTIQ_ERROR;
    }
}

int32_t Write(void* handle, const torustiq_message* message) {
    try {
        auto* self = static_cast<StdoutOutput*>(handle);
        std::cout << torustiq::plugins::FormatMessage(self->format, *message) << '\n';
        return std::cout.good() ? TORUSTIQ_OK : TORUSTIQ_ERROR;
    } catch (...) {
        return TORUSTIQ_ERROR;
    }
}

int32_t Flush(void*) {
    std::cout.flush();
    return std::cout.good() ? TORUSTIQ_OK : TORUSTIQ_ERROR;
}

void Destroy(void* handle) {
    delete static_cast<StdoutOutput*>(handle);
}

} // namespace

extern "C" int32_t torustiq_register_stdout_output(const torustiq_registrar* registrar) {
    torustiq_output_vtable vtable{};
    vtable.abi_version = TORUSTIQ_SDK_ABI_VERSION;
    vtable.name = "stdout";
    vtable.create = Create;
    vtable.write = Write;
    vtable.flush = Flush;
    vtable.destroy = Destroy;
    return registrar->register_output(registrar->context, &vtable);
}
