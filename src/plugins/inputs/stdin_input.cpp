#include "plugins/inputs/stdin_input.h"

#include <atomic>
#include <iostream>
#include <string>

namespace {

struct StdinInput {
    std::atomic<bool> stopped{false};
};

int32_t Create(const torustiq_config*, torustiq_payload_spec* out_output, void** out_handle) {
    try {
        out_output->kind = TORUSTIQ_PAYLOAD_BYTES;
        *out_handle = new StdinInput();
        return TORUSTIQ_OK;
    } catch (...) {
        return TORUSTIQ_ERROR;
    }
}

int32_t Run(void* handle, torustiq_emit_fn emit, void* user_data) {
    auto* self = static_cast<StdinInput*>(handle);
    try {
        std::string line;
        while (!self->stopped.load() && std::getline(std::cin, line)) {
            torustiq_message message{};
            message.payload = torustiq_value_from_bytes(
                reinterpret_cast<const uint8_t*>(line.data()), line.size());
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
    static_cast<StdinInput*>(handle)->stopped.store(true);
}

void Destroy(void* handle) {
    delete static_cast<StdinInput*>(handle);
}

} // namespace

extern "C" int32_t torustiq_register_stdin_input(const torustiq_registrar* registrar) {
    torustiq_input_vtable vtable{};
    vtable.abi_version = TORUSTIQ_SDK_ABI_VERSION;
    vtable.name = "stdin";
    vtable.create = Create;
    vtable.run = Run;
    vtable.stop = Stop;
    vtable.destroy = Destroy;
    return registrar->register_input(registrar->context, &vtable);
}
