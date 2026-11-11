#include <gtest/gtest.h>

#include "core/message.h"

namespace torustiq {
namespace {

// {"user": {"name": "bob", "age": 7}, "tags": ["a", true, null, 1.5], "n": null}
Value Sample() {
    return Value(
        Value::Map{{"user", Value(Value::Map{{"name", Value("bob")}, {"age", Value(int64_t{7})}})},
                   {"tags", Value(Value::Array{Value("a"), Value(true), Value(), Value(1.5)})},
                   {"n", Value()}});
}

} // namespace

TEST(MessageTest, RoundTripsBodyWithEmbeddedNulAndHeaders) {
    Message original;
    original.payload = Value(Bytes{'a', 0, 'b'});
    original.headers = {{"k1", "v1"}, {"k2", "v2"}};

    MessageView view(original);
    EXPECT_EQ(view.get().payload.kind, TORUSTIQ_VALUE_BYTES);
    EXPECT_EQ(view.get().payload.as.bytes.len, 3u);
    EXPECT_EQ(view.get().header_count, 2u);
    EXPECT_EQ(MessageFromC(view.get()), original);
}

TEST(MessageTest, EmptyMessage) {
    Message empty;
    MessageView view(empty);
    EXPECT_EQ(view.get().payload.kind, TORUSTIQ_VALUE_BYTES);
    EXPECT_EQ(view.get().payload.as.bytes.len, 0u);
    EXPECT_EQ(view.get().header_count, 0u);
    EXPECT_EQ(MessageFromC(view.get()), empty);
}

TEST(MessageTest, FromCCopiesData) {
    std::vector<uint8_t> bytes = {1, 2, 3};
    torustiq_message raw{};
    raw.payload = torustiq_value_from_bytes(bytes.data(), bytes.size());
    Message copy = MessageFromC(raw);
    bytes[0] = 99;
    EXPECT_EQ(std::get<Bytes>(copy.payload.data)[0], 1);
}

TEST(MessageTest, FromCSkipsNullKeyAndToleratesNullValue) {
    const torustiq_header headers[] = {{nullptr, "x"}, {"k", nullptr}};
    torustiq_message raw{};
    raw.payload = torustiq_value_from_bytes(nullptr, 0);
    raw.headers = headers;
    raw.header_count = 2;
    Message m = MessageFromC(raw);
    ASSERT_EQ(m.headers.size(), 1u);
    EXPECT_EQ(m.headers["k"], "");
}

TEST(MessageTest, StructuredPayloadRoundTrips) {
    Message original;
    original.payload = Sample();
    MessageView view(original);
    EXPECT_EQ(view.get().payload.kind, TORUSTIQ_VALUE_MAP);
    EXPECT_EQ(MessageFromC(view.get()), original);
}

TEST(MessageTest, SdkHelpersReadAViewOfAStructuredPayload) {
    Message message;
    message.payload = Sample();
    MessageView view(message);
    const torustiq_value* name =
        torustiq_value_map_get(torustiq_value_map_get(&view.get().payload, "user"), "name");
    ASSERT_NE(name, nullptr);
    EXPECT_EQ(name->kind, TORUSTIQ_VALUE_STRING);
    EXPECT_EQ(std::string(reinterpret_cast<const char*>(name->as.bytes.data), name->as.bytes.len),
              "bob");
    const torustiq_value* tag =
        torustiq_value_array_at(torustiq_value_map_get(&view.get().payload, "tags"), 3);
    ASSERT_NE(tag, nullptr);
    EXPECT_EQ(tag->as.d, 1.5);
}

TEST(MessageTest, ScalarPayloadsAreStructured) {
    for (const Value& value : {Value(), Value(true), Value(int64_t{-5}), Value(2.5), Value("s")}) {
        Message message;
        message.payload = value;
        MessageView view(message);
        EXPECT_EQ(PayloadKindOf(view.get().payload), TORUSTIQ_PAYLOAD_VALUE);
        EXPECT_EQ(MessageFromC(view.get()).payload, value);
    }
    EXPECT_EQ(PayloadKindOf(Value(Bytes{})), TORUSTIQ_PAYLOAD_BYTES);
}

TEST(MessageTest, ValueFindAndEquality) {
    Value value = Sample();
    ASSERT_NE(value.Find("user"), nullptr);
    EXPECT_EQ(value.Find("missing"), nullptr);
    EXPECT_EQ(Value("x").Find("user"), nullptr);
    value.Find("user")->Find("name")->data = std::string("alice");
    EXPECT_NE(value, Sample());
}

TEST(MessageTest, FromCRejectsMalformedPayloads) {
    torustiq_message raw{};
    raw.payload.kind = 99;
    EXPECT_THROW(MessageFromC(raw), std::invalid_argument);

    raw.payload.kind = TORUSTIQ_VALUE_BYTES;
    raw.payload.as.bytes.data = nullptr;
    raw.payload.as.bytes.len = 4;
    EXPECT_THROW(MessageFromC(raw), std::invalid_argument);

    raw.payload.kind = TORUSTIQ_VALUE_ARRAY;
    raw.payload.as.array.items = nullptr;
    raw.payload.as.array.count = 1;
    EXPECT_THROW(MessageFromC(raw), std::invalid_argument);

    // A tree nested beyond the limit (each level an array holding the previous one).
    std::vector<torustiq_value> levels(kMaxValueDepth + 3);
    levels[0].kind = TORUSTIQ_VALUE_NULL;
    for (size_t i = 1; i < levels.size(); ++i) {
        levels[i].kind = TORUSTIQ_VALUE_ARRAY;
        levels[i].as.array.items = &levels[i - 1];
        levels[i].as.array.count = 1;
    }
    raw.payload = levels.back();
    EXPECT_THROW(MessageFromC(raw), std::invalid_argument);
}

TEST(ConfigViewTest, LookupViaSdkHelper) {
    std::map<std::string, std::string> values = {{"user", "bob"}, {"pass", "x"}};
    ConfigView view(values);
    EXPECT_STREQ(torustiq_config_get(&view.get(), "user"), "bob");
    EXPECT_EQ(torustiq_config_get(&view.get(), "missing"), nullptr);
}

} // namespace torustiq
