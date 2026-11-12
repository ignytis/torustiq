// Test-only plugin library. Compile definitions: FAKE_PLUGIN_ID, FAKE_PLUGIN_ABI (0 = current).
// Appends "init <id>" / "shutdown <id>" to the file named by $FAKE_PLUGIN_LOG.

#include <cstdint>
#include <cstdio>
#include <cstdlib>

#include "torustiq/sdk.h"

namespace {

const torustiq_plugin_info kInfo = {
    FAKE_PLUGIN_ABI == 0 ? TORUSTIQ_SDK_ABI_VERSION : FAKE_PLUGIN_ABI, FAKE_PLUGIN_ID};

void Log(const char* what) {
    const char* path = std::getenv("FAKE_PLUGIN_LOG");
    if (path == nullptr) {
        return;
    }
    if (std::FILE* f = std::fopen(path, "a")) {
        std::fprintf(f, "%s %s\n", what, FAKE_PLUGIN_ID);
        std::fclose(f);
    }
}

int32_t Create(const torustiq_config*, const torustiq_payload_spec*, void** out_handle) {
    *out_handle = new int(0);
    return TORUSTIQ_OK;
}
int32_t Write(void*, const torustiq_message*) {
    return TORUSTIQ_OK;
}
void Destroy(void* handle) {
    delete static_cast<int*>(handle);
}

} // namespace

extern "C" {

TORUSTIQ_PLUGIN_EXPORT const torustiq_plugin_info* torustiq_plugin_get_info(void) {
    return &kInfo;
}

TORUSTIQ_PLUGIN_EXPORT int32_t torustiq_plugin_init(const torustiq_registrar* registrar) {
    Log("init");
    torustiq_output_vtable vtable{};
    vtable.abi_version = TORUSTIQ_SDK_ABI_VERSION;
    vtable.name = FAKE_PLUGIN_ID;
    vtable.create = Create;
    vtable.write = Write;
    vtable.destroy = Destroy;
    return registrar->register_output(registrar->context, &vtable);
}

TORUSTIQ_PLUGIN_EXPORT void torustiq_plugin_shutdown(void) {
    Log("shutdown");
}

} // extern "C"
