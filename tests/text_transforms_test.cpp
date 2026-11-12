#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "core/message.h"
#include "core/plugin_registry.h"
#include "plugins/transforms/text_transforms.h"

namespace torustiq {
namespace {

int32_t Collect(void* user_data, const torustiq_message* m) {
    static_cast<std::vector<Message>*>(user_data)->push_back(MessageFromC(*m));
    return TORUSTIQ_OK;
}

class TextTransformsTest : public ::testing::Test {
  protected:
    void SetUp() override {
        const torustiq_registrar registrar = registry_.registrar();
        ASSERT_EQ(torustiq_register_text_transforms(&registrar), TORUSTIQ_OK);
        decode_ = registry_.FindTransform("decode_text");
        encode_ = registry_.FindTransform("encode_text");
        ASSERT_NE(decode_, nullptr);
        ASSERT_NE(encode_, nullptr);
    }

    // Creates a step with optional `invalid` config and runs one message; returns the status.
    int32_t Run(const torustiq_transform_vtable* vt,
                uint32_t input_kind,
                const Message& in,
                std::vector<Message>& out,
                const char* invalid = nullptr) {
        std::map<std::string, std::string> params;
        if (invalid != nullptr) {
            params["invalid"] = invalid;
        }
        ConfigView config(params);
        const torustiq_payload_spec input{input_kind};
        torustiq_payload_spec output{};
        void* handle = nullptr;
        EXPECT_EQ(vt->create(&config.get(), &input, &output, &handle), TORUSTIQ_OK);
        MessageView view(in);
        const int32_t status = vt->process(handle, &view.get(), Collect, &out);
        vt->destroy(handle);
        return status;
    }

    PluginRegistry registry_;
    const torustiq_transform_vtable* decode_ = nullptr;
    const torustiq_transform_vtable* encode_ = nullptr;
};

TEST_F(TextTransformsTest, DecodeProducesTextAndKeepsHeaders) {
    Message in = MakeBytesMessage("h\xC3\xA9llo \xE2\x82\xAC \xF0\x9F\x98\x80");
    in.headers["h"] = "v";
    std::vector<Message> out;
    ASSERT_EQ(Run(decode_, TORUSTIQ_PAYLOAD_BYTES, in, out), TORUSTIQ_OK);
    ASSERT_EQ(out.size(), 1u);
    EXPECT_EQ(PayloadKindOf(out[0].payload), TORUSTIQ_PAYLOAD_TEXT);
    EXPECT_EQ(TextBody(out[0]), "h\xC3\xA9llo \xE2\x82\xAC \xF0\x9F\x98\x80");
    EXPECT_EQ(out[0].headers, in.headers);
}

TEST_F(TextTransformsTest, DecodeEmptyInput) {
    std::vector<Message> out;
    ASSERT_EQ(Run(decode_, TORUSTIQ_PAYLOAD_BYTES, MakeBytesMessage(""), out), TORUSTIQ_OK);
    ASSERT_EQ(out.size(), 1u);
    EXPECT_EQ(PayloadKindOf(out[0].payload), TORUSTIQ_PAYLOAD_TEXT);
    EXPECT_EQ(TextBody(out[0]), "");
}

TEST_F(TextTransformsTest, InvalidUtf8FailsByDefault) {
    const std::vector<std::string> bad = {
        "\xFF",             // never valid
        "ab\x80",           // stray continuation
        "\xC3",             // truncated
        "\xC0\xAF",         // overlong
        "\xED\xA0\x80",     // surrogate
        "\xF4\x90\x80\x80", // above U+10FFFF
    };
    for (const std::string& data : bad) {
        std::vector<Message> out;
        EXPECT_EQ(Run(decode_, TORUSTIQ_PAYLOAD_BYTES, MakeBytesMessage(data), out), TORUSTIQ_ERROR)
            << data;
        EXPECT_TRUE(out.empty());
    }
}

TEST_F(TextTransformsTest, InvalidUtf8CanBeReplaced) {
    std::vector<Message> out;
    ASSERT_EQ(Run(decode_,
                  TORUSTIQ_PAYLOAD_BYTES,
                  MakeBytesMessage("a\xFF"
                                   "b\xC3"),
                  out,
                  "replace"),
              TORUSTIQ_OK);
    ASSERT_EQ(out.size(), 1u);
    EXPECT_EQ(TextBody(out[0]),
              "a\xEF\xBF\xBD"
              "b\xEF\xBF\xBD");
}

TEST_F(TextTransformsTest, RejectsUnknownInvalidMode) {
    std::map<std::string, std::string> params{{"invalid", "ignore"}};
    ConfigView config(params);
    const torustiq_payload_spec input{TORUSTIQ_PAYLOAD_BYTES};
    torustiq_payload_spec output{};
    void* handle = nullptr;
    EXPECT_EQ(decode_->create(&config.get(), &input, &output, &handle), TORUSTIQ_ERROR);
}

TEST_F(TextTransformsTest, EncodeProducesBytes) {
    Message in = MakeTextMessage("caf\xC3\xA9");
    in.headers["k"] = "v";
    std::vector<Message> out;
    ASSERT_EQ(Run(encode_, TORUSTIQ_PAYLOAD_TEXT, in, out), TORUSTIQ_OK);
    ASSERT_EQ(out.size(), 1u);
    EXPECT_EQ(PayloadKindOf(out[0].payload), TORUSTIQ_PAYLOAD_BYTES);
    EXPECT_EQ(BodyText(out[0]), "caf\xC3\xA9");
    EXPECT_EQ(out[0].headers, in.headers);
}

TEST_F(TextTransformsTest, RejectsWrongInputKind) {
    const torustiq_config config{nullptr, 0};
    torustiq_payload_spec output{};
    void* handle = nullptr;
    const torustiq_payload_spec bytes{TORUSTIQ_PAYLOAD_BYTES};
    const torustiq_payload_spec value{TORUSTIQ_PAYLOAD_VALUE};
    const torustiq_payload_spec text{TORUSTIQ_PAYLOAD_TEXT};
    EXPECT_EQ(decode_->create(&config, &text, &output, &handle), TORUSTIQ_UNSUPPORTED_PAYLOAD);
    EXPECT_EQ(decode_->create(&config, &value, &output, &handle), TORUSTIQ_UNSUPPORTED_PAYLOAD);
    EXPECT_EQ(encode_->create(&config, &bytes, &output, &handle), TORUSTIQ_UNSUPPORTED_PAYLOAD);
    EXPECT_EQ(encode_->create(&config, &value, &output, &handle), TORUSTIQ_UNSUPPORTED_PAYLOAD);
}

TEST_F(TextTransformsTest, DeclaresOutputKinds) {
    const torustiq_config config{nullptr, 0};
    torustiq_payload_spec output{};
    void* handle = nullptr;
    const torustiq_payload_spec bytes{TORUSTIQ_PAYLOAD_BYTES};
    const torustiq_payload_spec text{TORUSTIQ_PAYLOAD_TEXT};
    ASSERT_EQ(decode_->create(&config, &bytes, &output, &handle), TORUSTIQ_OK);
    EXPECT_EQ(output.kind, TORUSTIQ_PAYLOAD_TEXT);
    decode_->destroy(handle);
    ASSERT_EQ(encode_->create(&config, &text, &output, &handle), TORUSTIQ_OK);
    EXPECT_EQ(output.kind, TORUSTIQ_PAYLOAD_BYTES);
}

} // namespace
} // namespace torustiq
