#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "core/message.h"
#include "core/plugin_registry.h"
#include "plugins/transforms/json_transforms.h"

namespace torustiq {
namespace {

int32_t Collect(void* user_data, const torustiq_message* m) {
    static_cast<std::vector<Message>*>(user_data)->push_back(MessageFromC(*m));
    return TORUSTIQ_OK;
}

class JsonTransformsTest : public ::testing::Test {
  protected:
    void SetUp() override {
        const torustiq_registrar registrar = registry_.registrar();
        ASSERT_EQ(torustiq_register_json_transforms(&registrar), TORUSTIQ_OK);
        decode_ = registry_.FindTransform("decode_json");
        encode_ = registry_.FindTransform("encode_json");
        ASSERT_NE(decode_, nullptr);
        ASSERT_NE(encode_, nullptr);
    }

    // Runs one message through `vt`; returns the process status.
    int32_t Run(const torustiq_transform_vtable* vt,
                uint32_t input_kind,
                const Message& in,
                std::vector<Message>& out) {
        const torustiq_config config{nullptr, 0};
        const torustiq_payload_spec input{input_kind};
        torustiq_payload_spec output{};
        void* handle = nullptr;
        EXPECT_EQ(vt->create(&config, &input, &output, &handle), TORUSTIQ_OK);
        MessageView view(in);
        const int32_t status = vt->process(handle, &view.get(), Collect, &out);
        vt->destroy(handle);
        return status;
    }

    int32_t Decode(const std::string& text, std::vector<Message>& out) {
        return Run(decode_, TORUSTIQ_PAYLOAD_BYTES, MakeBytesMessage(text), out);
    }

    PluginRegistry registry_;
    const torustiq_transform_vtable* decode_ = nullptr;
    const torustiq_transform_vtable* encode_ = nullptr;
};

TEST_F(JsonTransformsTest, DecodeBuildsStructuredValue) {
    std::vector<Message> out;
    Message in =
        MakeBytesMessage(R"({"b":[1,2.5,"x",true,null],"a":{"k":-3},"big":18446744073709551615})");
    in.headers["h"] = "v";
    ASSERT_EQ(Run(decode_, TORUSTIQ_PAYLOAD_BYTES, in, out), TORUSTIQ_OK);
    ASSERT_EQ(out.size(), 1u);
    EXPECT_EQ(out[0].headers, in.headers);
    const Value expected(Value::Map{
        {"b", Value(Value::Array{Value(int64_t{1}), Value(2.5), Value("x"), Value(true), Value()})},
        {"a", Value(Value::Map{{"k", Value(int64_t{-3})}})},
        {"big", Value(18446744073709551615.0)}});
    EXPECT_EQ(out[0].payload, expected); // member order is kept
}

TEST_F(JsonTransformsTest, EncodeRoundTrips) {
    const std::string text = R"({"z":1,"a":[true,null,"é\n",1.5],"o":{}})";
    std::vector<Message> decoded;
    ASSERT_EQ(Decode(text, decoded), TORUSTIQ_OK);
    std::vector<Message> encoded;
    ASSERT_EQ(Run(encode_, TORUSTIQ_PAYLOAD_VALUE, decoded.at(0), encoded), TORUSTIQ_OK);
    ASSERT_EQ(encoded.size(), 1u);
    EXPECT_EQ(BodyText(encoded[0]), R"({"z":1,"a":[true,null,"é\n",1.5],"o":{}})");
}

TEST_F(JsonTransformsTest, ScalarDocuments) {
    std::vector<Message> out;
    ASSERT_EQ(Decode("42", out), TORUSTIQ_OK);
    EXPECT_EQ(out.at(0).payload, Value(int64_t{42}));
    out.clear();
    ASSERT_EQ(Decode("\"text\"", out), TORUSTIQ_OK);
    EXPECT_EQ(out.at(0).payload, Value("text"));
}

TEST_F(JsonTransformsTest, InvalidDocumentFails) {
    std::vector<Message> out;
    EXPECT_EQ(Decode("{not json", out), TORUSTIQ_ERROR);
    EXPECT_EQ(Decode("", out), TORUSTIQ_ERROR);
    EXPECT_TRUE(out.empty());
}

TEST_F(JsonTransformsTest, BytesMemberCannotBeEncoded) {
    Message in;
    in.payload = Value(Value::Map{{"raw", Value(Bytes{1, 2})}});
    std::vector<Message> out;
    EXPECT_EQ(Run(encode_, TORUSTIQ_PAYLOAD_VALUE, in, out), TORUSTIQ_ERROR);
}

TEST_F(JsonTransformsTest, DecodesTextInput) {
    std::vector<Message> out;
    ASSERT_EQ(Run(decode_, TORUSTIQ_PAYLOAD_TEXT, MakeTextMessage(R"({"a":1})"), out), TORUSTIQ_OK);
    ASSERT_EQ(out.size(), 1u);
    EXPECT_EQ(out[0].payload, Value(Value::Map{{"a", Value(int64_t{1})}}));
}

TEST_F(JsonTransformsTest, RejectsWrongInputKind) {
    const torustiq_config config{nullptr, 0};
    torustiq_payload_spec output{};
    void* handle = nullptr;
    const torustiq_payload_spec bytes{TORUSTIQ_PAYLOAD_BYTES};
    const torustiq_payload_spec value{TORUSTIQ_PAYLOAD_VALUE};
    EXPECT_EQ(decode_->create(&config, &value, &output, &handle), TORUSTIQ_UNSUPPORTED_PAYLOAD);
    EXPECT_EQ(encode_->create(&config, &bytes, &output, &handle), TORUSTIQ_UNSUPPORTED_PAYLOAD);
}

TEST_F(JsonTransformsTest, DeclaresOutputKinds) {
    const torustiq_config config{nullptr, 0};
    torustiq_payload_spec output{};
    void* handle = nullptr;
    const torustiq_payload_spec bytes{TORUSTIQ_PAYLOAD_BYTES};
    const torustiq_payload_spec value{TORUSTIQ_PAYLOAD_VALUE};
    ASSERT_EQ(decode_->create(&config, &bytes, &output, &handle), TORUSTIQ_OK);
    EXPECT_EQ(output.kind, TORUSTIQ_PAYLOAD_VALUE);
    ASSERT_EQ(encode_->create(&config, &value, &output, &handle), TORUSTIQ_OK);
    EXPECT_EQ(output.kind, TORUSTIQ_PAYLOAD_BYTES);
}

} // namespace
} // namespace torustiq
