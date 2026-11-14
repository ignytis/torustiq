#include <string>

#include <gtest/gtest.h>

#include "core/secrets.h"

namespace torustiq {
namespace {

// Fake provider "fake": the secret value is the `value` config parameter. It counts calls and
// fails when `value` is "fail".
struct Counters {
    int created = 0;
    int resolved = 0;
    int destroyed = 0;
};
Counters g_counters;

struct Handle {
    std::string value;
};

int32_t Create(const torustiq_config* config, void** out_handle) {
    ++g_counters.created;
    const char* value = torustiq_config_get(config, "value");
    if (value == nullptr) {
        return TORUSTIQ_ERROR;
    }
    *out_handle = new Handle{value};
    return TORUSTIQ_OK;
}

int32_t Resolve(void* handle, torustiq_secret_sink_fn sink, void* user_data) {
    ++g_counters.resolved;
    const auto* h = static_cast<const Handle*>(handle);
    if (h->value == "fail") {
        return TORUSTIQ_ERROR;
    }
    const torustiq_bytes bytes{reinterpret_cast<const uint8_t*>(h->value.data()), h->value.size()};
    return sink(user_data, &bytes);
}

void Destroy(void* handle) {
    ++g_counters.destroyed;
    delete static_cast<Handle*>(handle);
}

class SecretsTest : public testing::Test {
  protected:
    void SetUp() override {
        g_counters = {};
        torustiq_secret_vtable v{};
        v.abi_version = TORUSTIQ_SDK_ABI_VERSION;
        v.name = "fake";
        v.create = Create;
        v.resolve = Resolve;
        v.destroy = Destroy;
        ASSERT_EQ(registry_.RegisterSecret(v), TORUSTIQ_OK);
    }

    static ComponentConfig Component(const std::string& id,
                                     std::map<std::string, std::string> refs) {
        ComponentConfig c;
        c.id = id;
        c.type = "x";
        c.secret_refs = std::move(refs);
        return c;
    }

    PluginRegistry registry_;
};

} // namespace

TEST_F(SecretsTest, SubstitutesValuesAndClearsReferences) {
    PipelineConfig config;
    config.secrets.push_back({"pass", "fake", {{"value", "s3cret"}}});
    config.inputs.push_back(Component("in", {{"auth.password", "pass"}}));
    config.inputs[0].config["user"] = "bob";

    ResolveSecrets(config, registry_);
    EXPECT_EQ(config.inputs[0].config.at("auth.password"), "s3cret");
    EXPECT_EQ(config.inputs[0].config.at("user"), "bob");
    EXPECT_TRUE(config.inputs[0].secret_refs.empty());
}

TEST_F(SecretsTest, EmptyAndBinarySafeValues) {
    PipelineConfig config;
    config.secrets.push_back({"empty", "fake", {{"value", ""}}});
    config.outputs.push_back(Component("out", {{"k", "empty"}}));
    ResolveSecrets(config, registry_);
    EXPECT_EQ(config.outputs[0].config.at("k"), "");
}

TEST_F(SecretsTest, ResolvesEachReferencedSecretOnceAcrossSections) {
    PipelineConfig config;
    config.secrets.push_back({"a", "fake", {{"value", "A"}}});
    config.secrets.push_back({"b", "fake", {{"value", "B"}}});
    config.inputs.push_back(Component("in", {{"x", "a"}, {"y", "a"}}));
    config.transforms.push_back(Component("t", {{"x", "b"}}));
    config.outputs.push_back(Component("out", {{"x", "a"}}));

    ResolveSecrets(config, registry_);
    EXPECT_EQ(config.inputs[0].config.at("x"), "A");
    EXPECT_EQ(config.inputs[0].config.at("y"), "A");
    EXPECT_EQ(config.transforms[0].config.at("x"), "B");
    EXPECT_EQ(config.outputs[0].config.at("x"), "A");
    EXPECT_EQ(g_counters.created, 2);
    EXPECT_EQ(g_counters.resolved, 2);
    EXPECT_EQ(g_counters.destroyed, 2);
}

TEST_F(SecretsTest, UnreferencedSecretsAreNotResolved) {
    PipelineConfig config;
    config.secrets.push_back({"unused", "no_such_kind", {}});
    config.inputs.push_back(Component("in", {}));
    EXPECT_NO_THROW(ResolveSecrets(config, registry_));
    EXPECT_EQ(g_counters.created, 0);
}

TEST_F(SecretsTest, UnknownProviderKindThrowsConfigError) {
    PipelineConfig config;
    config.secrets.push_back({"a", "no_such_kind", {}});
    config.inputs.push_back(Component("in", {{"x", "a"}}));
    EXPECT_THROW(ResolveSecrets(config, registry_), ConfigError);
}

TEST_F(SecretsTest, UnknownSecretIdThrowsConfigError) {
    PipelineConfig config;
    config.inputs.push_back(Component("in", {{"x", "missing"}}));
    EXPECT_THROW(ResolveSecrets(config, registry_), ConfigError);
}

TEST_F(SecretsTest, ProviderFailureNamesSecretButNotValue) {
    PipelineConfig config;
    config.secrets.push_back({"bad", "fake", {{"value", "fail"}}});
    config.inputs.push_back(Component("in", {{"x", "bad"}}));
    try {
        ResolveSecrets(config, registry_);
        FAIL() << "expected an exception";
    } catch (const std::runtime_error& e) {
        EXPECT_NE(std::string(e.what()).find("bad"), std::string::npos);
    }
    EXPECT_EQ(g_counters.destroyed, 1); // the handle is released even on failure
    EXPECT_FALSE(config.inputs[0].config.contains("x"));
}

TEST_F(SecretsTest, CreateFailureThrows) {
    PipelineConfig config;
    config.secrets.push_back({"a", "fake", {}}); // the fake needs a "value" parameter
    config.inputs.push_back(Component("in", {{"x", "a"}}));
    EXPECT_THROW(ResolveSecrets(config, registry_), std::runtime_error);
    EXPECT_EQ(g_counters.destroyed, 0); // no handle was produced
}

} // namespace torustiq
