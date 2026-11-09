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
    return std::string(m.body.begin(), m.body.end());
}

inline Message MakeMessage(const std::string& body,
                           std::map<std::string, std::string> headers = {}) {
    Message m;
    m.body.assign(body.begin(), body.end());
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

    // Creates a plugin with the given params; returns the handle or nullptr.
    template <typename VTable> void* Create(const VTable* vt, const Params& params) {
        ConfigView config(params);
        void* handle = nullptr;
        if (vt->create(&config.get(), &handle) != TORUSTIQ_OK) {
            return nullptr;
        }
        return handle;
    }

    PluginRegistry registry_;
};

} // namespace torustiq

#endif // TORUSTIQ_TESTS_LUA_TEST_UTIL_H_
