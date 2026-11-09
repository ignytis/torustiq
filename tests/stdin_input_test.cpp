#include <iostream>
#include <sstream>

#include <gtest/gtest.h>

#include "core/message.h"
#include "core/plugin_registry.h"
#include "plugins/stdin_input.h"

namespace torustiq {
namespace {

int32_t Collect(void* user_data, const torustiq_message* m) {
    static_cast<std::vector<Message>*>(user_data)->push_back(MessageFromC(*m));
    return TORUSTIQ_OK;
}

std::vector<std::string> ReadAll(const std::string& input) {
    PluginRegistry registry;
    const torustiq_registrar registrar = registry.registrar();
    EXPECT_EQ(torustiq_register_stdin_input(&registrar), TORUSTIQ_OK);
    const auto* vt = registry.FindInput("stdin");
    void* handle = nullptr;
    EXPECT_EQ(vt->create(nullptr, &handle), TORUSTIQ_OK);

    std::istringstream in(input);
    std::streambuf* old = std::cin.rdbuf(in.rdbuf());
    std::vector<Message> messages;
    EXPECT_EQ(vt->run(handle, Collect, &messages), TORUSTIQ_OK);
    std::cin.rdbuf(old);
    vt->destroy(handle);

    std::vector<std::string> lines;
    for (const Message& m : messages) {
        lines.emplace_back(m.body.begin(), m.body.end());
    }
    return lines;
}

} // namespace

TEST(StdinInputTest, EachLineIsARecord) {
    EXPECT_EQ(ReadAll("a\nb\n\nc"), (std::vector<std::string>{"a", "b", "", "c"}));
}

TEST(StdinInputTest, EmptyInputProducesNothing) {
    EXPECT_TRUE(ReadAll("").empty());
}

TEST(StdinInputTest, StopBeforeRunProducesNothing) {
    PluginRegistry registry;
    const torustiq_registrar registrar = registry.registrar();
    torustiq_register_stdin_input(&registrar);
    const auto* vt = registry.FindInput("stdin");
    void* handle = nullptr;
    vt->create(nullptr, &handle);
    vt->stop(handle);
    std::istringstream in("a\nb\n");
    std::streambuf* old = std::cin.rdbuf(in.rdbuf());
    std::vector<Message> messages;
    EXPECT_EQ(vt->run(handle, Collect, &messages), TORUSTIQ_OK);
    std::cin.rdbuf(old);
    vt->destroy(handle);
    EXPECT_TRUE(messages.empty());
}

} // namespace torustiq
