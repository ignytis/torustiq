#include <cstdlib>
#include <string>

#include <gtest/gtest.h>

#include "core/plugin_registry.h"
#include "plugins/secrets/env_secret.h"

namespace torustiq {
namespace {

constexpr const char* kVar = "TORUSTIQ_ENV_SECRET_TEST_VAR";

void SetEnv(const char* name, const char* value) {
#ifdef _WIN32
    _putenv_s(name, value);
#else
    setenv(name, value, 1);
#endif
}

void UnsetEnv(const char* name) {
#ifdef _WIN32
    _putenv_s(name, "");
#else
    unsetenv(name);
#endif
}

int32_t Collect(void* user_data, const torustiq_bytes* value) {
    static_cast<std::string*>(user_data)->assign(reinterpret_cast<const char*>(value->data),
                                                 value->len);
    return TORUSTIQ_OK;
}

class EnvSecretTest : public testing::Test {
  protected:
    void SetUp() override {
        UnsetEnv(kVar);
        const torustiq_registrar registrar = registry_.registrar();
        ASSERT_EQ(torustiq_register_env_secret(&registrar), TORUSTIQ_OK);
        vtable_ = registry_.FindSecret("env");
        ASSERT_NE(vtable_, nullptr);
    }
    void TearDown() override {
        UnsetEnv(kVar);
    }

    int32_t Resolve(const char* key, std::string& out) {
        const torustiq_header item{"key", key};
        const torustiq_config config{&item, 1};
        void* handle = nullptr;
        if (vtable_->create(&config, &handle) != TORUSTIQ_OK) {
            return TORUSTIQ_ERROR;
        }
        const int32_t status = vtable_->resolve(handle, Collect, &out);
        vtable_->destroy(handle);
        return status;
    }

    PluginRegistry registry_;
    const torustiq_secret_vtable* vtable_ = nullptr;
};

} // namespace

TEST_F(EnvSecretTest, ReadsVariable) {
    SetEnv(kVar, "hunter2");
    std::string value;
    ASSERT_EQ(Resolve(kVar, value), TORUSTIQ_OK);
    EXPECT_EQ(value, "hunter2");
}

TEST_F(EnvSecretTest, UnsetVariableFails) {
    std::string value;
    EXPECT_EQ(Resolve(kVar, value), TORUSTIQ_ERROR);
}

TEST_F(EnvSecretTest, MissingKeyParameterFailsToCreate) {
    const torustiq_config config{nullptr, 0};
    void* handle = nullptr;
    EXPECT_EQ(vtable_->create(&config, &handle), TORUSTIQ_ERROR);
}

} // namespace torustiq
