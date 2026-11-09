#include <string>

#include <gtest/gtest.h>

#include "lua_test_util.h"
#include "plugins/outputs/lua_output.h"

namespace torustiq {
namespace {

class LuaOutputTest : public LuaTestBase {
  protected:
    void SetUp() override {
        Register(torustiq_register_lua_output);
    }
};

TEST_F(LuaOutputTest, RegistersOutput) {
    EXPECT_NE(registry_.FindOutput("lua"), nullptr);
}

TEST_F(LuaOutputTest, OutputPrintsMessagesWithHeadersAndFlushes) {
    const auto* vt = registry_.FindOutput("lua");
    void* handle = Create(vt, {{"script", R"(
        local n = 0
        function write(msg)
          n = n + 1
          print(msg.body, msg.headers.a, msg.headers.b)
        end
        function flush() print("total", n) end)"}});
    ASSERT_NE(handle, nullptr);
    Message m = MakeMessage("hello", {{"a", "1"}, {"b", "2"}});
    MessageView view(m);

    testing::internal::CaptureStdout();
    EXPECT_EQ(vt->write(handle, &view.get()), TORUSTIQ_OK);
    EXPECT_EQ(vt->write(handle, &view.get()), TORUSTIQ_OK);
    EXPECT_EQ(vt->flush(handle), TORUSTIQ_OK);
    const std::string printed = testing::internal::GetCapturedStdout();
    vt->destroy(handle);
    EXPECT_EQ(printed, "hello\t1\t2\nhello\t1\t2\ntotal\t2\n");
}

TEST_F(LuaOutputTest, OutputFlushIsOptionalAndErrorsAreReported) {
    const auto* vt = registry_.FindOutput("lua");
    void* handle = Create(vt, {{"script", "function write(msg) error('fail') end"}});
    ASSERT_NE(handle, nullptr);
    MessageView view(MakeMessage("x"));
    EXPECT_EQ(vt->flush(handle), TORUSTIQ_OK);
    EXPECT_EQ(vt->write(handle, &view.get()), TORUSTIQ_ERROR);
    vt->destroy(handle);
    EXPECT_EQ(Create(vt, {{"script", "function process() end"}}), nullptr); // needs write()
}

} // namespace
} // namespace torustiq
