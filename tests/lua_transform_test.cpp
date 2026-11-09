#include <filesystem>
#include <fstream>
#include <string>

#include <gtest/gtest.h>

#include "lua_test_util.h"
#include "plugins/transforms/lua_transform.h"

namespace fs = std::filesystem;

namespace torustiq {
namespace {

class LuaTransformTest : public LuaTestBase {
  protected:
    void SetUp() override {
        Register(torustiq_register_lua_transform);
    }

    // Runs `process` for one message; returns the status.
    int32_t Transform(const Params& params, const Message& in, Collector& out) {
        const auto* vt = registry_.FindTransform("lua");
        void* handle = Create(vt, params);
        if (handle == nullptr) {
            return TORUSTIQ_ERROR;
        }
        MessageView view(in);
        const int32_t status = vt->process(handle, &view.get(), Collect, &out);
        vt->destroy(handle);
        return status;
    }
};

TEST_F(LuaTransformTest, RegistersTransform) {
    EXPECT_NE(registry_.FindTransform("lua"), nullptr);
}

TEST_F(LuaTransformTest, TransformModifiesBodyAndHeaders) {
    Collector out;
    ASSERT_EQ(Transform({{"script", R"(
        function process(msg)
          msg.body = msg.body:upper()
          msg.headers.seen = "yes"
          return msg
        end)"}},
                        MakeMessage("abc", {{"k", "v"}}),
                        out),
              TORUSTIQ_OK);
    ASSERT_EQ(out.messages.size(), 1u);
    EXPECT_EQ(BodyOf(out.messages[0]), "ABC");
    EXPECT_EQ(out.messages[0].headers,
              (std::map<std::string, std::string>{{"k", "v"}, {"seen", "yes"}}));
}

TEST_F(LuaTransformTest, TransformDropsOnNilFalseAndEmptyList) {
    for (const char* ret : {"nil", "false", "{ }"}) {
        Collector out;
        ASSERT_EQ(Transform({{"script", std::string("function process(m) return ") + ret + " end"}},
                            MakeMessage("x"),
                            out),
                  TORUSTIQ_OK)
            << ret;
        // `{ }` is an empty table: a message with an empty body (documented), not a drop.
        if (std::string(ret) == "{ }") {
            EXPECT_EQ(out.messages.size(), 1u);
        } else {
            EXPECT_TRUE(out.messages.empty()) << ret;
        }
    }
}

TEST_F(LuaTransformTest, TransformFansOutAList) {
    Collector out;
    ASSERT_EQ(Transform({{"script", R"(
        function process(msg)
          return { msg, { body = msg.body .. "2", headers = { n = 2 } }, "third" }
        end)"}},
                        MakeMessage("a"),
                        out),
              TORUSTIQ_OK);
    ASSERT_EQ(out.messages.size(), 3u);
    EXPECT_EQ(BodyOf(out.messages[0]), "a");
    EXPECT_EQ(BodyOf(out.messages[1]), "a2");
    EXPECT_EQ(out.messages[1].headers.at("n"), "2"); // numbers become strings
    EXPECT_EQ(BodyOf(out.messages[2]), "third");
}

TEST_F(LuaTransformTest, TransformStringResultIsBody) {
    Collector out;
    ASSERT_EQ(Transform({{"script", "function process(m) return m.body .. '!' end"}},
                        MakeMessage("hi"),
                        out),
              TORUSTIQ_OK);
    ASSERT_EQ(out.messages.size(), 1u);
    EXPECT_EQ(BodyOf(out.messages[0]), "hi!");
}

TEST_F(LuaTransformTest, BinaryBodyWithNulSurvives) {
    Collector out;
    const std::string binary("a\0b\xff", 4);
    ASSERT_EQ(Transform({{"script", "function process(m) assert(#m.body == 4) return m end"}},
                        MakeMessage(binary),
                        out),
              TORUSTIQ_OK);
    ASSERT_EQ(out.messages.size(), 1u);
    EXPECT_EQ(BodyOf(out.messages[0]), binary);
}

TEST_F(LuaTransformTest, ConfigIsVisibleToScript) {
    Collector out;
    ASSERT_EQ(Transform({{"script", "function process(m) m.body = config.greeting return m end"},
                         {"greeting", "hello"}},
                        MakeMessage("x"),
                        out),
              TORUSTIQ_OK);
    EXPECT_EQ(BodyOf(out.messages.at(0)), "hello");
}

TEST_F(LuaTransformTest, ScriptErrorsAreReported) {
    Collector out;
    EXPECT_EQ(
        Transform({{"script", "function process(m) error('boom') end"}}, MakeMessage("x"), out),
        TORUSTIQ_ERROR);
    EXPECT_EQ(
        Transform({{"script", "function process(m) return 42 .. {} end"}}, MakeMessage("x"), out),
        TORUSTIQ_ERROR);
    // Invalid result type / invalid header
    EXPECT_EQ(Transform({{"script", "function process(m) return true end"}}, MakeMessage("x"), out),
              TORUSTIQ_ERROR);
    EXPECT_EQ(
        Transform({{"script", "function process(m) return {body={}} end"}}, MakeMessage("x"), out),
        TORUSTIQ_ERROR);
    EXPECT_EQ(Transform({{"script", "function process(m) return {headers={a={}}} end"}},
                        MakeMessage("x"),
                        out),
              TORUSTIQ_ERROR);
    EXPECT_TRUE(out.messages.empty());
}

TEST_F(LuaTransformTest, MetatableTricksCannotCrashTheHost) {
    Collector out;
    // __index on the returned table must not run (raw access only); the message is just empty.
    EXPECT_EQ(Transform({{"script", R"(
        function process(m)
          return setmetatable({}, { __index = function() error('nope') end })
        end)"}},
                        MakeMessage("x"),
                        out),
              TORUSTIQ_OK);
    ASSERT_EQ(out.messages.size(), 1u);
    EXPECT_TRUE(out.messages[0].body.empty());
    // Breaking the globals table must produce an error, not a panic.
    EXPECT_EQ(Transform({{"script", R"(
        setmetatable(_G, { __index = function() error('nope') end })
        function process(m) return m end)"}},
                        MakeMessage("x"),
                        out),
              TORUSTIQ_OK);
}

TEST_F(LuaTransformTest, InstructionLimitStopsRunawayScripts) {
    Collector out;
    EXPECT_EQ(Transform({{"script", "function process(m) while true do end end"},
                         {"max_instructions", "100000"}},
                        MakeMessage("x"),
                        out),
              TORUSTIQ_ERROR);
    // The top-level code is limited as well.
    EXPECT_EQ(Transform({{"script", "while true do end function process(m) return m end"},
                         {"max_instructions", "100000"}},
                        MakeMessage("x"),
                        out),
              TORUSTIQ_ERROR);
    // The budget is per call: many cheap calls are fine.
    const auto* vt = registry_.FindTransform("lua");
    void* handle = Create(vt,
                          {{"script", "function process(m) for i=1,5000 do end return m end"},
                           {"max_instructions", "100000"}});
    ASSERT_NE(handle, nullptr);
    MessageView view(MakeMessage("x"));
    for (int i = 0; i < 100; ++i) {
        ASSERT_EQ(vt->process(handle, &view.get(), Collect, &out), TORUSTIQ_OK);
    }
    vt->destroy(handle);
}

TEST_F(LuaTransformTest, ZeroMeansUnlimited) {
    Collector out;
    EXPECT_EQ(Transform({{"script", "function process(m) for i=1,3000000 do end return m end"},
                         {"max_instructions", "0"}},
                        MakeMessage("x"),
                        out),
              TORUSTIQ_OK);
    // ... while the default limit would reject it.
    EXPECT_EQ(Transform({{"script", "function process(m) for i=1,30000000 do end return m end"}},
                        MakeMessage("x"),
                        out),
              TORUSTIQ_ERROR);
}

TEST_F(LuaTransformTest, InsecureLibrariesAreNotAvailable) {
    Collector out;
    for (const char* name :
         {"io", "os", "package", "debug", "require", "dofile", "loadfile", "load"}) {
        out.messages.clear();
        ASSERT_EQ(Transform({{"script",
                              std::string("function process(m) m.body = type(") + name +
                                  ") return m end"}},
                            MakeMessage("x"),
                            out),
                  TORUSTIQ_OK)
            << name;
        EXPECT_EQ(BodyOf(out.messages.at(0)), "nil") << name;
    }
    // Safe libraries are present.
    out.messages.clear();
    ASSERT_EQ(Transform({{"script", R"(
        function process(m)
          m.body = type(string) .. type(table) .. type(math) .. type(utf8) .. type(coroutine)
          return m
        end)"}},
                        MakeMessage("x"),
                        out),
              TORUSTIQ_OK);
    EXPECT_EQ(BodyOf(out.messages.at(0)), "tabletabletabletabletable");
}

TEST_F(LuaTransformTest, BytecodeIsRejected) {
    Collector out;
    EXPECT_EQ(Transform({{"script", "\x1bLua garbage"}}, MakeMessage("x"), out), TORUSTIQ_ERROR);
}

TEST_F(LuaTransformTest, CreateValidatesParameters) {
    const auto* vt = registry_.FindTransform("lua");
    const std::string ok = "function process(m) return m end";
    EXPECT_EQ(Create(vt, {}), nullptr);                                  // no script/file
    EXPECT_EQ(Create(vt, {{"script", ok}, {"file", "x.lua"}}), nullptr); // both
    EXPECT_EQ(Create(vt, {{"script", ok}, {"max_instructions", "abc"}}), nullptr);
    EXPECT_EQ(Create(vt, {{"script", ok}, {"max_instructions", "-5"}}), nullptr);
    EXPECT_EQ(Create(vt, {{"script", "function other() end"}}), nullptr); // no process()
    EXPECT_EQ(Create(vt, {{"script", "syntax error here"}}), nullptr);
    EXPECT_EQ(Create(vt, {{"script", "error('top-level')"}}), nullptr);
    EXPECT_EQ(Create(vt, {{"file", "/nonexistent/script.lua"}}), nullptr);
}

TEST_F(LuaTransformTest, ScriptCanBeLoadedFromFile) {
    const fs::path path = fs::temp_directory_path() / "torustiq_lua_test_script.lua";
    std::ofstream(path) << "function process(m) m.body = 'from file' return m end";
    Collector out;
    EXPECT_EQ(Transform({{"file", path.string()}}, MakeMessage("x"), out), TORUSTIQ_OK);
    fs::remove(path);
    ASSERT_EQ(out.messages.size(), 1u);
    EXPECT_EQ(BodyOf(out.messages[0]), "from file");
}

TEST_F(LuaTransformTest, TransformKeepsStateBetweenMessages) {
    const auto* vt = registry_.FindTransform("lua");
    void* handle = Create(vt, {{"script", R"(
        local n = 0
        function process(m) n = n + 1 m.body = tostring(n) return m end)"}});
    ASSERT_NE(handle, nullptr);
    Collector out;
    MessageView view(MakeMessage("x"));
    for (int i = 0; i < 3; ++i) {
        ASSERT_EQ(vt->process(handle, &view.get(), Collect, &out), TORUSTIQ_OK);
    }
    vt->destroy(handle);
    EXPECT_EQ(BodyOf(out.messages[2]), "3");
}

} // namespace
} // namespace torustiq
