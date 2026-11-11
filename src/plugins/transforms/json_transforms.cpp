#include "plugins/transforms/json_transforms.h"

#include <deque>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace {

using Json = nlohmann::ordered_json;

constexpr int kMaxDepth = 128;

// Builds a torustiq_value tree that points into a parsed document and into its own storage.
// Both must outlive the tree.
class TreeBuilder {
  public:
    torustiq_value Build(const Json& json) {
        torustiq_value out{};
        switch (json.type()) {
        case Json::value_t::boolean:
            out.kind = TORUSTIQ_VALUE_BOOL;
            out.as.b = json.get<bool>() ? 1 : 0;
            break;
        case Json::value_t::number_integer:
            out.kind = TORUSTIQ_VALUE_INT;
            out.as.i = json.get<int64_t>();
            break;
        case Json::value_t::number_unsigned: {
            const auto number = json.get<uint64_t>();
            if (number <= static_cast<uint64_t>(INT64_MAX)) {
                out.kind = TORUSTIQ_VALUE_INT;
                out.as.i = static_cast<int64_t>(number);
            } else {
                out.kind = TORUSTIQ_VALUE_DOUBLE;
                out.as.d = static_cast<double>(number);
            }
            break;
        }
        case Json::value_t::number_float:
            out.kind = TORUSTIQ_VALUE_DOUBLE;
            out.as.d = json.get<double>();
            break;
        case Json::value_t::string: {
            const std::string& text = json.get_ref<const std::string&>();
            out.kind = TORUSTIQ_VALUE_STRING;
            out.as.bytes.data = reinterpret_cast<const uint8_t*>(text.data());
            out.as.bytes.len = text.size();
            break;
        }
        case Json::value_t::array: {
            auto& items = arrays_.emplace_back();
            items.reserve(json.size());
            for (const Json& item : json) {
                items.push_back(Build(item));
            }
            out.kind = TORUSTIQ_VALUE_ARRAY;
            out.as.array.items = items.data();
            out.as.array.count = items.size();
            break;
        }
        case Json::value_t::object: {
            auto& entries = maps_.emplace_back();
            entries.reserve(json.size());
            for (auto it = json.begin(); it != json.end(); ++it) {
                torustiq_map_entry entry{};
                entry.key.data = reinterpret_cast<const uint8_t*>(it.key().data());
                entry.key.len = it.key().size();
                entry.value = Build(it.value());
                entries.push_back(entry);
            }
            out.kind = TORUSTIQ_VALUE_MAP;
            out.as.map.entries = entries.data();
            out.as.map.count = entries.size();
            break;
        }
        default: // null (and the types the parser never produces)
            out.kind = TORUSTIQ_VALUE_NULL;
            break;
        }
        return out;
    }

  private:
    std::deque<std::vector<torustiq_value>> arrays_;
    std::deque<std::vector<torustiq_map_entry>> maps_;
};

std::string ToText(const torustiq_bytes& bytes) {
    return bytes.len == 0 ? std::string()
                          : std::string(reinterpret_cast<const char*>(bytes.data), bytes.len);
}

// Throws std::runtime_error for values JSON cannot represent.
Json ToJson(const torustiq_value& value, int depth) {
    if (depth > kMaxDepth) {
        throw std::runtime_error("value is nested too deeply");
    }
    switch (value.kind) {
    case TORUSTIQ_VALUE_NULL:
        return nullptr;
    case TORUSTIQ_VALUE_BOOL:
        return value.as.b != 0;
    case TORUSTIQ_VALUE_INT:
        return value.as.i;
    case TORUSTIQ_VALUE_DOUBLE:
        return value.as.d;
    case TORUSTIQ_VALUE_STRING:
        return ToText(value.as.bytes);
    case TORUSTIQ_VALUE_ARRAY: {
        Json array = Json::array();
        for (size_t i = 0; i < value.as.array.count; ++i) {
            array.push_back(ToJson(value.as.array.items[i], depth + 1));
        }
        return array;
    }
    case TORUSTIQ_VALUE_MAP: {
        Json object = Json::object();
        for (size_t i = 0; i < value.as.map.count; ++i) {
            const torustiq_map_entry& entry = value.as.map.entries[i];
            object[ToText(entry.key)] = ToJson(entry.value, depth + 1);
        }
        return object;
    }
    case TORUSTIQ_VALUE_BYTES:
        throw std::runtime_error("a bytes member cannot be encoded as JSON");
    default:
        throw std::runtime_error("unknown value kind");
    }
}

int32_t DecodeCreate(const torustiq_config*,
                     const torustiq_payload_spec* input,
                     torustiq_payload_spec* out_output,
                     void** out_handle) {
    if (input->kind != TORUSTIQ_PAYLOAD_BYTES) {
        return TORUSTIQ_UNSUPPORTED_PAYLOAD;
    }
    out_output->kind = TORUSTIQ_PAYLOAD_VALUE;
    *out_handle = nullptr; // stateless; destroy has nothing to free
    return TORUSTIQ_OK;
}

int32_t EncodeCreate(const torustiq_config*,
                     const torustiq_payload_spec* input,
                     torustiq_payload_spec* out_output,
                     void** out_handle) {
    if (input->kind != TORUSTIQ_PAYLOAD_VALUE) {
        return TORUSTIQ_UNSUPPORTED_PAYLOAD;
    }
    out_output->kind = TORUSTIQ_PAYLOAD_BYTES;
    *out_handle = nullptr;
    return TORUSTIQ_OK;
}

int32_t Decode(void*, const torustiq_message* message, torustiq_emit_fn emit, void* user_data) {
    try {
        const torustiq_bytes& body = message->payload.as.bytes;
        const char* begin = reinterpret_cast<const char*>(body.data);
        const Json document = Json::parse(begin, begin + body.len);
        TreeBuilder builder;
        torustiq_message out{};
        out.payload = builder.Build(document);
        out.headers = message->headers;
        out.header_count = message->header_count;
        return emit(user_data, &out);
    } catch (const std::exception& e) {
        std::cerr << "decode_json: " << e.what() << "\n";
        return TORUSTIQ_ERROR;
    }
}

int32_t Encode(void*, const torustiq_message* message, torustiq_emit_fn emit, void* user_data) {
    try {
        const std::string text =
            ToJson(message->payload, 0).dump(-1, ' ', false, Json::error_handler_t::replace);
        torustiq_message out{};
        out.payload =
            torustiq_value_from_bytes(reinterpret_cast<const uint8_t*>(text.data()), text.size());
        out.headers = message->headers;
        out.header_count = message->header_count;
        return emit(user_data, &out);
    } catch (const std::exception& e) {
        std::cerr << "encode_json: " << e.what() << "\n";
        return TORUSTIQ_ERROR;
    }
}

void Destroy(void*) {}

} // namespace

extern "C" int32_t torustiq_register_json_transforms(const torustiq_registrar* registrar) {
    torustiq_transform_vtable decode{};
    decode.abi_version = TORUSTIQ_SDK_ABI_VERSION;
    decode.name = "decode_json";
    decode.create = DecodeCreate;
    decode.process = Decode;
    decode.destroy = Destroy;
    if (registrar->register_transform(registrar->context, &decode) != TORUSTIQ_OK) {
        return TORUSTIQ_ERROR;
    }
    torustiq_transform_vtable encode{};
    encode.abi_version = TORUSTIQ_SDK_ABI_VERSION;
    encode.name = "encode_json";
    encode.create = EncodeCreate;
    encode.process = Encode;
    encode.destroy = Destroy;
    return registrar->register_transform(registrar->context, &encode);
}
