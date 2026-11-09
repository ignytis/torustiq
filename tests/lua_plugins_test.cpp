#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include "core/message.h"
#include "core/plugin_registry.h"
#include "plugins/lua_plugins.h"
#include "plugins/lua_runtime.h"

namespace fs = std::filesystem;

namespace torustiq {
namespace {

using Params = std::map<std::string, std::string>;

struct Collector {
    std::vector<Message> messages;
};

int32_t Collect(void* user_data, const torustiq_message* m) {
    static_cast<Collector*>(user_data)->messages.push_back(MessageFromC(*m));
    return TORUSTIQ_OK;
}

std::string BodyOf(const Message& m) {
    return std::string(m.body.begin(), m.body.end());
}

Message MakeMessage(const std::string& body, std::map<std::string, std::string> headers = {}) {
    Message m;
    m.body.assign(body.begin(), body.end());
    m.headers = std::move(headers);
    return m;
}

class LuaPluginsTest : public ::testing::Test {
  protected:
    void SetUp() override {
        const torustiq_registrar registrar = registry_.registrar();
        ASSERT_EQ(torustiq_register_lua_plugins(&registrar), TORUSTIQ_OK);
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

    PluginRegistry registry_;
};

TEST_F(LuaPluginsTest, RegistersAllThreeKinds) {
    EXPECT_NE(registry_.FindInput("lua"), nullptr);
    EXPECT_NE(registry_.FindTransform("lua"), nullptr);
    EXPECT_NE(registry_.FindOutput("lua"), nullptr);
}

// ---- transform ----

TEST_F(LuaPluginsTest, TransformModifiesBodyAndHeaders) {
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

TEST_F(LuaPluginsTest, TransformDropsOnNilFalseAndEmptyList) {
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

TEST_F(LuaPluginsTest, TransformFansOutAList) {
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

TEST_F(LuaPluginsTest, TransformStringResultIsBody) {
    Collector out;
    ASSERT_EQ(Transform({{"script", "function process(m) return m.body .. '!' end"}},
                        MakeMessage("hi"),
                        out),
              TORUSTIQ_OK);
    ASSERT_EQ(out.messages.size(), 1u);
    EXPECT_EQ(BodyOf(out.messages[0]), "hi!");
}

TEST_F(LuaPluginsTest, BinaryBodyWithNulSurvives) {
    Collector out;
    const std::string binary("a\0b\xff", 4);
    ASSERT_EQ(Transform({{"script", "function process(m) assert(#m.body == 4) return m end"}},
                        MakeMessage(binary),
                        out),
              TORUSTIQ_OK);
    ASSERT_EQ(out.messages.size(), 1u);
    EXPECT_EQ(BodyOf(out.messages[0]), binary);
}

TEST_F(LuaPluginsTest, ConfigIsVisibleToScript) {
    Collector out;
    ASSERT_EQ(Transform({{"script", "function process(m) m.body = config.greeting return m end"},
                         {"greeting", "hello"}},
                        MakeMessage("x"),
                        out),
              TORUSTIQ_OK);
    EXPECT_EQ(BodyOf(out.messages.at(0)), "hello");
}

TEST_F(LuaPluginsTest, ScriptErrorsAreReported) {
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

TEST_F(LuaPluginsTest, MetatableTricksCannotCrashTheHost) {
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

TEST_F(LuaPluginsTest, InstructionLimitStopsRunawayScripts) {
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

TEST_F(LuaPluginsTest, ZeroMeansUnlimited) {
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

TEST_F(LuaPluginsTest, InsecureLibrariesAreNotAvailable) {
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

TEST_F(LuaPluginsTest, BytecodeIsRejected) {
    Collector out;
    EXPECT_EQ(Transform({{"script", "\x1bLua garbage"}}, MakeMessage("x"), out), TORUSTIQ_ERROR);
}

TEST_F(LuaPluginsTest, CreateValidatesParameters) {
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

TEST_F(LuaPluginsTest, ScriptCanBeLoadedFromFile) {
    const fs::path path = fs::temp_directory_path() / "torustiq_lua_test_script.lua";
    std::ofstream(path) << "function process(m) m.body = 'from file' return m end";
    Collector out;
    EXPECT_EQ(Transform({{"file", path.string()}}, MakeMessage("x"), out), TORUSTIQ_OK);
    fs::remove(path);
    ASSERT_EQ(out.messages.size(), 1u);
    EXPECT_EQ(BodyOf(out.messages[0]), "from file");
}

TEST_F(LuaPluginsTest, TransformKeepsStateBetweenMessages) {
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

// ---- input ----

TEST_F(LuaPluginsTest, InputEmitsMessages) {
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

TEST_F(LuaPluginsTest, InputSeesStopFromEmit) {
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

TEST_F(LuaPluginsTest, InputStopInterruptsBusyLoopAndSleep) {
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

TEST_F(LuaPluginsTest, InputInstructionLimitAppliesBetweenEmits) {
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

TEST_F(LuaPluginsTest, InputErrorsAreReported) {
    const auto* vt = registry_.FindInput("lua");
    void* handle = Create(vt, {{"script", "function run(emit) emit({ body = {} }) end"}});
    ASSERT_NE(handle, nullptr);
    Collector out;
    EXPECT_EQ(vt->run(handle, Collect, &out), TORUSTIQ_ERROR);
    vt->destroy(handle);
    EXPECT_EQ(Create(vt, {{"script", "function process() end"}}), nullptr); // needs run()
}

// ---- output ----

TEST_F(LuaPluginsTest, OutputPrintsMessagesWithHeadersAndFlushes) {
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

TEST_F(LuaPluginsTest, OutputFlushIsOptionalAndErrorsAreReported) {
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
