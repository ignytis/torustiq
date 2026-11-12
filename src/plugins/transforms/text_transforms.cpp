#include "plugins/transforms/text_transforms.h"

#include <iostream>
#include <string>
#include <string_view>

namespace {

struct DecodeText {
    bool replace_invalid = false;
};

// Returns the length of the valid UTF-8 sequence at the start of `s` (non-empty), or 0 if the
// start is not a valid sequence (overlong forms, surrogates and values above U+10FFFF included).
size_t ValidSequenceLength(std::string_view s) {
    const auto at = [&](size_t i) { return static_cast<unsigned char>(s[i]); };
    const auto cont = [&](size_t i, unsigned lo = 0x80, unsigned hi = 0xBF) {
        return i < s.size() && at(i) >= lo && at(i) <= hi;
    };
    const unsigned c = at(0);
    if (c < 0x80) {
        return 1;
    }
    if (c >= 0xC2 && c <= 0xDF) {
        return cont(1) ? 2 : 0;
    }
    if (c >= 0xE0 && c <= 0xEF) {
        const unsigned lo = c == 0xE0 ? 0xA0 : 0x80;
        const unsigned hi = c == 0xED ? 0x9F : 0xBF;
        return cont(1, lo, hi) && cont(2) ? 3 : 0;
    }
    if (c >= 0xF0 && c <= 0xF4) {
        const unsigned lo = c == 0xF0 ? 0x90 : 0x80;
        const unsigned hi = c == 0xF4 ? 0x8F : 0xBF;
        return cont(1, lo, hi) && cont(2) && cont(3) ? 4 : 0;
    }
    return 0;
}

// Copies `s` to `out`. Returns false if it is not valid UTF-8 and `replace` is false.
bool SanitizeUtf8(std::string_view s, bool replace, std::string& out) {
    static const char kReplacement[] = "\xEF\xBF\xBD"; // U+FFFD
    out.clear();
    out.reserve(s.size());
    size_t i = 0;
    while (i < s.size()) {
        const size_t n = ValidSequenceLength(s.substr(i));
        if (n == 0) {
            if (!replace) {
                return false;
            }
            out += kReplacement;
            ++i;
            continue;
        }
        out.append(s.substr(i, n));
        i += n;
    }
    return true;
}

int32_t DecodeCreate(const torustiq_config* config,
                     const torustiq_payload_spec* input,
                     torustiq_payload_spec* out_output,
                     void** out_handle) {
    if (input->kind != TORUSTIQ_PAYLOAD_BYTES) {
        return TORUSTIQ_UNSUPPORTED_PAYLOAD;
    }
    const char* invalid = torustiq_config_get(config, "invalid");
    const std::string_view mode = invalid == nullptr ? "error" : invalid;
    if (mode != "error" && mode != "replace") {
        std::cerr << "decode_text: 'invalid' must be 'error' or 'replace', got '" << mode << "'\n";
        return TORUSTIQ_ERROR;
    }
    try {
        auto* state = new DecodeText();
        state->replace_invalid = mode == "replace";
        *out_handle = state;
    } catch (...) {
        return TORUSTIQ_ERROR;
    }
    out_output->kind = TORUSTIQ_PAYLOAD_TEXT;
    return TORUSTIQ_OK;
}

int32_t
Decode(void* handle, const torustiq_message* message, torustiq_emit_fn emit, void* user_data) {
    try {
        const auto* self = static_cast<const DecodeText*>(handle);
        const torustiq_bytes& body = message->payload.as.bytes;
        const std::string_view raw =
            body.len == 0 ? std::string_view()
                          : std::string_view(reinterpret_cast<const char*>(body.data), body.len);
        std::string text;
        if (!SanitizeUtf8(raw, self->replace_invalid, text)) {
            std::cerr << "decode_text: the data is not valid UTF-8\n";
            return TORUSTIQ_ERROR;
        }
        torustiq_message out{};
        out.payload = torustiq_value_from_text(text.data(), text.size());
        out.headers = message->headers;
        out.header_count = message->header_count;
        return emit(user_data, &out);
    } catch (const std::exception& e) {
        std::cerr << "decode_text: " << e.what() << "\n";
        return TORUSTIQ_ERROR;
    }
}

int32_t EncodeCreate(const torustiq_config*,
                     const torustiq_payload_spec* input,
                     torustiq_payload_spec* out_output,
                     void** out_handle) {
    if (input->kind != TORUSTIQ_PAYLOAD_TEXT) {
        return TORUSTIQ_UNSUPPORTED_PAYLOAD;
    }
    out_output->kind = TORUSTIQ_PAYLOAD_BYTES;
    *out_handle = nullptr; // stateless
    return TORUSTIQ_OK;
}

int32_t Encode(void*, const torustiq_message* message, torustiq_emit_fn emit, void* user_data) {
    // The string and bytes members share a layout, so only the kind changes.
    torustiq_message out = *message;
    out.payload =
        torustiq_value_from_bytes(message->payload.as.bytes.data, message->payload.as.bytes.len);
    return emit(user_data, &out);
}

void DecodeDestroy(void* handle) {
    delete static_cast<DecodeText*>(handle);
}

void EncodeDestroy(void*) {}

} // namespace

extern "C" int32_t torustiq_register_text_transforms(const torustiq_registrar* registrar) {
    torustiq_transform_vtable decode{};
    decode.abi_version = TORUSTIQ_SDK_ABI_VERSION;
    decode.name = "decode_text";
    decode.create = DecodeCreate;
    decode.process = Decode;
    decode.destroy = DecodeDestroy;
    if (registrar->register_transform(registrar->context, &decode) != TORUSTIQ_OK) {
        return TORUSTIQ_ERROR;
    }
    torustiq_transform_vtable encode{};
    encode.abi_version = TORUSTIQ_SDK_ABI_VERSION;
    encode.name = "encode_text";
    encode.create = EncodeCreate;
    encode.process = Encode;
    encode.destroy = EncodeDestroy;
    return registrar->register_transform(registrar->context, &encode);
}
