#include <iostream>
#include <sstream>

#include <gtest/gtest.h>

#include "core/message.h"
#include "core/plugin_registry.h"
#include "plugins/outputs/stdout_output.h"

namespace torustiq {
namespace {

Message Sample() {
    Message m;
    m.payload = Value(Bytes{'h', 'i'});
    m.headers = {{"some_header", "v"}};
    return m;
}

std::string Format(const std::string& format, const Message& message) {
    MessageView view(message);
    return plugins::FormatMessage(format, view.get());
}

} // namespace

TEST(FormatMessageTest, Placeholders) {
    EXPECT_EQ(Format("%BODY% / %HEADERS[some_header]%", Sample()), "hi / v");
}

TEST(FormatMessageTest, MissingHeaderIsEmpty) {
    EXPECT_EQ(Format("[%HEADERS[nope]%]", Sample()), "[]");
}

TEST(FormatMessageTest, LiteralsAndMalformedPlaceholders) {
    EXPECT_EQ(Format("100% %HEADERS[x", Sample()), "100% %HEADERS[x");
    EXPECT_EQ(Format("", Sample()), "");
}

TEST(FormatMessageTest, BodyWithEmbeddedNul) {
    Message m;
    m.payload = Value(Bytes{'a', 0, 'b'});
    EXPECT_EQ(Format("%BODY%", m), std::string("a\0b", 3));
}

TEST(StdoutPluginTest, WritesFormattedLines) {
    PluginRegistry registry;
    const torustiq_registrar registrar = registry.registrar();
    ASSERT_EQ(torustiq_register_stdout_output(&registrar), TORUSTIQ_OK);
    const auto* vt = registry.FindOutput("stdout");
    ASSERT_NE(vt, nullptr);

    std::map<std::string, std::string> params = {{"format", "<%BODY%>"}};
    ConfigView config(params);
    void* handle = nullptr;
    const torustiq_payload_spec input{TORUSTIQ_PAYLOAD_BYTES};
    ASSERT_EQ(vt->create(&config.get(), &input, &handle), TORUSTIQ_OK);

    std::ostringstream captured;
    std::streambuf* old = std::cout.rdbuf(captured.rdbuf());
    Message m = Sample();
    MessageView view(m);
    EXPECT_EQ(vt->write(handle, &view.get()), TORUSTIQ_OK);
    EXPECT_EQ(vt->flush(handle), TORUSTIQ_OK);
    std::cout.rdbuf(old);
    vt->destroy(handle);

    EXPECT_EQ(captured.str(), "<hi>\n");
}

TEST(StdoutPluginTest, RejectsStructuredPayload) {
    PluginRegistry registry;
    const torustiq_registrar registrar = registry.registrar();
    ASSERT_EQ(torustiq_register_stdout_output(&registrar), TORUSTIQ_OK);
    const auto* vt = registry.FindOutput("stdout");
    void* handle = nullptr;
    const torustiq_payload_spec input{TORUSTIQ_PAYLOAD_VALUE};
    EXPECT_EQ(vt->create(nullptr, &input, &handle), TORUSTIQ_UNSUPPORTED_PAYLOAD);
}

} // namespace torustiq
