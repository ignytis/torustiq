#ifndef TORUSTIQ_CORE_MESSAGE_H_
#define TORUSTIQ_CORE_MESSAGE_H_

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "torustiq/sdk.h"

namespace torustiq {

// Host-side message: body bytes (length is the vector size) and string headers.
struct Message {
    std::vector<uint8_t> body;
    std::map<std::string, std::string> headers;

    bool operator==(const Message&) const = default;
};

// Deep-copies an SDK message.
Message MessageFromC(const torustiq_message& message);

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
    std::vector<torustiq_header> headers_;
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
