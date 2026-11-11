#ifndef TORUSTIQ_TESTS_LUA_TEST_UTIL_H_
#define TORUSTIQ_TESTS_LUA_TEST_UTIL_H_

#include <map>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "core/message.h"
#include "core/plugin_registry.h"

namespace torustiq {

using Params = std::map<std::string, std::string>;

struct Collector {
    std::vector<Message> messages;
};

inline int32_t Collect(void* user_data, const torustiq_message* m) {
    static_cast<Collector*>(user_data)->messages.push_back(MessageFromC(*m));
    return TORUSTIQ_OK;
}

inline std::string BodyOf(const Message& m) {
    return BodyText(m);
}

inline Message MakeMessage(const std::string& body,
                           std::map<std::string, std::string> headers = {}) {
    Message m;
    m.payload = Value(Bytes(body.begin(), body.end()));
    m.headers = std::move(headers);
    return m;
}

// Base fixture: a registry in which the derived fixture registers one Lua plugin kind.
class LuaTestBase : public ::testing::Test {
  protected:
    void Register(int32_t (*register_fn)(const torustiq_registrar*)) {
        const torustiq_registrar registrar = registry_.registrar();
        ASSERT_EQ(register_fn(&registrar), TORUSTIQ_OK);
    }

    // Creates a plugin with the given params; returns the handle or nullptr. A transform or
    // output receives `input_kind`; the emitted kind is stored in `emitted_` (input, transform).
    void* Create(const torustiq_input_vtable* vt, const Params& params) {
        ConfigView config(params);
        void* handle = nullptr;
        return vt->create(&config.get(), &emitted_, &handle) == TORUSTIQ_OK ? handle : nullptr;
    }
    void* Create(const torustiq_transform_vtable* vt,
                 const Params& params,
                 uint32_t input_kind = TORUSTIQ_PAYLOAD_BYTES) {
        ConfigView config(params);
        void* handle = nullptr;
        const torustiq_payload_spec input{input_kind};
        return vt->create(&config.get(), &input, &emitted_, &handle) == TORUSTIQ_OK ? handle
                                                                                    : nullptr;
    }
    void* Create(const torustiq_output_vtable* vt,
                 const Params& params,
                 uint32_t input_kind = TORUSTIQ_PAYLOAD_BYTES) {
        ConfigView config(params);
        void* handle = nullptr;
        const torustiq_payload_spec input{input_kind};
        return vt->create(&config.get(), &input, &handle) == TORUSTIQ_OK ? handle : nullptr;
    }

    torustiq_payload_spec emitted_{};
    PluginRegistry registry_;
};

} // namespace torustiq

#endif // TORUSTIQ_TESTS_LUA_TEST_UTIL_H_
