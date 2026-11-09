#include <gtest/gtest.h>

#include "core/message.h"

namespace torustiq {

TEST(MessageTest, RoundTripsBodyWithEmbeddedNulAndHeaders) {
    Message original;
    original.body = {'a', 0, 'b'};
    original.headers = {{"k1", "v1"}, {"k2", "v2"}};

    MessageView view(original);
    EXPECT_EQ(view.get().body.len, 3u);
    EXPECT_EQ(view.get().header_count, 2u);
    EXPECT_EQ(MessageFromC(view.get()), original);
}

TEST(MessageTest, EmptyMessage) {
    Message empty;
    MessageView view(empty);
    EXPECT_EQ(view.get().body.len, 0u);
    EXPECT_EQ(view.get().header_count, 0u);
    EXPECT_EQ(MessageFromC(view.get()), empty);
}

TEST(MessageTest, FromCCopiesData) {
    std::vector<uint8_t> bytes = {1, 2, 3};
    torustiq_message raw{};
    raw.body.data = bytes.data();
    raw.body.len = bytes.size();
    Message copy = MessageFromC(raw);
    bytes[0] = 99;
    EXPECT_EQ(copy.body[0], 1);
}

TEST(MessageTest, FromCSkipsNullKeyAndToleratesNullValue) {
    const torustiq_header headers[] = {{nullptr, "x"}, {"k", nullptr}};
    torustiq_message raw{};
    raw.headers = headers;
    raw.header_count = 2;
    Message m = MessageFromC(raw);
    ASSERT_EQ(m.headers.size(), 1u);
    EXPECT_EQ(m.headers["k"], "");
}

TEST(ConfigViewTest, LookupViaSdkHelper) {
    std::map<std::string, std::string> values = {{"user", "bob"}, {"pass", "x"}};
    ConfigView view(values);
    EXPECT_STREQ(torustiq_config_get(&view.get(), "user"), "bob");
    EXPECT_EQ(torustiq_config_get(&view.get(), "missing"), nullptr);
}

} // namespace torustiq
