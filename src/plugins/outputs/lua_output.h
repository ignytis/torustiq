#ifndef TORUSTIQ_PLUGINS_OUTPUTS_LUA_OUTPUT_H_
#define TORUSTIQ_PLUGINS_OUTPUTS_LUA_OUTPUT_H_

#include "torustiq/sdk.h"

extern "C" {
// Registers the "lua" output. Parameters: `script` (inline code) or `file`, optional
// `max_instructions` (see lua_runtime.h).
//
//   function write(msg)  -- consume a message (e.g. print it)
//   function flush()     -- optional; called once when the stream ends
//
// A message is a table {body = "bytes", headers = {key = "value"}}.
int32_t torustiq_register_lua_output(const torustiq_registrar* registrar);
}

#endif // TORUSTIQ_PLUGINS_OUTPUTS_LUA_OUTPUT_H_
