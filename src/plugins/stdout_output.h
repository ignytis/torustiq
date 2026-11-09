#ifndef TORUSTIQ_PLUGINS_STDOUT_OUTPUT_H_
#define TORUSTIQ_PLUGINS_STDOUT_OUTPUT_H_

#include <string>

#include "torustiq/sdk.h"

namespace torustiq::plugins {

// Expands `%BODY%` and `%HEADERS[name]%` (missing headers expand to an empty string).
std::string FormatMessage(const std::string& format, const torustiq_message& message);

} // namespace torustiq::plugins

extern "C" {
// Registers the "stdout" output. Config: `format` (default "%BODY%"); a newline follows each
// record.
int32_t torustiq_register_stdout_output(const torustiq_registrar* registrar);
}

#endif // TORUSTIQ_PLUGINS_STDOUT_OUTPUT_H_
