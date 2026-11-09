#ifndef TORUSTIQ_PLUGINS_STDIN_INPUT_H_
#define TORUSTIQ_PLUGINS_STDIN_INPUT_H_

#include "torustiq/sdk.h"

extern "C" {
// Registers the "stdin" input: each line read from standard input is a record.
// Note: stop() takes effect after the pending read returns (next line or EOF).
int32_t torustiq_register_stdin_input(const torustiq_registrar* registrar);
}

#endif // TORUSTIQ_PLUGINS_STDIN_INPUT_H_
