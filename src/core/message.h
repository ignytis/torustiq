#ifndef TORUSTIQ_CORE_MESSAGE_H_
#define TORUSTIQ_CORE_MESSAGE_H_

#include <cstddef>
#include <cstdint>
#include <deque>
#include <map>
#include <string>
#include <variant>
#include <vector>

#include "torustiq/sdk.h"

namespace torustiq {

using Bytes = std::vector<uint8_t>;

struct MapEntry;

// Host-side payload value, owning. Alternatives are ordered like torustiq_value_kind, so
// `data.index()` is the SDK kind: null, bool, int, double, string, bytes, array, map.
struct Value {
    using Array = std::vector<Value>;
    using Map = std::vector<MapEntry>; // ordered, string keys

    Value() = default;
    Value(std::nullptr_t) {}
    Value(bool v) : data(v) {}
    Value(int64_t v) : data(v) {}
    Value(double v) : data(v) {}
    Value(std::string v) : data(std::move(v)) {}
    Value(const char* v) : data(std::string(v)) {}
    Value(Bytes v) : data(std::move(v)) {}
    Value(Array v) : data(std::move(v)) {}
    Value(Map v) : data(std::move(v)) {}

    bool IsBytes() const {
        return std::holds_alternative<Bytes>(data);
    }
    // Returns the map member `key`, or nullptr if this is not a map or has no such key.
    const Value* Find(const std::string& key) const;
    Value* Find(const std::string& key);

    bool operator==(const Value&) const = default;

    std::variant<std::nullptr_t, bool, int64_t, double, std::string, Bytes, Array, Map> data;
};

struct MapEntry {
    std::string key;
    Value value;

    bool operator==(const MapEntry&) const = default;
};

// Host-side message: a payload (bytes for simple data, anything else for structured data)
// and string headers. A default message has an empty bytes payload.
struct Message {
    Value payload{Bytes{}};
    std::map<std::string, std::string> headers;

    bool operator==(const Message&) const = default;
};

// Builds a message with a bytes payload.
Message MakeBytesMessage(const std::string& body);

// The payload of a bytes message as a string; empty if the payload is not bytes.
std::string BodyText(const Message& message);

// The payload kind of a value as agreed between steps: TORUSTIQ_PAYLOAD_BYTES or _VALUE.
uint32_t PayloadKindOf(const Value& value);
uint32_t PayloadKindOf(const torustiq_value& value);

constexpr int kMaxValueDepth = 128;

// Deep-copies an SDK message. Throws std::invalid_argument for a malformed payload tree (unknown
// kind, null data with a non-zero length, nesting deeper than kMaxValueDepth).
Message MessageFromC(const torustiq_message& message);

// Deep-copies an SDK value. Throws like MessageFromC.
Value ValueFromC(const torustiq_value& value);

// Exposes a Message as a torustiq_message. The Message must outlive the view.
class MessageView {
  public:
    explicit MessageView(const Message& message);
    MessageView(const MessageView&) = delete;
    MessageView& operator=(const MessageView&) = delete;

    const torustiq_message& get() const {
        return message_;
    }

  private:
    torustiq_value Build(const Value& value);

    std::vector<torustiq_header> headers_;
    std::deque<std::vector<torustiq_value>> arrays_;
    std::deque<std::vector<torustiq_map_entry>> maps_;
    torustiq_message message_{};
};

// Exposes a string map as a torustiq_config. The map must outlive the view.
class ConfigView {
  public:
    explicit ConfigView(const std::map<std::string, std::string>& values);
    ConfigView(const ConfigView&) = delete;
    ConfigView& operator=(const ConfigView&) = delete;

    const torustiq_config& get() const {
        return config_;
    }

  private:
    std::vector<torustiq_header> items_;
    torustiq_config config_{};
};

} // namespace torustiq

#endif // TORUSTIQ_CORE_MESSAGE_H_
