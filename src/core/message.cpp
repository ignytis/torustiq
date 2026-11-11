#include "core/message.h"

#include <stdexcept>

namespace torustiq {
namespace {

Bytes CopyBytes(const torustiq_bytes& bytes) {
    if (bytes.len == 0) {
        return {};
    }
    if (bytes.data == nullptr) {
        throw std::invalid_argument("payload bytes: null data with non-zero length");
    }
    return Bytes(bytes.data, bytes.data + bytes.len);
}

Value CopyValue(const torustiq_value& value, int depth) {
    if (depth > kMaxValueDepth) {
        throw std::invalid_argument("payload is nested too deeply");
    }
    switch (value.kind) {
    case TORUSTIQ_VALUE_NULL:
        return Value();
    case TORUSTIQ_VALUE_BOOL:
        return Value(value.as.b != 0);
    case TORUSTIQ_VALUE_INT:
        return Value(value.as.i);
    case TORUSTIQ_VALUE_DOUBLE:
        return Value(value.as.d);
    case TORUSTIQ_VALUE_STRING: {
        const Bytes bytes = CopyBytes(value.as.bytes);
        return Value(std::string(bytes.begin(), bytes.end()));
    }
    case TORUSTIQ_VALUE_BYTES:
        return Value(CopyBytes(value.as.bytes));
    case TORUSTIQ_VALUE_ARRAY: {
        if (value.as.array.count > 0 && value.as.array.items == nullptr) {
            throw std::invalid_argument("payload array: null items");
        }
        Value::Array array;
        array.reserve(value.as.array.count);
        for (size_t i = 0; i < value.as.array.count; ++i) {
            array.push_back(CopyValue(value.as.array.items[i], depth + 1));
        }
        return Value(std::move(array));
    }
    case TORUSTIQ_VALUE_MAP: {
        if (value.as.map.count > 0 && value.as.map.entries == nullptr) {
            throw std::invalid_argument("payload map: null entries");
        }
        Value::Map map;
        map.reserve(value.as.map.count);
        for (size_t i = 0; i < value.as.map.count; ++i) {
            const torustiq_map_entry& entry = value.as.map.entries[i];
            const Bytes key = CopyBytes(entry.key);
            map.push_back({std::string(key.begin(), key.end()), CopyValue(entry.value, depth + 1)});
        }
        return Value(std::move(map));
    }
    default:
        throw std::invalid_argument("payload: unknown value kind " + std::to_string(value.kind));
    }
}

} // namespace

const Value* Value::Find(const std::string& key) const {
    const auto* map = std::get_if<Map>(&data);
    if (map == nullptr) {
        return nullptr;
    }
    for (const MapEntry& entry : *map) {
        if (entry.key == key) {
            return &entry.value;
        }
    }
    return nullptr;
}

Value* Value::Find(const std::string& key) {
    return const_cast<Value*>(static_cast<const Value*>(this)->Find(key));
}

Message MakeBytesMessage(const std::string& body) {
    Message message;
    message.payload = Value(Bytes(body.begin(), body.end()));
    return message;
}

std::string BodyText(const Message& message) {
    const auto* bytes = std::get_if<Bytes>(&message.payload.data);
    return bytes == nullptr ? std::string() : std::string(bytes->begin(), bytes->end());
}

uint32_t PayloadKindOf(const Value& value) {
    return value.IsBytes() ? TORUSTIQ_PAYLOAD_BYTES : TORUSTIQ_PAYLOAD_VALUE;
}

uint32_t PayloadKindOf(const torustiq_value& value) {
    return value.kind == TORUSTIQ_VALUE_BYTES ? TORUSTIQ_PAYLOAD_BYTES : TORUSTIQ_PAYLOAD_VALUE;
}

Value ValueFromC(const torustiq_value& value) {
    return CopyValue(value, 0);
}

Message MessageFromC(const torustiq_message& message) {
    Message result;
    result.payload = ValueFromC(message.payload);
    for (size_t i = 0; i < message.header_count; ++i) {
        const torustiq_header& header = message.headers[i];
        if (header.key == nullptr) {
            continue;
        }
        result.headers[header.key] = header.value != nullptr ? header.value : "";
    }
    return result;
}

MessageView::MessageView(const Message& message) {
    headers_.reserve(message.headers.size());
    for (const auto& [key, value] : message.headers) {
        headers_.push_back({key.c_str(), value.c_str()});
    }
    message_.payload = Build(message.payload);
    message_.headers = headers_.data();
    message_.header_count = headers_.size();
}

// Storage for nested nodes lives in deques, so pointers to it stay valid while the view lives.
torustiq_value MessageView::Build(const Value& value) {
    torustiq_value out{};
    out.kind = static_cast<uint32_t>(value.data.index());
    switch (out.kind) {
    case TORUSTIQ_VALUE_NULL:
        break;
    case TORUSTIQ_VALUE_BOOL:
        out.as.b = std::get<bool>(value.data) ? 1 : 0;
        break;
    case TORUSTIQ_VALUE_INT:
        out.as.i = std::get<int64_t>(value.data);
        break;
    case TORUSTIQ_VALUE_DOUBLE:
        out.as.d = std::get<double>(value.data);
        break;
    case TORUSTIQ_VALUE_STRING: {
        const auto& text = std::get<std::string>(value.data);
        out.as.bytes.data = reinterpret_cast<const uint8_t*>(text.data());
        out.as.bytes.len = text.size();
        break;
    }
    case TORUSTIQ_VALUE_BYTES: {
        const auto& bytes = std::get<Bytes>(value.data);
        out.as.bytes.data = bytes.data();
        out.as.bytes.len = bytes.size();
        break;
    }
    case TORUSTIQ_VALUE_ARRAY: {
        const auto& array = std::get<Value::Array>(value.data);
        auto& items = arrays_.emplace_back();
        items.reserve(array.size());
        for (const Value& item : array) {
            items.push_back(Build(item));
        }
        out.as.array.items = items.data();
        out.as.array.count = items.size();
        break;
    }
    case TORUSTIQ_VALUE_MAP: {
        const auto& map = std::get<Value::Map>(value.data);
        auto& entries = maps_.emplace_back();
        entries.reserve(map.size());
        for (const MapEntry& entry : map) {
            torustiq_map_entry out_entry{};
            out_entry.key.data = reinterpret_cast<const uint8_t*>(entry.key.data());
            out_entry.key.len = entry.key.size();
            out_entry.value = Build(entry.value);
            entries.push_back(out_entry);
        }
        out.as.map.entries = entries.data();
        out.as.map.count = entries.size();
        break;
    }
    }
    return out;
}

ConfigView::ConfigView(const std::map<std::string, std::string>& values) {
    items_.reserve(values.size());
    for (const auto& [key, value] : values) {
        items_.push_back({key.c_str(), value.c_str()});
    }
    config_.items = items_.data();
    config_.count = items_.size();
}

} // namespace torustiq
