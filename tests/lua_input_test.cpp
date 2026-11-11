#include <chrono>
#include <string>
#include <thread>

#include <gtest/gtest.h>

#include "lua_test_util.h"
#include "plugins/inputs/lua_input.h"

namespace torustiq {
namespace {

class LuaInputTest : public LuaTestBase {
  protected:
    void SetUp() override {
        Register(torustiq_register_lua_input);
    }
};

TEST_F(LuaInputTest, RegistersInput) {
    EXPECT_NE(registry_.FindInput("lua"), nullptr);
}

TEST_F(LuaInputTest, InputEmitsMessages) {
    const auto* vt = registry_.FindInput("lua");
    void* handle = Create(vt, {{"script", R"(
        function run(emit)
          for i = 1, 3 do emit({ body = "m" .. i, headers = { i = i } }) end
          emit("plain")
        end)"}});
    ASSERT_NE(handle, nullptr);
    Collector out;
    EXPECT_EQ(vt->run(handle, Collect, &out), TORUSTIQ_OK);
    vt->destroy(handle);
    ASSERT_EQ(out.messages.size(), 4u);
    EXPECT_EQ(BodyOf(out.messages[1]), "m2");
    EXPECT_EQ(out.messages[1].headers.at("i"), "2");
    EXPECT_EQ(BodyOf(out.messages[3]), "plain");
}

TEST_F(LuaInputTest, InputSeesStopFromEmit) {
    const auto* vt = registry_.FindInput("lua");
    void* handle = Create(vt, {{"script", R"(
        function run(emit)
          local sent = 0
          while emit("x") do sent = sent + 1 end
          emit("after stop: " .. sent)
        end)"}});
    ASSERT_NE(handle, nullptr);
    struct Limited {
        std::vector<Message> messages;
        size_t limit = 3;
    } out;
    auto limited = [](void* ud, const torustiq_message* m) -> int32_t {
        auto* o = static_cast<Limited*>(ud);
        o->messages.push_back(MessageFromC(*m));
        return o->messages.size() >= o->limit ? TORUSTIQ_STOPPED : TORUSTIQ_OK;
    };
    EXPECT_EQ(vt->run(handle, limited, &out), TORUSTIQ_OK);
    vt->destroy(handle);
    EXPECT_GE(out.messages.size(), 3u);
}

TEST_F(LuaInputTest, InputStopInterruptsBusyLoopAndSleep) {
    for (const char* body : {"while true do end", "while true do sleep(0.01) end"}) {
        const auto* vt = registry_.FindInput("lua");
        void* handle = Create(vt,
                              {{"script", std::string("function run(emit) ") + body + " end"},
                               {"max_instructions", "0"}});
        ASSERT_NE(handle, nullptr);
        Collector out;
        std::thread stopper([&] {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            vt->stop(handle);
        });
        EXPECT_EQ(vt->run(handle, Collect, &out), TORUSTIQ_OK) << body;
        stopper.join();
        vt->destroy(handle);
    }
}

TEST_F(LuaInputTest, InputInstructionLimitAppliesBetweenEmits) {
    const auto* vt = registry_.FindInput("lua");
    // Plenty of work overall, but little between two emit() calls: fine.
    void* ok = Create(vt,
                      {{"script", R"(
        function run(emit)
          for i = 1, 200 do
            for j = 1, 500 do end
            emit("x")
          end
        end)"},
                       {"max_instructions", "20000"}});
    ASSERT_NE(ok, nullptr);
    Collector out;
    EXPECT_EQ(vt->run(ok, Collect, &out), TORUSTIQ_OK);
    vt->destroy(ok);
    EXPECT_EQ(out.messages.size(), 200u);
    // Runaway code without emit() is cut off.
    void* bad = Create(
        vt,
        {{"script", "function run(emit) while true do end end"}, {"max_instructions", "20000"}});
    ASSERT_NE(bad, nullptr);
    EXPECT_EQ(vt->run(bad, Collect, &out), TORUSTIQ_ERROR);
    vt->destroy(bad);
}

TEST_F(LuaInputTest, InputErrorsAreReported) {
    const auto* vt = registry_.FindInput("lua");
    void* handle = Create(vt, {{"script", "function run(emit) emit({ body = {} }) end"}});
    ASSERT_NE(handle, nullptr);
    Collector out;
    EXPECT_EQ(vt->run(handle, Collect, &out), TORUSTIQ_ERROR);
    vt->destroy(handle);
    EXPECT_EQ(Create(vt, {{"script", "function process() end"}}), nullptr); // needs run()
}

TEST_F(LuaInputTest, StructuredInputEmitsValues) {
    const auto* vt = registry_.FindInput("lua");
    void* handle = Create(vt, {{"payload", "value"}, {"script", R"(
        function run(emit)
          emit({ value = { id = 1, list = { "a", "b" } }, headers = { k = "v" } })
        end)"}});
    ASSERT_NE(handle, nullptr);
    EXPECT_EQ(emitted_.kind, TORUSTIQ_PAYLOAD_VALUE);
    Collector out;
    EXPECT_EQ(vt->run(handle, Collect, &out), TORUSTIQ_OK);
    vt->destroy(handle);
    ASSERT_EQ(out.messages.size(), 1u);
    EXPECT_EQ(out.messages[0].payload,
              Value(Value::Map{{"id", Value(int64_t{1})},
                               {"list", Value(Value::Array{Value("a"), Value("b")})}}));
    EXPECT_EQ(out.messages[0].headers.at("k"), "v");
}

TEST_F(LuaInputTest, DefaultInputKindIsBytes) {
    const auto* vt = registry_.FindInput("lua");
    void* handle = Create(vt, {{"script", "function run(emit) end"}});
    ASSERT_NE(handle, nullptr);
    EXPECT_EQ(emitted_.kind, TORUSTIQ_PAYLOAD_BYTES);
    vt->destroy(handle);
    EXPECT_EQ(Create(vt, {{"payload", "x"}, {"script", "function run(emit) end"}}), nullptr);
}

} // namespace
} // namespace torustiq
