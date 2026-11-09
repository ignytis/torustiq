#ifndef TORUSTIQ_PLUGINS_INPUTS_DIRECTORY_INPUT_H_
#define TORUSTIQ_PLUGINS_INPUTS_DIRECTORY_INPUT_H_

#include "torustiq/sdk.h"

extern "C" {
// Registers the "directory" input. Config: `path` (required). Every regular file directly inside
// the directory (sorted by name) is a record; headers: `filename`, `path`.
int32_t torustiq_register_directory_input(const torustiq_registrar* registrar);
}

#endif // TORUSTIQ_PLUGINS_INPUTS_DIRECTORY_INPUT_H_
