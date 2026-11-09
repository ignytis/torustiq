#include "core/message.h"

namespace torustiq {

Message MessageFromC(const torustiq_message& message) {
    Message result;
    if (message.body.data != nullptr && message.body.len > 0) {
        result.body.assign(message.body.data, message.body.data + message.body.len);
    }
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
    message_.body.data = message.body.data();
    message_.body.len = message.body.size();
    message_.headers = headers_.data();
    message_.header_count = headers_.size();
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
