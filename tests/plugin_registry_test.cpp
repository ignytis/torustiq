#include <gtest/gtest.h>

#include "core/plugin_registry.h"

namespace torustiq {
namespace {

int32_t Create(const torustiq_config*, void**) {
    return TORUSTIQ_OK;
}
int32_t Run(void*, torustiq_emit_fn, void*) {
    return TORUSTIQ_OK;
}
void Destroy(void*) {}

torustiq_input_vtable ValidInput() {
    torustiq_input_vtable v{};
    v.abi_version = TORUSTIQ_SDK_ABI_VERSION;
    v.name = "fake";
    v.create = Create;
    v.run = Run;
    v.destroy = Destroy;
    return v;
}

} // namespace

TEST(PluginRegistryTest, RegisterAndFind) {
    PluginRegistry registry;
    EXPECT_EQ(registry.RegisterInput(ValidInput()), TORUSTIQ_OK);
    ASSERT_NE(registry.FindInput("fake"), nullptr);
    EXPECT_EQ(registry.FindInput("other"), nullptr);
    EXPECT_EQ(registry.FindOutput("fake"), nullptr);
}

TEST(PluginRegistryTest, RejectsDuplicate) {
    PluginRegistry registry;
    EXPECT_EQ(registry.RegisterInput(ValidInput()), TORUSTIQ_OK);
    EXPECT_EQ(registry.RegisterInput(ValidInput()), TORUSTIQ_ERROR);
}

TEST(PluginRegistryTest, RejectsBadAbiAndMissingFunctions) {
    PluginRegistry registry;
    auto bad_abi = ValidInput();
    bad_abi.abi_version = 999;
    EXPECT_EQ(registry.RegisterInput(bad_abi), TORUSTIQ_ERROR);
    auto no_run = ValidInput();
    no_run.run = nullptr;
    EXPECT_EQ(registry.RegisterInput(no_run), TORUSTIQ_ERROR);
    auto no_name = ValidInput();
    no_name.name = nullptr;
    EXPECT_EQ(registry.RegisterInput(no_name), TORUSTIQ_ERROR);
}

TEST(PluginRegistryTest, RegistrarForwardsToRegistry) {
    PluginRegistry registry;
    torustiq_registrar r = registry.registrar();
    torustiq_input_vtable v = ValidInput();
    EXPECT_EQ(r.register_input(r.context, &v), TORUSTIQ_OK);
    EXPECT_NE(registry.FindInput("fake"), nullptr);
}

} // namespace torustiq
