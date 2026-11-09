#ifndef TORUSTIQ_PLUGINS_INPUTS_LUA_INPUT_H_
#define TORUSTIQ_PLUGINS_INPUTS_LUA_INPUT_H_

#include "torustiq/sdk.h"

extern "C" {
// Registers the "lua" input. Parameters: `script` (inline code) or `file`, optional
// `max_instructions` (see lua_runtime.h; for inputs it applies between two emit() calls).
//
//   function run(emit)   -- call emit(msg) per message; emit returns false when the pipeline wants
//                        -- no more data. `sleep(seconds)` is available.
//
// A message is a table {body = "bytes", headers = {key = "value"}}; a plain string means body only.
int32_t torustiq_register_lua_input(const torustiq_registrar* registrar);
}

#endif // TORUSTIQ_PLUGINS_INPUTS_LUA_INPUT_H_
